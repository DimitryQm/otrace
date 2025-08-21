#ifndef OTRACE_HPP_INCLUDED
#define OTRACE_HPP_INCLUDED

/**
 * @file    otrace.hpp
 * @brief   Header-only in-process timeline instrumentation for Perfetto / Chrome Trace Event JSON.
 * @author  DimitryQm
 * @version 0.1.0
 * @date    2025-08-20
 * @license MIT
 * @see     https://ui.perfetto.dev/ , chrome://tracing
 *
 * otrace — drop-in annotations for scopes, instants, counters, flows, and frames.
 * Single header, zero deps, microsecond timestamps, per-thread lock-free rings, safe flush.
 *
 * Build flags (define at compile time):
 *   -DOTRACE=1                         Enable tracing (default 0 = disabled)
 *   -DOTRACE_THREAD_BUFFER_EVENTS=N    Events per thread (default 1<<15)
 *   -DOTRACE_DEFAULT_PATH="file.json"  Output file (default "trace.json")
 *   -DOTRACE_ON_EXIT=1                 Auto-flush at process exit (default 1)
 *   -DOTRACE_CLOCK=1|2|3               1=steady_clock, 2=RDTSC(x86), 3=system_clock
 *   -DOTRACE_MAX_ARGS=N                Max args per event (default 4)
 *
 * Public API (when OTRACE==1) — examples:
 *   TRACE_SCOPE("step");                              // complete slice (ph:"X")
 *   TRACE_BEGIN("upload"); TRACE_END("upload");       // begin/end pair
 *   TRACE_INSTANT("tick");                            // instant (ph:"i")
 *   TRACE_INSTANT_C("tick","frame");                  // instant with category
 *   TRACE_INSTANT_CKV("tick","frame","phase",42);     // instant with key/value
 *   TRACE_COUNTER("queue_len", n);                    // counter sample
 *   TRACE_FLOW_BEGIN(id); TRACE_FLOW_STEP(id); TRACE_FLOW_END(id);
 *   TRACE_MARK_FRAME(i); TRACE_MARK_FRAME_S("present");
 *   TRACE_SET_THREAD_NAME("worker-0"); TRACE_SET_PROCESS_NAME("my-app");
 *   TRACE_SET_THREAD_SORT_INDEX(10);
 *   TRACE_COLOR("good");                              // color hint for next event
 *   TRACE_SET_OUTPUT_PATH("trace.json");              // set output path at runtime
 *   TRACE_FLUSH(nullptr);                             // force flush now
 *
 * Requirements: C++17+, Windows/Linux/macOS. Not async-signal-safe.
 * SPDX-License-Identifier: MIT
 */

#if !defined(__cplusplus) || __cplusplus < 201703L
#  error "otrace.hpp requires C++17 or later"
#endif

#ifndef OTRACE_NO_SHORT_MACROS
#define OTRACE_NO_SHORT_MACROS 0   // set to 1 before including to hide TRACE_* aliases
#endif

#ifndef OTRACE
#define OTRACE 0
#endif

#ifndef OTRACE_THREAD_BUFFER_EVENTS
#define OTRACE_THREAD_BUFFER_EVENTS (1u<<15)  // 32768
#endif

#ifndef OTRACE_DEFAULT_PATH
#define OTRACE_DEFAULT_PATH "trace.json"
#endif

#ifndef OTRACE_ON_EXIT
#define OTRACE_ON_EXIT 1
#endif

#ifndef OTRACE_CLOCK
#define OTRACE_CLOCK 1  // 1=steady_clock (default), 2=RDTSC (x86), 3=system_clock
#endif

#ifndef OTRACE_MAX_ARGS
#define OTRACE_MAX_ARGS 4
#endif


// Public Macros (no-ops when OTRACE == 0)
#if OTRACE

#include <atomic>
#include <cstdint>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <chrono>
#include <vector>
#include <algorithm>
#include <string>
#include <utility>

#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <processthreadsapi.h>
#elif defined(__APPLE__)
  #include <pthread.h>
  #include <sys/types.h>
  #include <unistd.h>
#else
  #include <sys/syscall.h>
  #include <sys/types.h>
  #include <unistd.h>
#endif

#if OTRACE_CLOCK==2 && (defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64))
  #if defined(_MSC_VER)
    #include <intrin.h>
    #include <immintrin.h>
  #else
    #include <x86intrin.h>
    #include <immintrin.h>
  #endif
#endif


namespace otrace {

// ---- Platform helpers -----------------------------------------------------
#if OTRACE_CLOCK==2 && (defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64))
  static inline uint64_t rdtsc() noexcept { return __rdtsc(); }
#endif

struct AtExitHook;                   // forward
inline AtExitHook& hook();           // forward
inline uint32_t pid() {
#if defined(_WIN32)
  return static_cast<uint32_t>(::GetCurrentProcessId());
#else
  return static_cast<uint32_t>(::getpid());
#endif
}

inline uint32_t tid() {
#if defined(_WIN32)
  return static_cast<uint32_t>(::GetCurrentThreadId());
#elif defined(__APPLE__)
  uint64_t tid64 = 0;
  pthread_threadid_np(nullptr, &tid64);
  return static_cast<uint32_t>(tid64);
#elif defined(__linux__)
  return static_cast<uint32_t>(::syscall(SYS_gettid));
#else
  return static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
}

// ---- Timestamp source -----------------------------------------------------
struct Timebase {
  // Ensure build-time correctness.
  static_assert(OTRACE_CLOCK==1 || OTRACE_CLOCK==2 || OTRACE_CLOCK==3,
                "OTRACE_CLOCK must be 1 (steady), 2 (RDTSC), or 3 (system)");

  // Returns microseconds since first use.
  static uint64_t now_us() {
#if OTRACE_CLOCK==1
    using clk = std::chrono::steady_clock;
    static const auto t0 = clk::now();
    auto d = clk::now() - t0;
    return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(d).count();

#elif OTRACE_CLOCK==3
    // NOTE: system_clock can be adjusted by NTP or manual changes; deltas may jump.
    using clk = std::chrono::system_clock;
    static const auto t0 = clk::now();
    auto d = clk::now() - t0;
    return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(d).count();

#elif OTRACE_CLOCK==2
  #if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
    // Read TSC with serialization to reduce OoO noise.
    auto rdtsc_read = []() noexcept -> uint64_t {
    #if defined(_MSC_VER)
      _mm_lfence();
      unsigned __int64 v = __rdtsc();
      _mm_lfence();
      return (uint64_t)v;
    #else
      _mm_lfence();
      unsigned long long v = __rdtsc();
      _mm_lfence();
      return (uint64_t)v;
    #endif
    };

    // Calibrate cycles -> usec against steady_clock, pick a good (min) estimate.
    struct Cal {
      double cycles_per_us;
      Cal() {
        using clk = std::chrono::steady_clock;
        const int iters = 5;
        double best = 1e300;
        for (int i = 0; i < iters; ++i) {
          auto t0 = clk::now(); uint64_t c0 = rdtsc_read();
          // busy for ~1ms
          while (std::chrono::duration_cast<std::chrono::microseconds>(clk::now() - t0).count() < 1000) {}
          auto t1 = clk::now(); uint64_t c1 = rdtsc_read();
          double us  = (double)std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
          double cyc = (double)(c1 - c0);
          double cpu = cyc / us;
          if (cpu < best) best = cpu;
        }
        cycles_per_us = (best > 0.0) ? best : 1.0;
      }
    };
    static Cal cal;

    // Per-process baseline
    static const uint64_t c0 = rdtsc_read();
    uint64_t c = rdtsc_read() - c0;
    return (uint64_t)((double)c / cal.cycles_per_us);
  #else
    // Non-x86 fallback to steady_clock
    using clk = std::chrono::steady_clock;
    static const auto t0 = clk::now();
    auto d = clk::now() - t0;
    return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(d).count();
  #endif

#else
    // Defensive fallback if OTRACE_CLOCK somehow has an unexpected value.
    using clk = std::chrono::steady_clock;
    static const auto t0 = clk::now();
    auto d = clk::now() - t0;
    return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(d).count();
#endif
  }
};


inline uint64_t now_us() { return Timebase::now_us(); }

// ---- Trace event model ----------------------------------------------------

enum class Phase : uint8_t {
  B, E, X, I, C,                      // Begin, End, Complete, Instant, Counter
  MThreadName, MProcessName, MThreadSortIndex,
  FlowStart, FlowStep, FlowEnd
};

enum class ArgKind : uint8_t { None, Number, String };

#ifndef OTRACE_MAX_NAME
#define OTRACE_MAX_NAME 64
#endif
#ifndef OTRACE_MAX_CAT
#define OTRACE_MAX_CAT  32
#endif
#ifndef OTRACE_MAX_ARGK
#define OTRACE_MAX_ARGK 32
#endif
#ifndef OTRACE_MAX_ARGV
#define OTRACE_MAX_ARGV 64
#endif
#ifndef OTRACE_MAX_CNAME
#define OTRACE_MAX_CNAME 16
#endif

struct Arg {
  char    key[OTRACE_MAX_ARGK];
  ArgKind kind;
  double  num;
  char    str[OTRACE_MAX_ARGV];
};

// Event stored in per‑thread ring
struct Event {
  uint64_t ts_us;             // timestamp
  uint64_t dur_us;            // for Complete (X)
  uint64_t flow_id;           // for flows (s/t/f)
  uint32_t pid;
  uint32_t tid;
  Phase    ph;                // event phase
  char     name[OTRACE_MAX_NAME];
  char     cat[OTRACE_MAX_CAT];
  char     cname[OTRACE_MAX_CNAME]; // optional color name
  uint8_t  argc;              // number of args used
  Arg      args[OTRACE_MAX_ARGS];
  std::atomic<uint8_t> committed;   // 0 while being written, 1 when complete

  Event() : ts_us(0), dur_us(0), flow_id(0), pid(0), tid(0), ph(Phase::I), argc(0), committed{0} {
    name[0]=cat[0]=cname[0]='\0';
    for (int i=0;i<OTRACE_MAX_ARGS;i++){ args[i].key[0]='\0'; args[i].kind=ArgKind::None; args[i].num=0; args[i].str[0]='\0'; }
  }
};

// Per‑thread ring buffer, lock‑free for the owning thread.
struct ThreadBuffer {
  ThreadBuffer* next;
  uint32_t      tid_v;
  char          thread_name[OTRACE_MAX_NAME];
  int           thread_sort_index;
  Event*        buf;
  uint32_t      cap;
  uint32_t      head;
  bool          wrapped;
  char          pending_cname[OTRACE_MAX_CNAME]; // color hint for next event only

  ThreadBuffer(uint32_t capacity)
  : next(nullptr), tid_v(otrace::tid()), thread_sort_index(0), buf(nullptr), cap(capacity), head(0), wrapped(false) {
    thread_name[0] = '\0';
    pending_cname[0] = '\0';
    buf = new Event[cap];
  }

  ~ThreadBuffer() { delete[] buf; }

  Event* append() {
    uint32_t idx = head++;
    if (head >= cap) { head = 0; wrapped = true; }
    Event* e = &buf[idx];
    // mark slot as in‑flight
    e->committed.store(0, std::memory_order_relaxed);
    // reset dynamic fields (cheap, skip large memsets)
    e->argc = 0; e->dur_us = 0; e->flow_id = 0;
    e->name[0]=0; e->cat[0]=0; 
    if (pending_cname[0]) { std::snprintf(e->cname, sizeof(e->cname), "%s", pending_cname); pending_cname[0]=0; }
    else e->cname[0]=0;
    return e;
  }
};

// Global registry of all thread buffers
struct Registry {
  std::atomic<ThreadBuffer*> head { nullptr };
  std::atomic<bool> enabled { true };
  uint32_t pid_v { otrace::pid() };
  char process_name[OTRACE_MAX_NAME];
  char default_path[256];

  Registry() {
    process_name[0] = '\0';
    std::snprintf(default_path, sizeof(default_path), "%s", OTRACE_DEFAULT_PATH);
  }
};

inline Registry& reg() { static Registry R; return R; }

// thread‑local buffer registration
inline ThreadBuffer* get_tbuf() {
  thread_local ThreadBuffer* TB = nullptr;
  if (TB) return TB;
  (void)hook(); 
  TB = new ThreadBuffer(OTRACE_THREAD_BUFFER_EVENTS);
  ThreadBuffer* old = reg().head.load(std::memory_order_relaxed);
  do { TB->next = old; } while(!reg().head.compare_exchange_weak(old, TB, std::memory_order_release, std::memory_order_relaxed));
  return TB;
}

// ---- Tiny JSON helpers ----------------------------------------------------

inline void json_escape_and_write(FILE* f, const char* s) {
  std::fputc('"', f);
  for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
    unsigned char c = *p;
    switch (c) {
      case '"': std::fputs("\\\"", f); break;
      case '\\': std::fputs("\\\\", f); break;
      case '\b': std::fputs("\\b", f); break;
      case '\f': std::fputs("\\f", f); break;
      case '\n': std::fputs("\\n", f); break;
      case '\r': std::fputs("\\r", f); break;
      case '\t': std::fputs("\\t", f); break;
      default:
        if (c < 0x20) { std::fprintf(f, "\\u%04x", c); }
        else { std::fputc(c, f); }
    }
  }
  std::fputc('"', f);
}

// Templated args writer (works for Event and CleanEvent)
template <class E>
inline void write_args_json_common(FILE* f, const E& e) {
  if (e.argc == 0) return;
  std::fputs(",\"args\":{", f);
  for (uint8_t i = 0; i < e.argc; i++) {
    if (i) std::fputc(',', f);
    json_escape_and_write(f, e.args[i].key); std::fputc(':', f);
    if (e.args[i].kind == ArgKind::Number) {
      std::fprintf(f, "%g", e.args[i].num);
    } else if (e.args[i].kind == ArgKind::String) {
      json_escape_and_write(f, e.args[i].str);
    } else {
      std::fputs("null", f);
    }
  }
  std::fputc('}', f);
}


// Templated event writer (no UB; used by both Event and CleanEvent)
template <class E>
inline void write_event_json_common(FILE* f, const E& e) {
  std::fputc('{', f);

  // name/cat (metadata uses fixed names)
  if (e.ph == Phase::MThreadName || e.ph == Phase::MProcessName || e.ph == Phase::MThreadSortIndex) {
    std::fputs("\"name\":", f);
    if (e.ph == Phase::MThreadName)      std::fputs("\"thread_name\"", f);
    else if (e.ph == Phase::MProcessName)std::fputs("\"process_name\"", f);
    else                                  std::fputs("\"thread_sort_index\"", f);
  } else {
        std::fputs("\"name\":", f); json_escape_and_write(f, e.name);
        std::fputs(",\"cat\":", f);
        json_escape_and_write(f, e.cat[0] ? e.cat : "");
}


  // phase
  std::fputs(",\"ph\":\"", f);
  switch (e.ph) {
    case Phase::B: std::fputs("B", f); break;
    case Phase::E: std::fputs("E", f); break;
    case Phase::X: std::fputs("X", f); break;
    case Phase::I: std::fputs("I", f); break;
    case Phase::C: std::fputs("C", f); break;
    case Phase::MThreadName:
    case Phase::MProcessName:
    case Phase::MThreadSortIndex: std::fputs("M", f); break;
    case Phase::FlowStart: std::fputs("s", f); break;
    case Phase::FlowStep:  std::fputs("t", f); break;
    case Phase::FlowEnd:   std::fputs("f", f); break;
  }
  std::fputc('"', f);

  // timestamps & ids
  std::fprintf(f, ",\"ts\":%" PRIu64, (uint64_t)e.ts_us);
  std::fprintf(f, ",\"pid\":%" PRIu32 ",\"tid\":%" PRIu32,(uint32_t)e.pid, (uint32_t)e.tid);

  // instant scope (thread)
  if (e.ph == Phase::I) { std::fputs(",\"s\":\"t\"", f); }

  // duration for completes
  if (e.ph == Phase::X) {
    std::fprintf(f, ",\"dur\":%" PRIu64, (uint64_t)e.dur_us);
  }

  // flow id
  if (e.ph == Phase::FlowStart || e.ph == Phase::FlowStep || e.ph == Phase::FlowEnd) {
    std::fprintf(f, ",\"id\":%" PRIu64, (uint64_t)e.flow_id);
  }

  // color hint
  if (e.cname[0]) { std::fputs(",\"cname\":", f); json_escape_and_write(f, e.cname); }

  // args
  if (e.ph == Phase::MThreadName || e.ph == Phase::MProcessName) {
    std::fputs(",\"args\":{\"name\":", f); json_escape_and_write(f, e.name); std::fputc('}', f);
  } else if (e.ph == Phase::MThreadSortIndex) {
    std::fputs(",\"args\":{\"sort_index\":", f);
    double v = (e.argc > 0 && e.args[0].kind == ArgKind::Number) ? e.args[0].num : 0.0;
    std::fprintf(f, "%g}", v);
  } else {
    write_args_json_common(f, e);
  }

  std::fputc('}', f);

}
inline void write_event_json(FILE* f, const Event& e)      { write_event_json_common(f, e); }
    
// ---- Emit helpers ---------------------------------------------------------

inline void arg_number(Event& e, const char* key, double val) {
  if (!key || e.argc >= OTRACE_MAX_ARGS) return;
  Arg& a = e.args[e.argc++];
  std::snprintf(a.key, sizeof(a.key), "%s", key);
  a.kind = ArgKind::Number; a.num = val; a.str[0]=0;
}
inline void arg_string(Event& e, const char* key, const char* s) {
  if (!key || !s || e.argc >= OTRACE_MAX_ARGS) return;
  Arg& a = e.args[e.argc++];
  std::snprintf(a.key, sizeof(a.key), "%s", key);
  a.kind = ArgKind::String; std::snprintf(a.str, sizeof(a.str), "%s", s);
}

inline void fill_common(Event& e, Phase ph, const char* name, const char* cat) {
  // recompute PID lazily in case of fork
  e.ts_us = now_us();
  e.dur_us = 0;
  uint32_t p = otrace::pid();
  if (p != reg().pid_v) reg().pid_v = p;
  e.pid = reg().pid_v;
  e.tid = get_tbuf()->tid_v;
  e.ph = ph;
  e.name[0] = e.cat[0] = '\0';
  if (name) { std::snprintf(e.name, sizeof(e.name), "%s", name); }
  if (cat)  { std::snprintf(e.cat,  sizeof(e.cat),  "%s", cat); }
}

inline void commit(Event* ev) {
  ev->committed.store(1, std::memory_order_release);
}

inline bool enabled() {
  return reg().enabled.load(std::memory_order_relaxed);
}

inline void emit_begin(const char* name, const char* cat=nullptr) {
  if (!enabled()) return;
  Event* ev = get_tbuf()->append();
  fill_common(*ev, Phase::B, name, cat);
  commit(ev);
}

inline void emit_end(const char* name, const char* cat=nullptr) {
  if (!enabled()) return;
  Event* ev = get_tbuf()->append();
  fill_common(*ev, Phase::E, name, cat);
  commit(ev);
}

inline void emit_instant(const char* name, const char* cat=nullptr) {
  if (!enabled()) return;
  Event* ev = get_tbuf()->append();
  fill_common(*ev, Phase::I, name, cat);
  commit(ev);
}

inline void emit_instant_kv(const char* name, const char* key, double val, const char* cat=nullptr) {
  if (!enabled()) return;
  Event* ev = get_tbuf()->append();
  fill_common(*ev, Phase::I, name, cat);
  if (key) arg_number(*ev, key, val);
  commit(ev);
}

inline void emit_counter_n(const char* name, const char* cat, int n, const char** keys, const double* vals) {
  if (!enabled()) return;
  Event* ev = get_tbuf()->append();
  fill_common(*ev, Phase::C, name, cat);
  for (int i=0;i<n && i<(int)OTRACE_MAX_ARGS;i++) arg_number(*ev, keys[i], vals[i]);
  // ensure the primary series exists: if no keys provided, use event name as key
  if (n==0) arg_number(*ev, name, 0.0);
  commit(ev);
}

inline void emit_complete(const char* name, uint64_t dur_us, const char* cat=nullptr) {
  if (!enabled()) return;
  Event* ev = get_tbuf()->append();
  fill_common(*ev, Phase::X, name, cat);
  ev->dur_us = dur_us;
  commit(ev);
}

inline void emit_complete_kv(const char* name, uint64_t dur_us, const char* key, double val, const char* cat=nullptr) {
  if (!enabled()) return;
  Event* ev = get_tbuf()->append();
  fill_common(*ev, Phase::X, name, cat);
  ev->dur_us = dur_us;
  if (key) arg_number(*ev, key, val);
  commit(ev);
}

inline void emit_thread_name(const char* name) {
  if (!enabled()) return;
  ThreadBuffer* tb = get_tbuf();
  std::snprintf(tb->thread_name, sizeof(tb->thread_name), "%s", name ? name : "");
  Event* ev = tb->append();
  fill_common(*ev, Phase::MThreadName, name ? name : "", "");
  commit(ev);
}

inline void emit_thread_sort_index(int sort_index) {
  if (!enabled()) return;
  ThreadBuffer* tb = get_tbuf();
  tb->thread_sort_index = sort_index;
  Event* ev = tb->append();
  fill_common(*ev, Phase::MThreadSortIndex, "", "");
  arg_number(*ev, "sort_index", (double)sort_index);
  commit(ev);
}

inline void emit_process_name(const char* name) {
  if (!enabled()) return;
  Event* ev = get_tbuf()->append();
  fill_common(*ev, Phase::MProcessName, name ? name : "", "");
  commit(ev);
}

inline void set_next_color(const char* cname) {
  ThreadBuffer* tb = get_tbuf();
  if (cname) std::snprintf(tb->pending_cname, sizeof(tb->pending_cname), "%s", cname);
}
    
inline void emit_flow(Phase ph, uint64_t id, const char* name=nullptr, const char* cat=nullptr) {
  if (!enabled()) return;
  Event* ev = get_tbuf()->append();
  if (!name) name = "flow";
  if (!cat)  cat  = "flow";
  fill_common(*ev, ph, name, cat);
  ev->flow_id = id;
  commit(ev);
}

// RAII scope -> Complete (X)
struct Scope {
  const char* name;
  const char* cat;
  const char* arg_key;
  double arg_val;
  bool has_arg;
  uint64_t t0;
  Scope(const char* nm, const char* ct=nullptr)
  : name(nm), cat(ct), arg_key(nullptr), arg_val(0), has_arg(false), t0(now_us()) {}
  Scope(const char* nm, const char* ct, const char* key, double val)
  : name(nm), cat(ct), arg_key(key), arg_val(val), has_arg(true), t0(now_us()) {}
  ~Scope() {
    uint64_t dur = now_us() - t0;
    if (has_arg) emit_complete_kv(name, dur, arg_key, arg_val, cat);
    else emit_complete(name, dur, cat);
  }
};

// ---- Flush ----------------------------------------------------------------

struct CleanEvent {
  // copy‑friendly event for sorting/writing (no atomics)
  uint64_t ts_us, dur_us, flow_id; uint32_t pid, tid; Phase ph; 
  char name[OTRACE_MAX_NAME]; char cat[OTRACE_MAX_CAT]; char cname[OTRACE_MAX_CNAME];
  uint8_t argc; Arg args[OTRACE_MAX_ARGS];
};

inline void set_output_path(const char* path) {
  if (!path) return;
  std::snprintf(reg().default_path, sizeof(reg().default_path), "%s", path);
}

inline void collect_all(std::vector<CleanEvent>& out) {
  // Walk thread buffers and copy only committed events with acquire
  for (ThreadBuffer* tb = reg().head.load(std::memory_order_acquire); tb; tb = tb->next) {
    uint32_t count = tb->wrapped ? tb->cap : tb->head;
    uint32_t start = tb->wrapped ? tb->head : 0;
    for (uint32_t i = 0; i < count; ++i) {
      uint32_t idx = start + i; if (idx >= tb->cap) idx -= tb->cap;
      Event* src = &tb->buf[idx];
      if (!src->committed.load(std::memory_order_acquire)) continue; // skip in‑flight
      CleanEvent ce{};
      ce.ts_us=src->ts_us; ce.dur_us=src->dur_us; ce.flow_id=src->flow_id; ce.pid=src->pid; ce.tid=src->tid; ce.ph=src->ph;
      std::snprintf(ce.name,sizeof(ce.name),"%s",src->name);
      std::snprintf(ce.cat,sizeof(ce.cat),"%s",src->cat);
      std::snprintf(ce.cname,sizeof(ce.cname),"%s",src->cname);
      ce.argc = src->argc;
      for (uint8_t a=0;a<ce.argc && a<OTRACE_MAX_ARGS;a++){ ce.args[a]=src->args[a]; }
      out.push_back(ce);
    }
    // emit metadata for thread name/sort index once per flush (viewer is idempotent)
    if (tb->thread_name[0]) {
      CleanEvent m{}; m.ts_us = 0; m.pid = reg().pid_v; m.tid = tb->tid_v; m.ph = Phase::MThreadName; std::snprintf(m.name,sizeof(m.name),"%s", tb->thread_name); out.push_back(m);
    }
    if (tb->thread_sort_index != 0) {
      CleanEvent m{}; m.ts_us = 0; m.pid = reg().pid_v; m.tid = tb->tid_v; m.ph = Phase::MThreadSortIndex; m.argc=1; std::snprintf(m.args[0].key,sizeof(m.args[0].key),"sort_index"); m.args[0].kind=ArgKind::Number; m.args[0].num=(double)tb->thread_sort_index; out.push_back(m);
    }
  }
  // process name (once)
  if (reg().process_name[0]) {
    CleanEvent m{}; m.ts_us = 0; m.pid = reg().pid_v; m.tid = 0; m.ph = Phase::MProcessName; std::snprintf(m.name,sizeof(m.name),"%s", reg().process_name); out.push_back(m);
  }
}

inline void flush_file(const char* path) {
  // Pause new writes without blocking in‑flight ones
  bool prev = reg().enabled.exchange(false, std::memory_order_acq_rel);

  std::vector<CleanEvent> all; all.reserve(4096);
  collect_all(all);

  // Sort for coherent timeline
  std::sort(all.begin(), all.end(), [](const CleanEvent& a, const CleanEvent& b){
    if (a.ts_us == b.ts_us) { if (a.tid == b.tid) return (int)a.ph < (int)b.ph; return a.tid < b.tid; }
    return a.ts_us < b.ts_us;
  });

  const char* out_path = path ? path : reg().default_path;
  FILE* f = std::fopen(out_path, "wb");
  if (!f) { reg().enabled.store(prev, std::memory_order_release); return; }

  std::fputs("{\n\"traceEvents\":[\n", f);
  for (size_t i = 0; i < all.size(); ++i) {
    write_event_json_common(f, all[i]);  
    if (i + 1 != all.size()) std::fputs(",\n", f);
  }
std::fputs("\n],\n\"displayTimeUnit\":\"ms\"\n}\n", f);

  std::fclose(f);

  reg().enabled.store(prev, std::memory_order_release);
}

inline void atexit_flush() {
#if OTRACE_ON_EXIT
  flush_file(reg().default_path);
#endif
}

struct AtExitHook { AtExitHook(){ std::atexit(atexit_flush); } };
inline AtExitHook& hook() { static AtExitHook H; return H; }

} // namespace otrace

// ---- Public API macros ----------------------------------------------------

// ---- Core API macros (long names: OTRACE_*) --------------------------------
// Init hook
#define OTRACE_TOUCH() (void)otrace::hook()

// Control
#define OTRACE_ENABLE()          do{ OTRACE_TOUCH(); otrace::reg().enabled.store(true,  std::memory_order_release); }while(0)
#define OTRACE_DISABLE()         do{ OTRACE_TOUCH(); otrace::reg().enabled.store(false, std::memory_order_release); }while(0)
#define OTRACE_IS_ENABLED()      (otrace::reg().enabled.load(std::memory_order_relaxed))

// Colors
#define OTRACE_COLOR(cname)      do{ OTRACE_TOUCH(); otrace::set_next_color((cname)); }while(0)

// RAII scopes
#define OTRACE_SCOPE(name) \
  ::otrace::Scope _otrace_scope_##__LINE__( \
    ([&](){ (void)::otrace::hook(); return (name); }()) )

#define OTRACE_SCOPE_C(name, cat) \
  ::otrace::Scope _otrace_scope_##__LINE__( \
    ([&](){ (void)::otrace::hook(); return (name); }()), (cat) )

#define OTRACE_SCOPE_KV(name, key, val) \
  ::otrace::Scope _otrace_scope_##__LINE__( \
    ([&](){ (void)::otrace::hook(); return (name); }()), nullptr, (key), (double)(val) )

#define OTRACE_SCOPE_CKV(name, cat, key, val) \
  ::otrace::Scope _otrace_scope_##__LINE__( \
    ([&](){ (void)::otrace::hook(); return (name); }()), (cat), (key), (double)(val) )

#define OTRACE_ZONE(name)            OTRACE_SCOPE_C((name), "zone")

// Begin/End
#define OTRACE_BEGIN(name)           do{ OTRACE_TOUCH(); otrace::emit_begin((name), nullptr); }while(0)
#define OTRACE_BEGIN_C(name, cat)    do{ OTRACE_TOUCH(); otrace::emit_begin((name), (cat)); }while(0)
#define OTRACE_END(name)             do{ OTRACE_TOUCH(); otrace::emit_end((name), nullptr); }while(0)
#define OTRACE_END_C(name, cat)      do{ OTRACE_TOUCH(); otrace::emit_end((name), (cat)); }while(0)

// Instants
#define OTRACE_INSTANT(name)         do{ OTRACE_TOUCH(); otrace::emit_instant((name), nullptr); }while(0)
#define OTRACE_INSTANT_C(name, cat)  do{ OTRACE_TOUCH(); otrace::emit_instant((name), (cat)); }while(0)
#define OTRACE_INSTANT_KV(name, key, val) do{ OTRACE_TOUCH(); otrace::emit_instant_kv((name), (key), (double)(val), nullptr); }while(0)
#define OTRACE_INSTANT_CKV(name, cat, key, val) do{ OTRACE_TOUCH(); otrace::emit_instant_kv((name), (key), (double)(val), (cat)); }while(0)

// Frames
#define OTRACE_MARK_FRAME(idx)       do{ OTRACE_TOUCH(); otrace::emit_instant_kv("frame", "frame", (double)(idx), "frame"); }while(0)
#define OTRACE_MARK_FRAME_S(label)   do{ OTRACE_TOUCH(); otrace::Event* _e = otrace::get_tbuf()->append(); otrace::fill_common(*_e, otrace::Phase::I, "frame", "frame"); otrace::arg_string(*_e, "label", (label)); otrace::commit(_e); }while(0)

// Counters
#define OTRACE_COUNTER(name, value)        do{ OTRACE_TOUCH(); const char* _k[] = { (name) }; double _v[] = { (double)(value) }; otrace::emit_counter_n((name), nullptr, 1, _k, _v); }while(0)
#define OTRACE_COUNTER_C(name, cat, value) do{ OTRACE_TOUCH(); const char* _k[] = { (name) }; double _v[] = { (double)(value) }; otrace::emit_counter_n((name), (cat), 1, _k, _v); }while(0)
#define OTRACE_COUNTER2(name, k1,v1, k2,v2) do{ OTRACE_TOUCH(); const char* _k[]={ (k1),(k2) }; double _v[]={ (double)(v1),(double)(v2) }; otrace::emit_counter_n((name), nullptr, 2, _k, _v); }while(0)
#define OTRACE_COUNTER3(name, k1,v1, k2,v2, k3,v3) do{ OTRACE_TOUCH(); const char* _k[]={ (k1),(k2),(k3) }; double _v[]={ (double)(v1),(double)(v2),(double)(v3) }; otrace::emit_counter_n((name), nullptr, 3, _k, _v); }while(0)

// Metadata
#define OTRACE_SET_THREAD_NAME(name)     do{ OTRACE_TOUCH(); std::snprintf(otrace::get_tbuf()->thread_name, sizeof(otrace::get_tbuf()->thread_name), "%s", (name)?(name):""); }while(0)
#define OTRACE_SET_THREAD_SORT_INDEX(i)  do{ OTRACE_TOUCH(); otrace::get_tbuf()->thread_sort_index = (int)(i); }while(0)
#define OTRACE_SET_PROCESS_NAME(name)    do{ OTRACE_TOUCH(); std::snprintf(otrace::reg().process_name, sizeof(otrace::reg().process_name), "%s", (name)?(name):""); }while(0)

// Flows
#define OTRACE_FLOW_BEGIN(id)     do{ OTRACE_TOUCH(); otrace::emit_flow(otrace::Phase::FlowStart, (uint64_t)(id), nullptr, nullptr); }while(0)
#define OTRACE_FLOW_STEP(id)      do{ OTRACE_TOUCH(); otrace::emit_flow(otrace::Phase::FlowStep,  (uint64_t)(id), nullptr, nullptr); }while(0)
#define OTRACE_FLOW_END(id)       do{ OTRACE_TOUCH(); otrace::emit_flow(otrace::Phase::FlowEnd,   (uint64_t)(id), nullptr, nullptr); }while(0)

// Output
#define OTRACE_FLUSH(path)           do{ OTRACE_TOUCH(); otrace::flush_file((path)); }while(0)
#define OTRACE_SET_OUTPUT_PATH(path) do{ OTRACE_TOUCH(); otrace::set_output_path((path)); }while(0)


// Call-by-name single macro: OTRACE_CALL(SCOPE, "name"), OTRACE_CALL(COUNTER, "n", v), etc.
#define OTRACE_CALL(name, ...) OTRACE_##name(__VA_ARGS__)
#if !OTRACE_NO_SHORT_MACROS
  #define TRACE(name, ...) OTRACE_##name(__VA_ARGS__)
#endif


// ---- Optional short aliases (default ON unless OTRACE_NO_SHORT_MACROS) ----
#if !OTRACE_NO_SHORT_MACROS
  #define TRACE_ENABLE(...)                  OTRACE_ENABLE(__VA_ARGS__)
  #define TRACE_DISABLE(...)                 OTRACE_DISABLE(__VA_ARGS__)
  #define TRACE_IS_ENABLED(...)              OTRACE_IS_ENABLED(__VA_ARGS__)
  #define TRACE_COLOR(...)                   OTRACE_COLOR(__VA_ARGS__)

  #define TRACE_SCOPE(...)                   OTRACE_SCOPE(__VA_ARGS__)
  #define TRACE_SCOPE_C(...)                 OTRACE_SCOPE_C(__VA_ARGS__)
  #define TRACE_SCOPE_KV(...)                OTRACE_SCOPE_KV(__VA_ARGS__)
  #define TRACE_SCOPE_CKV(...)               OTRACE_SCOPE_CKV(__VA_ARGS__)
  #define TRACE_ZONE(...)                    OTRACE_ZONE(__VA_ARGS__)

  #define TRACE_BEGIN(...)                   OTRACE_BEGIN(__VA_ARGS__)
  #define TRACE_BEGIN_C(...)                 OTRACE_BEGIN_C(__VA_ARGS__)
  #define TRACE_END(...)                     OTRACE_END(__VA_ARGS__)
  #define TRACE_END_C(...)                   OTRACE_END_C(__VA_ARGS__)

  #define TRACE_INSTANT(...)                 OTRACE_INSTANT(__VA_ARGS__)
  #define TRACE_INSTANT_C(...)               OTRACE_INSTANT_C(__VA_ARGS__)
  #define TRACE_INSTANT_KV(...)              OTRACE_INSTANT_KV(__VA_ARGS__)
  #define TRACE_INSTANT_CKV(...)             OTRACE_INSTANT_CKV(__VA_ARGS__)

  #define TRACE_MARK_FRAME(...)              OTRACE_MARK_FRAME(__VA_ARGS__)
  #define TRACE_MARK_FRAME_S(...)            OTRACE_MARK_FRAME_S(__VA_ARGS__)

  #define TRACE_COUNTER(...)                 OTRACE_COUNTER(__VA_ARGS__)
  #define TRACE_COUNTER_C(...)               OTRACE_COUNTER_C(__VA_ARGS__)
  #define TRACE_COUNTER2(...)                OTRACE_COUNTER2(__VA_ARGS__)
  #define TRACE_COUNTER3(...)                OTRACE_COUNTER3(__VA_ARGS__)

  #define TRACE_SET_THREAD_NAME(...)         OTRACE_SET_THREAD_NAME(__VA_ARGS__)
  #define TRACE_SET_THREAD_SORT_INDEX(...)   OTRACE_SET_THREAD_SORT_INDEX(__VA_ARGS__)
  #define TRACE_SET_PROCESS_NAME(...)        OTRACE_SET_PROCESS_NAME(__VA_ARGS__)

  #define TRACE_FLOW_BEGIN(...)              OTRACE_FLOW_BEGIN(__VA_ARGS__)
  #define TRACE_FLOW_STEP(...)               OTRACE_FLOW_STEP(__VA_ARGS__)
  #define TRACE_FLOW_END(...)                OTRACE_FLOW_END(__VA_ARGS__)

  #define TRACE_FLUSH(...)                   OTRACE_FLUSH(__VA_ARGS__)
  #define TRACE_SET_OUTPUT_PATH(...)         OTRACE_SET_OUTPUT_PATH(__VA_ARGS__)
#endif

#else  // OTRACE disabled -----------------------------------------------------

#define OTRACE_ENABLE(...)                        ((void)0)
#define OTRACE_DISABLE(...)                       ((void)0)
#define OTRACE_IS_ENABLED(...)                    (false)
#define OTRACE_COLOR(...)                         ((void)0)

#define OTRACE_SCOPE(...)                         ((void)0)
#define OTRACE_SCOPE_C(...)                       ((void)0)
#define OTRACE_SCOPE_KV(...)                      ((void)0)
#define OTRACE_SCOPE_CKV(...)                     ((void)0)
#define OTRACE_ZONE(...)                          ((void)0)

#define OTRACE_BEGIN(...)                         ((void)0)
#define OTRACE_BEGIN_C(...)                       ((void)0)
#define OTRACE_END(...)                           ((void)0)
#define OTRACE_END_C(...)                         ((void)0)

#define OTRACE_INSTANT(...)                       ((void)0)
#define OTRACE_INSTANT_C(...)                     ((void)0)
#define OTRACE_INSTANT_KV(...)                    ((void)0)
#define OTRACE_INSTANT_CKV(...)                   ((void)0)

#define OTRACE_MARK_FRAME(...)                    ((void)0)
#define OTRACE_MARK_FRAME_S(...)                  ((void)0)

#define OTRACE_COUNTER(...)                       ((void)0)
#define OTRACE_COUNTER_C(...)                     ((void)0)
#define OTRACE_COUNTER2(...)                      ((void)0)
#define OTRACE_COUNTER3(...)                      ((void)0)

#define OTRACE_SET_THREAD_NAME(...)               ((void)0)
#define OTRACE_SET_THREAD_SORT_INDEX(...)         ((void)0)
#define OTRACE_SET_PROCESS_NAME(...)              ((void)0)

#define OTRACE_FLOW_BEGIN(...)                    ((void)0)
#define OTRACE_FLOW_STEP(...)                     ((void)0)
#define OTRACE_FLOW_END(...)                      ((void)0)

#define OTRACE_FLUSH(...)                         ((void)0)
#define OTRACE_SET_OUTPUT_PATH(...)               ((void)0)

// Keep call-by-name macros so code compiles as no-ops when disabled
#define OTRACE_CALL(name, ...) OTRACE_##name(__VA_ARGS__)
#if !OTRACE_NO_SHORT_MACROS
  #define TRACE(name, ...) OTRACE_##name(__VA_ARGS__)
#endif

#if !OTRACE_NO_SHORT_MACROS
  #define TRACE_ENABLE(...)                      OTRACE_ENABLE(__VA_ARGS__)
  #define TRACE_DISABLE(...)                     OTRACE_DISABLE(__VA_ARGS__)
  #define TRACE_IS_ENABLED(...)                  OTRACE_IS_ENABLED(__VA_ARGS__)
  #define TRACE_COLOR(...)                       OTRACE_COLOR(__VA_ARGS__)
  #define TRACE_SCOPE(...)                       OTRACE_SCOPE(__VA_ARGS__)
  #define TRACE_SCOPE_C(...)                     OTRACE_SCOPE_C(__VA_ARGS__)
  #define TRACE_SCOPE_KV(...)                    OTRACE_SCOPE_KV(__VA_ARGS__)
  #define TRACE_SCOPE_CKV(...)                   OTRACE_SCOPE_CKV(__VA_ARGS__)
  #define TRACE_ZONE(...)                        OTRACE_ZONE(__VA_ARGS__)
  #define TRACE_BEGIN(...)                       OTRACE_BEGIN(__VA_ARGS__)
  #define TRACE_BEGIN_C(...)                     OTRACE_BEGIN_C(__VA_ARGS__)
  #define TRACE_END(...)                         OTRACE_END(__VA_ARGS__)
  #define TRACE_END_C(...)                       OTRACE_END_C(__VA_ARGS__)
  #define TRACE_INSTANT(...)                     OTRACE_INSTANT(__VA_ARGS__)
  #define TRACE_INSTANT_C(...)                   OTRACE_INSTANT_C(__VA_ARGS__)
  #define TRACE_INSTANT_KV(...)                  OTRACE_INSTANT_KV(__VA_ARGS__)
  #define TRACE_INSTANT_CKV(...)                 OTRACE_INSTANT_CKV(__VA_ARGS__)
  #define TRACE_MARK_FRAME(...)                  OTRACE_MARK_FRAME(__VA_ARGS__)
  #define TRACE_MARK_FRAME_S(...)                OTRACE_MARK_FRAME_S(__VA_ARGS__)
  #define TRACE_COUNTER(...)                     OTRACE_COUNTER(__VA_ARGS__)
  #define TRACE_COUNTER_C(...)                   OTRACE_COUNTER_C(__VA_ARGS__)
  #define TRACE_COUNTER2(...)                    OTRACE_COUNTER2(__VA_ARGS__)
  #define TRACE_COUNTER3(...)                    OTRACE_COUNTER3(__VA_ARGS__)
  #define TRACE_SET_THREAD_NAME(...)             OTRACE_SET_THREAD_NAME(__VA_ARGS__)
  #define TRACE_SET_THREAD_SORT_INDEX(...)       OTRACE_SET_THREAD_SORT_INDEX(__VA_ARGS__)
  #define TRACE_SET_PROCESS_NAME(...)            OTRACE_SET_PROCESS_NAME(__VA_ARGS__)
  #define TRACE_FLOW_BEGIN(...)                  OTRACE_FLOW_BEGIN(__VA_ARGS__)
  #define TRACE_FLOW_STEP(...)                   OTRACE_FLOW_STEP(__VA_ARGS__)
  #define TRACE_FLOW_END(...)                    OTRACE_FLOW_END(__VA_ARGS__)
  #define TRACE_FLUSH(...)                       OTRACE_FLUSH(__VA_ARGS__)
  #define TRACE_SET_OUTPUT_PATH(...)             OTRACE_SET_OUTPUT_PATH(__VA_ARGS__)
#endif

#endif // OTRACE
#endif // OTRACE_HPP_INCLUDED
