/* SPDX-License-Identifier: MIT
 * Copyright (c) 2025 DimitryQm
 * This file is part of otrace (v0.2.0). The full MIT license text is in the project’s LICENSE file:
 * https://github.com/DimitryQm/otrace/blob/main/LICENSE
 */


#ifndef OTRACE_HPP_INCLUDED
#define OTRACE_HPP_INCLUDED
/*
 * otrace.hpp — header-only timeline tracer
 * version: 0.2.0
 *
 * About:
 *   Annotate C++ with scopes, instants, counters, flows, and frames, then open the
 *   resulting Chrome Trace JSON in Perfetto or chrome://tracing. You control exactly
 *   what’s recorded; nothing is sampled implicitly. Per-thread lock-free rings buffer
 *   events; a synchronous flush writes a compact .json (or .json.gz).
 *
 * Build flags (define at compile time):
 *   -DOTRACE=1                         Enable tracing (default 0 = disabled)
 *   -DOTRACE_THREAD_BUFFER_EVENTS=N    Events per thread (default 1<<15)
 *   -DOTRACE_DEFAULT_PATH="file.json"  Output file (default "trace.json")
 *   -DOTRACE_ON_EXIT=1                 Auto-flush at process exit (default 1)
 *   -DOTRACE_CLOCK=1|2|3               1=steady_clock, 2=RDTSC(x86), 3=system_clock
 *   -DOTRACE_MAX_ARGS=N                Max key/values per event (default 4)
 *   -DOTRACE_NO_SHORT_MACROS=1         Hide TRACE_* aliases; use OTRACE_* only (default 0)
 *   -DOTRACE_USE_ZLIB=1                Enable gzip for *.json.gz (zlib present)
 *   -DOTRACE_USE_MINIZ=1               Enable gzip via bundled miniz (if you ship it)
 *   -DOTRACE_SYNTHESIZE_TRACKS=1       Enable synthetic tracks at flush (default 0)
 *   -DOTRACE_SYNTH_RATE_WINDOW_US=...  Rolling window for FPS/rates (default 500000)
 *   -DOTRACE_SYNTH_PCT_NAMES="..."     Percentiles for latency summary (default "p50,p95,p99")
 *
 *   // Heap tracer (optional, header-only; off by default)
 *   -DOTRACE_HEAP=1                    Enable heap tracing layer
 *   -DOTRACE_DEFINE_HEAP_HOOKS=1       Define global new/delete wrappers (ONE TU only)
 *   -DOTRACE_HEAP_SAMPLE=0.10          Initial callsite sampling probability (default 0.0)
 *   -DOTRACE_HEAP_STACKS=1             Capture short stacks for sampled sites (default 0)
 *   -DOTRACE_HEAP_STACK_DEPTH=8        Max frames per captured stack (default 8)
 *   -DOTRACE_HEAP_SHARDS=64            Shard count for live allocation map (default 64)
 *   -DOTRACE_HEAP_DEMANGLE=1           Demangle C++ symbols in reports if available (default 0)
 *   -DOTRACE_HEAP_DBGHELP=1            Use DbgHelp on Windows when present (default 0)
 *
 * Environment variables (read once on first use):
 *   OTRACE_DISABLE=1                   Disable recording
 *   OTRACE_ENABLE=1                    Enable recording (wins over DISABLE)
 *   OTRACE_SAMPLE=0.10                 Keep probability for sampling (0..1)
 *
 * Public API (when OTRACE==1) — examples:
 *   // Bring the recorder up / query state
 *   OTRACE_TOUCH();                                   // force lazy init (reads env)
 *   bool on = TRACE_IS_ENABLED();                     // fast “is enabled?” check
 *
 *   // Scopes & events
 *   TRACE_SCOPE("step");                              // or OTRACE_SCOPE(...)
 *   TRACE_BEGIN("upload"); TRACE_END("upload");
 *
 *   // Instants with key/values (0.2.0+):
 *   // • Values: numbers or strings
 *   // • Multiple KVs per call (variadic), capped by OTRACE_MAX_ARGS
 *   TRACE_INSTANT_KV ("speed", "mps", 12.5);
 *   TRACE_INSTANT_KV ("note",  "text", "hello world");
 *   TRACE_INSTANT_CKV("tick",  "frame", "phase", 42, "stage", "copy");
 *
 *   // Counters, flows, frames
 *   TRACE_COUNTER("queue_len", n);
 *   TRACE_FLOW_BEGIN(id); TRACE_FLOW_STEP(id); TRACE_FLOW_END(id);
 *   TRACE_MARK_FRAME(i); TRACE_MARK_FRAME_S("present");
 *
 *   // Metadata & colors
 *   TRACE_SET_THREAD_NAME("worker-0"); TRACE_SET_PROCESS_NAME("my-app");
 *   TRACE_SET_THREAD_SORT_INDEX(10);
 *   TRACE_COLOR("good");                                 // affects next event only
 *
 *   // Output control: single file or rotation (+optional gzip)
 *   TRACE_SET_OUTPUT_PATH("trace.json");
 *   TRACE_SET_OUTPUT_PATTERN("traces/run-%04u.json.gz", 64 MB, 10); // max_size_mb, max_files
 *   TRACE_FLUSH(nullptr);
 *
 *   // Filters & sampling (runtime gates)
 *   OTRACE_SET_FILTER(+[](const char* name, const char* cat){ return cat && std::strcmp(cat,"io")==0; });
 *   OTRACE_ENABLE_CATS("io,frame");                  // allowlist categories
 *   OTRACE_DISABLE_CATS("debug,noise");              // denylist categories
 *   OTRACE_SET_SAMPLING(0.1);                        // keep 10% of events
 *
 *   // Call-by-name macro (optional sugar)
 *   OTRACE_CALL(SCOPE, "init");                      // expands to OTRACE_SCOPE("init")
 *   OTRACE_CALL(COUNTER, "queue_len", v);            // expands to OTRACE_COUNTER(...)
 *   // or with aliases (enabled by default):
 *   TRACE(COUNTER, "queue_len", v);
 *
 *   // Flush-time synthesis (if compiled in)
 *   OTRACE_ENABLE_SYNTH_TRACKS(true);                // runtime toggle for synthetic tracks
 *
 *   // Heap tracer controls & report (if compiled with OTRACE_HEAP)
 *   OTRACE_HEAP_ENABLE(true);                        // arm/disarm heap capture at runtime
 *   OTRACE_HEAP_SET_SAMPLING(0.2);                   // adjust callsite sampling (0..1)
 *   OTRACE_HEAP_REPORT();                            // emit heap_report_stats/leaks/sites
 *
 * Notes:
 *   • If the rotation pattern ends with ".gz", gzip is used only when built with
 *     OTRACE_USE_ZLIB=1 or OTRACE_USE_MINIZ=1; otherwise a plain JSON file is written.
 *   • TRACE_* aliases are enabled by default; define OTRACE_NO_SHORT_MACROS=1 to hide them.
 *   • Env vars are read once on first touch in-process (see OTRACE_TOUCH()).
 *   • Each key/value added to an event counts toward OTRACE_MAX_ARGS for that event.
 *   • Define OTRACE_DEFINE_HEAP_HOOKS in exactly one translation unit when using the heap hooks.
 *
 * Requirements: C++17+, Windows/Linux/macOS. Not async-signal-safe.
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

#ifndef OTRACE_USE_ZLIB
#define OTRACE_USE_ZLIB 0   // set to 1 to use system zlib for gzip (.gz)
#endif
#ifndef OTRACE_USE_MINIZ
#define OTRACE_USE_MINIZ 0  // set to 1 to use bundled miniz (zlib-compat) for gzip
#endif

#ifndef OTRACE_SYNTHESIZE_TRACKS
#define OTRACE_SYNTHESIZE_TRACKS 0
#endif
#ifndef OTRACE_SYNTH_RATE_WINDOW_US
#define OTRACE_SYNTH_RATE_WINDOW_US 500000  // 0.5s
#endif
#ifndef OTRACE_SYNTH_PCT_NAMES
#define OTRACE_SYNTH_PCT_NAMES "p50,p95,p99"
#endif

#ifndef OTRACE_HEAP
#define OTRACE_HEAP 0
#endif

#ifndef OTRACE_DEFINE_HEAP_HOOKS
#define OTRACE_DEFINE_HEAP_HOOKS 0
#endif

#ifndef OTRACE_HEAP_SAMPLE
#define OTRACE_HEAP_SAMPLE 0.0
#endif

#ifndef OTRACE_HEAP_STACK_DEPTH
#define OTRACE_HEAP_STACK_DEPTH 8
#endif

#ifndef OTRACE_HEAP_SHARDS
#define OTRACE_HEAP_SHARDS 64
#endif

#ifndef OTRACE_HEAP_STACKS
#define OTRACE_HEAP_STACKS 0
#endif
#ifndef OTRACE_HEAP_DEMANGLE
#define OTRACE_HEAP_DEMANGLE 0
#endif
#ifndef OTRACE_HEAP_DBGHELP
#define OTRACE_HEAP_DBGHELP 0
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
#include <new>
#include <vector>
#include <algorithm>
#include <string>
#include <utility>
#include <type_traits>
#include <map>
#include <cmath>
#include <unordered_map>
#include <mutex>



#if __cplusplus >= 201703L
  #include <string_view>
#endif

#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <processthreadsapi.h>
  #include <sys/stat.h>
  #include <direct.h>
#elif defined(__APPLE__)
  #include <pthread.h>
  #include <sys/types.h>
  #include <unistd.h>
  #include <sys/stat.h>
#else
  #include <sys/syscall.h>
  #include <sys/types.h>
  #include <unistd.h>
  #include <sys/stat.h>  
#endif

#include <cerrno>
#if OTRACE_CLOCK==2 && (defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64))
  #if defined(_MSC_VER)
    #include <intrin.h>
    #include <immintrin.h>
  #else
    #include <x86intrin.h>
    #include <immintrin.h>
  #endif
#endif

#if OTRACE_USE_ZLIB
  #include <zlib.h>
#elif OTRACE_USE_MINIZ
  #include "miniz.h"
#endif

#ifndef __has_include
  #define __has_include(x) 0
#endif

// ================= Optional: stack capture (heap tracer) =============
// Only include when BOTH heap tracer and stack capture are enabled.
#if OTRACE_HEAP && OTRACE_HEAP_STACKS
  #if defined(_WIN32)
    // DbgHelp is optional; do NOT force-link from a header.
    #if OTRACE_HEAP_DBGHELP && __has_include(<dbghelp.h>)
      #include <dbghelp.h>
      #define OTRACE_HAVE_DBGHELP 1
    #else
      #define OTRACE_HAVE_DBGHELP 0
    #endif
  #else
    #if __has_include(<execinfo.h>)
      #include <execinfo.h>        // backtrace(), backtrace_symbols()
      #define OTRACE_HAVE_EXECINFO 1
    #else
      #define OTRACE_HAVE_EXECINFO 0
    #endif
  #endif
#else
  #define OTRACE_HAVE_DBGHELP   0
  #define OTRACE_HAVE_EXECINFO  0
#endif

// ================= Optional: demangling (heap tracer) ================
#if OTRACE_HEAP && OTRACE_HEAP_DEMANGLE && __has_include(<cxxabi.h>)
  #include <cxxabi.h>            // abi::__cxa_demangle
  #define OTRACE_HAVE_CXXABI 1
#else
  #define OTRACE_HAVE_CXXABI 0
#endif



namespace otrace {

// ---- Platform helpers -----------------------------------------------------
#if OTRACE_CLOCK==2 && (defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64))
  static inline uint64_t rdtsc() noexcept { return __rdtsc(); }
#endif
inline thread_local bool tls_in_tracer = false;
struct TracerGuard { bool active=false; TracerGuard(){ if(!tls_in_tracer){ tls_in_tracer=true; active=true; } } ~TracerGuard(){ if(active) tls_in_tracer=false; } };
    
inline bool csv_has(const char* csv, const char* key);                   // forward
inline bool should_emit(const char* name, const char* cat);              // forward
struct AtExitHook;                   // forward
inline AtExitHook& hook();           // forward
inline uint64_t now_us();  // forward so heap code can call it
// 1-pair overload forward decl (works for generate_report)
inline void emit_instant_kvs(const char* name, const char* cat,
                             const char* key, const char* value);
// keeping a correct forward decl of the template too, but it won’t be needed by generate_report:
template <typename... KVs>
inline void emit_instant_kvs(const char* name, const char* cat, KVs&&... kvs);

inline void emit_counter_n(const char* name, const char* cat, int n,
                           const char** keys, const double* vals);

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



// Create parent directories for a target path.
inline void mkpath(const char* path) {
  if (!path || !*path) return;

#if defined(_WIN32)
  // Normalize slashes and skip drive/UNC roots.
  char tmp[1024];
  std::snprintf(tmp, sizeof(tmp), "%s", path);
  for (char* q = tmp; *q; ++q) if (*q == '/') *q = '\\';

  char* p = tmp;
  // "C:\"
  if (((p[0]|32) >= 'a' && (p[0]|32) <= 'z') && p[1]==':' && p[2]=='\\') {
    p += 3;
  }
  // "\\server\share\"
  else if (p[0]=='\\' && p[1]=='\\') {
    p += 2; while (*p && *p!='\\') ++p; if (*p=='\\') ++p; // server
    while (*p && *p!='\\') ++p; if (*p=='\\') ++p;         // share
  }

  for (; *p; ++p) {
    if (*p == '\\') { char c = *p; *p = 0; _mkdir(tmp); *p = c; }
  }
#else
  // POSIX: progressively mkdir() on each '/' boundary.
  char tmp[1024];
  std::snprintf(tmp, sizeof(tmp), "%s", path);
  for (char* p = tmp + 1; *p; ++p) {
    if (*p == '/') {
      char c = *p; *p = 0;
      if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        // ignore; best-effort (race/perms/etc.)
      }
      *p = c;
    }
  }
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
    

#if OTRACE_HEAP

namespace heap {

// Allocation entry
struct AllocEntry {
    size_t size;
    uint64_t stack_hash;
    uint64_t timestamp;
};

// Callsite statistics
struct CallsiteStats {
    uint64_t total_bytes;
    uint64_t alloc_count;
    uint64_t live_bytes;
    uint64_t live_count;
    std::string sample_stack;
};

// Shard for lock reduction
struct Shard {
    std::unordered_map<void*, AllocEntry> allocations;
    std::mutex mutex;
};

// Global state
struct State {
    std::atomic<uint64_t> live_bytes{0};
    std::atomic<uint64_t> total_allocations{0};
    std::atomic<uint64_t> total_frees{0};
    std::atomic<bool> enabled{false};
    std::atomic<double> sample_rate{OTRACE_HEAP_SAMPLE};
    
    Shard shards[OTRACE_HEAP_SHARDS];
    std::unordered_map<uint64_t, CallsiteStats> callsites;
    std::mutex callsites_mutex;
    
    std::atomic<uint64_t> last_counter_update{0};
    uint64_t counter_update_interval{1000000}; // 1 second in microseconds
};
    

inline State& state() {
    static State s;
    return s;
}

// Thread-local reentrancy guard for heap hooks
inline thread_local bool tls_in_heap_hook = false;

struct HeapHookGuard {
  bool active = false;
  HeapHookGuard() {
    if (!tls_in_heap_hook) { tls_in_heap_hook = true; active = true; }
  }
  ~HeapHookGuard() {
    if (active) tls_in_heap_hook = false;
  }
};

inline uint64_t now_us() { return Timebase::now_us(); }
// Hash a stack trace
inline uint64_t hash_stack(void** stack, int depth) {
    uint64_t hash = 0;
    for (int i = 0; i < depth; ++i) {
        hash ^= (uint64_t)stack[i];
        hash = (hash << 13) | (hash >> 51);
        hash *= 0x9E3779B97F4A7C15;
    }
    return hash;
}

inline int capture_stack(void** buffer, int max_depth) {
#if OTRACE_HAVE_EXECINFO
  return backtrace(buffer, max_depth);
#elif defined(_WIN32) && OTRACE_HAVE_DBGHELP
  return (int)CaptureStackBackTrace(0, (ULONG)max_depth, buffer, nullptr);
#else
  return 0;
#endif
}


// Demangle symbol name
inline std::string demangle(const char* name) {
#if OTRACE_HAVE_CXXABI
  int status = 0;
  char* demangled = abi::__cxa_demangle(name, nullptr, nullptr, &status);
  if (demangled) { std::string s(demangled); std::free(demangled); return s; }
#endif
  return name ? name : "unknown";
}

// Format stack frame
inline std::string format_frame(const char* symbol) {
    std::string frame(symbol);
    // Extract function name from symbol
    size_t start = frame.find('(');
    size_t end = frame.find('+', start);
    if (start != std::string::npos && end != std::string::npos) {
        std::string func = frame.substr(start + 1, end - start - 1);
        return demangle(func.c_str());
    }
    return frame;
}

// Format stack trace
inline std::string format_stack(void** stack, int depth) {
    std::string result;
#if defined(__linux__) || defined(__APPLE__)
    char** symbols = backtrace_symbols(stack, depth);
    if (symbols) {
        for (int i = 2; i < depth && symbols[i]; ++i) { // Skip first two frames (heap functions)
            if (i > 2) result += " <- ";
            result += format_frame(symbols[i]);
        }
        free(symbols);
    }
#endif
    return result;
}

// Get shard for a pointer
inline Shard& get_shard(void* ptr) {
    uintptr_t p = reinterpret_cast<uintptr_t>(ptr);
    return state().shards[p % OTRACE_HEAP_SHARDS];
}

// Record allocation
inline void record_alloc(void* ptr, size_t size) {
    if (!ptr) return;
    if (otrace::tls_in_tracer) return;
    HeapHookGuard guard;
    if (!guard.active) return;  // already inside the hook: skip
    if (!state().enabled.load(std::memory_order_relaxed)) return;
    
    // Update global stats
    state().live_bytes.fetch_add(size, std::memory_order_relaxed);
    state().total_allocations.fetch_add(1, std::memory_order_relaxed);
    
    // Sample stack if needed
    uint64_t stack_hash = 0;
    std::string stack_str;
    double rate = state().sample_rate.load(std::memory_order_relaxed);
    
    if (rate > 0.0) {
        // Tiny thread-local xorshift for sampling
        thread_local uint64_t s = (uint64_t)otrace::tid() * 0x9E3779B97F4A7C15ull + now_us();
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        double u = (double)((s >> 11) & ((1ull<<53)-1)) / (double)(1ull<<53);
        
        if (u < rate) {
            void* stack[OTRACE_HEAP_STACK_DEPTH];
            int depth = capture_stack(stack, OTRACE_HEAP_STACK_DEPTH);
            if (depth > 2) { // Skip heap functions
                stack_hash = hash_stack(stack + 2, depth - 2);
                stack_str = format_stack(stack + 2, depth - 2);
            }
        }
    }
    
    // Store allocation
    Shard& shard = get_shard(ptr);
    std::lock_guard<std::mutex> lock(shard.mutex);
    shard.allocations[ptr] = {size, stack_hash, now_us()};
    
    // Update callsite stats if we have a stack
    if (stack_hash != 0) {
        std::lock_guard<std::mutex> lock(state().callsites_mutex);
        CallsiteStats& stats = state().callsites[stack_hash];
        stats.total_bytes += size;
        stats.alloc_count += 1;
        stats.live_bytes += size;
        stats.live_count += 1;
        if (stats.sample_stack.empty() && !stack_str.empty()) {
            stats.sample_stack = stack_str;
        }
    }
    
    // Periodically update counter
uint64_t now = ::otrace::now_us();
uint64_t last = state().last_counter_update.load(std::memory_order_relaxed);
if (now - last >= state().counter_update_interval) {
  if (state().last_counter_update.compare_exchange_strong(last, now, std::memory_order_relaxed)) {
    const char* k[] = { "heap_live_bytes" };
    double v[] = { (double)state().live_bytes.load(std::memory_order_relaxed) };
    ::otrace::emit_counter_n("heap_live_bytes", nullptr, 1, k, v);
  }
}

        }

// Record free
inline void record_free(void* ptr) {
  if (!ptr) return;
  if (otrace::tls_in_tracer) return;   
  HeapHookGuard guard;
  if (!guard.active) return;  // already inside the hook: skip
  if (!state().enabled.load(std::memory_order_relaxed)) return;
    Shard& shard = get_shard(ptr);
    std::lock_guard<std::mutex> lock(shard.mutex);
    auto it = shard.allocations.find(ptr);
    if (it != shard.allocations.end()) {
        // Update global stats
        state().live_bytes.fetch_sub(it->second.size, std::memory_order_relaxed);
        state().total_frees.fetch_add(1, std::memory_order_relaxed);
        
        // Update callsite stats if we have a stack
        if (it->second.stack_hash != 0) {
            std::lock_guard<std::mutex> lock(state().callsites_mutex);
            auto cs_it = state().callsites.find(it->second.stack_hash);
            if (cs_it != state().callsites.end()) {
                cs_it->second.live_bytes -= it->second.size;
                cs_it->second.live_count -= 1;
            }
        }
        
        // Remove allocation
        shard.allocations.erase(it);
    }
}

// Generate heap report
inline void generate_report() {
  if (!state().enabled.load(std::memory_order_relaxed)) return;

  ::otrace::emit_instant_kvs("heap_report_started", "heap", "status", "begin");

  // 1) Snapshot live allocations
  std::vector<std::pair<void*, AllocEntry>> all_allocs;
  all_allocs.reserve(1024);
  for (int i = 0; i < OTRACE_HEAP_SHARDS; ++i) {
    std::lock_guard<std::mutex> lk(state().shards[i].mutex);
    all_allocs.insert(all_allocs.end(),
                      state().shards[i].allocations.begin(),
                      state().shards[i].allocations.end());
  }

  // 2) Group by callsite hash
  std::unordered_map<uint64_t, std::vector<std::pair<void*, AllocEntry>>> by_site;
  for (const auto& p : all_allocs) {
    by_site[p.second.stack_hash].push_back(p);
  }

  // 3) Sort sites by total bytes
  std::vector<std::pair<uint64_t, uint64_t>> leak_sizes;
  leak_sizes.reserve(by_site.size());
  for (const auto& kv : by_site) {
    uint64_t total = 0;
    for (const auto& alloc : kv.second) total += alloc.second.size;
    leak_sizes.emplace_back(kv.first, total);
  }
  std::sort(leak_sizes.begin(), leak_sizes.end(),
            [](const auto& a, const auto& b){ return a.second > b.second; });

  // 4) Emit summary stats (always)
  {
    const std::string live_cnt = std::to_string(all_allocs.size());
    const std::string sites_cnt = std::to_string(by_site.size());
    ::otrace::emit_instant_kvs("heap_report_stats","heap",
                               "live_alloc_count", live_cnt.c_str(),
                               "site_count",       sites_cnt.c_str());
  }

  // 5) Emit top leaks — with fallback text if we don't have a callsite entry
  {
    std::lock_guard<std::mutex> lk(state().callsites_mutex);
    const int N = std::min<int>(10, leak_sizes.size());
    if (N == 0) {
      ::otrace::emit_instant_kvs("heap_leaks","heap",
                                 "info","no_live_allocations_detected");
    } else {
      for (int i = 0; i < N; ++i) {
        const uint64_t hash = leak_sizes[i].first;
        const uint64_t size = leak_sizes[i].second;

        const auto it = state().callsites.find(hash);
        const bool have_cs = (it != state().callsites.end());
        const std::string& sample = have_cs ? it->second.sample_stack : std::string();

        std::string key   = "leak_" + std::to_string(i+1);
        std::string value;
        if (have_cs && !sample.empty()) {
          value = sample + " (" +
                  std::to_string(size) + " bytes, " +
                  std::to_string(by_site[hash].size()) + " allocations)";
        } else {
          // fallback when callsite info is missing
          char buf[128];
          std::snprintf(buf, sizeof(buf),
                        "hash=0x%016llx (%llu bytes, %zu allocations)",
                        (unsigned long long)hash,
                        (unsigned long long)size,
                        by_site[hash].size());
          value = buf;
        }
        ::otrace::emit_instant_kvs("heap_leaks","heap", key.c_str(), value.c_str());
      }
    }
  }

  // 6) Emit top allocation sites by total bytes (or a fallback)
  {
    std::vector<std::pair<uint64_t, CallsiteStats>> sites(
        state().callsites.begin(), state().callsites.end());
    std::sort(sites.begin(), sites.end(),
              [](const auto& a, const auto& b){
                return a.second.total_bytes > b.second.total_bytes;
              });

    const int N = std::min<int>(10, sites.size());
    if (N == 0) {
      ::otrace::emit_instant_kvs("heap_sites","heap",
                                 "info","no_callsite_info_available");
    } else {
      for (int i = 0; i < N; ++i) {
        std::string key   = "site_" + std::to_string(i+1);
        std::string value = sites[i].second.sample_stack + " (" +
                            std::to_string(sites[i].second.total_bytes) + " bytes, " +
                            std::to_string(sites[i].second.alloc_count) + " allocations)";
        ::otrace::emit_instant_kvs("heap_sites","heap", key.c_str(), value.c_str());
      }
    }
  }

  ::otrace::emit_instant_kvs("heap_report_done", "heap", "status", "end");
}


// Public API
inline void enable(bool on) {
    state().enabled.store(on, std::memory_order_release);
    if (on) {
        state().live_bytes = 0;
        state().total_allocations = 0;
        state().total_frees = 0;
        for (int i = 0; i < OTRACE_HEAP_SHARDS; ++i) {
            std::lock_guard<std::mutex> lock(state().shards[i].mutex);
            state().shards[i].allocations.clear();
        }
        std::lock_guard<std::mutex> lock(state().callsites_mutex);
        state().callsites.clear();
    }
}

inline void set_sampling(double rate) {
    if (rate < 0.0) rate = 0.0;
    if (rate > 1.0) rate = 1.0;
    state().sample_rate.store(rate, std::memory_order_release);
}

} // namespace heap

#endif // OTRACE_HEAP



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
  uint32_t seq;               // stable sequence per thread
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
  uint32_t      seq_ctr;
  uint64_t      total_appends;    
  char          thread_name[OTRACE_MAX_NAME];
  int           thread_sort_index;
  Event*        buf;
  uint32_t      cap;
  uint32_t      head;
  bool          wrapped;
  char          pending_cname[OTRACE_MAX_CNAME]; // color hint for next event only

  ThreadBuffer(uint32_t capacity)
  : next(nullptr), tid_v(otrace::tid()), thread_sort_index(0), buf(nullptr),
    cap(capacity), head(0), wrapped(false), seq_ctr(0), total_appends(0) {
    thread_name[0] = '\0';
    pending_cname[0] = '\0';
    buf = new Event[cap];
  }

  ~ThreadBuffer() { delete[] buf; }

Event* append() {
    otrace::TracerGuard _tg;  
    uint32_t idx = head++;
    total_appends++;
    if (head >= cap) { head = 0; wrapped = true; }
    Event* e = &buf[idx];
    // mark slot as in‑flight
    e->committed.store(0, std::memory_order_relaxed);
    // reset dynamic fields (cheap, skip large memsets)
    e->argc = 0; e->dur_us = 0; e->flow_id = 0; e->seq = ++seq_ctr;
    e->name[0]=0; e->cat[0]=0; 
    if (pending_cname[0]) { std::snprintf(e->cname, sizeof(e->cname), "%s", pending_cname); pending_cname[0]=0; }
    else e->cname[0]=0;
    return e;
  }
};

using OtraceFilter = bool(*)(const char* name, const char* cat);

// Global registry of all thread buffers
struct Registry {
  std::atomic<ThreadBuffer*> head { nullptr };
  std::atomic<bool> enabled { true };
  uint32_t pid_v { otrace::pid() };
  char process_name[OTRACE_MAX_NAME];
  char default_path[256];

  OtraceFilter filter = nullptr;
  double sample_keep = 1.0;               // 0..1
  char allow_cats[256];                   // CSV allowlist
  char deny_cats[256];                    // CSV denylist

  enum class FlushMode { PauseAppenders, Quiescent };
  std::atomic<FlushMode> flush_mode { FlushMode::PauseAppenders };

  // snapshots
  std::atomic<uint32_t> snapshot_ms { 0 };
  std::atomic<bool>     snapshot_stop { false };
  std::thread           snapshot_thr;

  // rotation/pattern (lightweight; gzip optional)
  char pattern[256];                      // e.g. "traces/run-%Y%m%d-%H%M%S.json"
  uint32_t max_files = 0;
  uint32_t max_size_mb = 0;
  // rotation state
  uint32_t rot_index = 0;
  bool     pattern_has_index = false; // true if pattern contains a %d
  bool     pattern_use_gzip  = false; // true if pattern ends with .gz and gzip is available

  // synthesis (post-process at flush)
  std::atomic<bool> synth_enabled { OTRACE_SYNTHESIZE_TRACKS != 0 };

  struct SynthCfg {
    uint64_t rate_window_us;
    uint32_t pct_count;
    double   pct_vals[8];          // 0..1 (e.g. 0.50, 0.95, 0.99)
    char     pct_names[8][8];      // labels (e.g. "p50")
  } synth;


  Registry() {
    process_name[0] = '\0';
    std::snprintf(default_path, sizeof(default_path), "%s", OTRACE_DEFAULT_PATH);
    allow_cats[0]=deny_cats[0]=pattern[0]='\0';
    // synth defaults
    synth.rate_window_us = (uint64_t)OTRACE_SYNTH_RATE_WINDOW_US;
    synth.pct_count = 0;
    // parse OTRACE_SYNTH_PCT_NAMES at startup
    {
      const char* csv = OTRACE_SYNTH_PCT_NAMES;
      while (*csv && synth.pct_count < 8) {
        while (*csv==',' || *csv==' ' || *csv=='\t') ++csv;
        const char* s = csv;
        while (*csv && *csv!=',') ++csv;
        size_t n = (size_t)(csv - s);
        if (n > 0 && n < sizeof(synth.pct_names[0])) {
          std::snprintf(synth.pct_names[synth.pct_count], sizeof(synth.pct_names[0]), "%.*s", (int)n, s);
          // accept formats: "p50" or "50" -> 0.50
          const char* p = synth.pct_names[synth.pct_count];
          if (p[0]=='p' || p[0]=='P') ++p;
          double v = std::atof(p) / 100.0;
          if (v > 0.0 && v < 1.0) {
            synth.pct_vals[synth.pct_count++] = v;
          }
        }
        if (*csv == ',') ++csv;
      }
      if (synth.pct_count == 0) { // fallback
        std::snprintf(synth.pct_names[0], sizeof(synth.pct_names[0]), "p50"); synth.pct_vals[0] = 0.50; synth.pct_count = 1;
      }
    }

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
  otrace::TracerGuard _tg;  
  ev->committed.store(1, std::memory_order_release);
}

inline bool enabled() {
  return reg().enabled.load(std::memory_order_relaxed);
}

inline void emit_begin(const char* name, const char* cat=nullptr) {
  otrace::TracerGuard _tg;
  if (!should_emit(name, cat)) return;     
  if (!enabled()) return;
  Event* ev = get_tbuf()->append();
  fill_common(*ev, Phase::B, name, cat);
  commit(ev);
}

inline void emit_end(const char* name, const char* cat=nullptr) {
  otrace::TracerGuard _tg;  
  if (!should_emit(name, cat)) return;     
  if (!enabled()) return;
  Event* ev = get_tbuf()->append();
  fill_common(*ev, Phase::E, name, cat);
  commit(ev);
}

inline void emit_instant(const char* name, const char* cat=nullptr) {
  otrace::TracerGuard _tg;
  if (!should_emit(name, cat)) return;     
  if (!enabled()) return;
  Event* ev = get_tbuf()->append();
  fill_common(*ev, Phase::I, name, cat);
  commit(ev);
}

inline void emit_instant_kvs(const char* name, const char* cat,
                             const char* key, const char* value) {
  otrace::TracerGuard _tg;
  if (!should_emit(name, cat)) return;
  if (!enabled()) return;
  Event* ev = get_tbuf()->append();
  fill_common(*ev, Phase::I, name, cat);
  arg_string(*ev, key, value);
  commit(ev);
}


inline void emit_instant_kv(const char* name, const char* key, double val, const char* cat=nullptr) {
  otrace::TracerGuard _tg;
  if (!should_emit(name, cat)) return;     
  if (!enabled()) return;
  Event* ev = get_tbuf()->append();
  fill_common(*ev, Phase::I, name, cat);
  if (key) arg_number(*ev, key, val);
  commit(ev);
}

inline void emit_counter_n(const char* name, const char* cat, int n, const char** keys, const double* vals) {
  otrace::TracerGuard _tg;
  if (!should_emit(name, cat)) return;     
  if (!enabled()) return;
  Event* ev = get_tbuf()->append();
  fill_common(*ev, Phase::C, name, cat);
  for (int i=0;i<n && i<(int)OTRACE_MAX_ARGS;i++) arg_number(*ev, keys[i], vals[i]);
  // ensure the primary series exists: if no keys provided, use event name as key
  if (n==0) arg_number(*ev, name, 0.0);
  commit(ev);
}

inline void emit_complete(const char* name, uint64_t dur_us, const char* cat=nullptr) {
  otrace::TracerGuard _tg;
  if (!should_emit(name, cat)) return;
  if (!enabled()) return;
  Event* ev = get_tbuf()->append();
  fill_common(*ev, Phase::X, name, cat);
  ev->dur_us = dur_us;
  commit(ev);
}

inline void emit_complete_kv(const char* name, uint64_t dur_us, const char* key, double val, const char* cat=nullptr) {
  otrace::TracerGuard _tg;
  if (!should_emit(name, cat)) return;
  if (!enabled()) return;
  Event* ev = get_tbuf()->append();
  fill_common(*ev, Phase::X, name, cat);
  ev->dur_us = dur_us;
  if (key) arg_number(*ev, key, val);
  commit(ev);
}

// ---- Variadic KV helpers for instants (numbers and strings) ----
// String-like overloads first
    
inline void otrace_add_one_kv(Event& e, const char* key, const char* v) {
  arg_string(e, key, v ? v : "");
}
inline void otrace_add_one_kv(Event& e, const char* key, const std::string& v) {
  arg_string(e, key, v.c_str());
}
#if __cplusplus >= 201703L
inline void otrace_add_one_kv(Event& e, const char* key, std::string_view v) {
  char tmp[OTRACE_MAX_ARGV];
  if (v.size() >= sizeof(tmp)) v = v.substr(0, sizeof(tmp)-1);
  std::snprintf(tmp, sizeof(tmp), "%.*s", (int)v.size(), v.data());
  arg_string(e, key, tmp);
}
#endif

// Numeric & bool fallback (SFINAE so it doesn't collide with string overloads)
template <class V, typename = std::enable_if_t<std::is_arithmetic_v<std::decay_t<V>>>>
inline void otrace_add_one_kv(Event& e, const char* key, V&& v) {
  arg_number(e, key, static_cast<double>(v));
}

inline void otrace_add_kvs(Event&) {}

template <class V, class... Rest>
inline void otrace_add_kvs(Event& e, const char* key, V&& v, Rest&&... rest) {
  otrace_add_one_kv(e, key, std::forward<V>(v));
  if constexpr (sizeof...(rest) > 0) {
    otrace_add_kvs(e, std::forward<Rest>(rest)...);
  }
}

template <class... KVs>
inline void emit_instant_kvs(const char* name, const char* cat, KVs&&... kvs) {
  static_assert(sizeof...(kvs) % 2 == 0, "emit_instant_kvs expects key/value pairs");
  if (!should_emit(name, cat)) return;
  if (!enabled()) return;
  Event* ev = get_tbuf()->append();
  fill_common(*ev, Phase::I, name, cat);
  if constexpr (sizeof...(kvs) > 0) {
    otrace_add_kvs(*ev, std::forward<KVs>(kvs)...);
  }
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
  otrace::TracerGuard _tg;
  if (!name) name = "flow";
  if (!cat)  cat  = "flow";
  if (!should_emit(name, cat)) return;
  if (!enabled()) return;
  Event* ev = get_tbuf()->append();
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
  bool record;        
  uint64_t t0;

  Scope(const char* nm, const char* ct=nullptr)
  : name(nm), cat(ct), arg_key(nullptr), arg_val(0), has_arg(false) {
    otrace::TracerGuard _tg;  
    record = should_emit(name, cat);
    t0 = record ? now_us() : 0;
  }

  Scope(const char* nm, const char* ct, const char* key, double val)
  : name(nm), cat(ct), arg_key(key), arg_val(val), has_arg(true) {
    otrace::TracerGuard _tg;  
    record = should_emit(name, cat);
    t0 = record ? now_us() : 0;
  }

  ~Scope() {
    otrace::TracerGuard _tg;  
    if (!record) return;
    uint64_t dur = now_us() - t0;
    if (has_arg) emit_complete_kv(name, dur, arg_key, arg_val, cat);
    else         emit_complete(name, dur, cat);
  }
};


// ---- Flush ----------------------------------------------------------------

struct CleanEvent {
  // copy‑friendly event for sorting/writing (no atomics)
  uint64_t ts_us, dur_us, flow_id;
  uint32_t pid, tid, seq; 
  Phase ph;
  char name[OTRACE_MAX_NAME];
  char cat[OTRACE_MAX_CAT];
  char cname[OTRACE_MAX_CNAME];
  uint8_t argc; Arg args[OTRACE_MAX_ARGS];

};
    
#define OTRACE_HAVE_CLEAN_SEQ 1

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
      ce.ts_us=src->ts_us; ce.dur_us=src->dur_us; ce.flow_id=src->flow_id;
      ce.pid=src->pid; ce.tid=src->tid; ce.seq=src->seq; 
      ce.ph=src->ph;
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



// ---- Synthetic tracks at flush (optional) ---------------------------------
#if OTRACE_SYNTHESIZE_TRACKS
inline void synthesize_tracks(const std::vector<CleanEvent>& in,
                              std::vector<CleanEvent>& out,
                              const Registry::SynthCfg& cfg) {
  // helpers to emit counters/instants
  auto emit_counter = [&](uint64_t ts, const char* name, const char* key, double val,
                          uint32_t pid, uint32_t tid=0, const char* cat="synth") {
    CleanEvent ce{}; ce.ts_us=ts; ce.pid=pid; ce.tid=tid; ce.ph=Phase::C;
    std::snprintf(ce.name,sizeof(ce.name),"%s", name);
    std::snprintf(ce.cat, sizeof(ce.cat), "%s", cat);
    ce.argc=1;
    std::snprintf(ce.args[0].key,sizeof(ce.args[0].key),"%s", key?key:"value");
    ce.args[0].kind=ArgKind::Number; ce.args[0].num=val;
    out.push_back(ce);
  };
  auto emit_instant = [&](uint64_t ts, const char* name, uint32_t pid, uint32_t tid=0,
                          const char* cat="synth") {
    CleanEvent ce{}; ce.ts_us=ts; ce.pid=pid; ce.tid=tid; ce.ph=Phase::I;
    std::snprintf(ce.name,sizeof(ce.name),"%s", name);
    std::snprintf(ce.cat, sizeof(ce.cat), "%s", cat);
    out.push_back(ce);
    return &out.back();
  };

  // gather basics
  uint32_t pid = reg().pid_v;
  uint64_t last_ts = 0;
  for (auto& e : in) if (e.ts_us > last_ts) last_ts = e.ts_us;

  // FPS from frame markers (name="frame", cat="frame")
  {
    std::vector<uint64_t> fts; fts.reserve(1024);
    for (auto& e : in) {
      if (e.ph==Phase::I && e.name[0] && std::strcmp(e.name,"frame")==0 &&
          e.cat[0] && std::strcmp(e.cat,"frame")==0) {
        fts.push_back(e.ts_us);
      }
    }
    if (!fts.empty()) {
      const uint64_t W = cfg.rate_window_us ? cfg.rate_window_us : 500000;
      size_t j = 0;
      for (size_t i = 0; i < fts.size(); ++i) {
        uint64_t t = fts[i];
        while (j < i && fts[j] + W < t) ++j;
        size_t count = i - j + 1;
        double fps = (double)count * 1000000.0 / (double)W;
        emit_counter(t, "fps", "fps", fps, pid, 0, "synth");
      }
    }
  }

  // Counter rates: rate(<counter-name>)
  {
    struct Sample { uint64_t ts; double v; };
    // map: name -> vector<Sample> (first arg only)
    std::map<std::string, std::vector<Sample>> series;
    series.clear();
    for (auto& e : in) {
      if (e.ph != Phase::C || e.argc == 0) continue;
      if (e.args[0].kind != ArgKind::Number) continue;
      series[e.name].push_back({e.ts_us, e.args[0].num});
    }
    for (auto& kv : series) {
      auto& v = kv.second;
      if (v.size() < 2) continue;
      std::sort(v.begin(), v.end(), [](auto& a, auto& b){ return a.ts < b.ts; });
      for (size_t i=1;i<v.size();++i) {
        double dt = (double)(v[i].ts - v[i-1].ts) / 1e6;
        if (dt <= 0) continue;
        double rate = (v[i].v - v[i-1].v) / dt;   // units per second
        char name[OTRACE_MAX_NAME];
        std::snprintf(name, sizeof(name), "rate(%s)", kv.first.c_str());
        emit_counter(v[i].ts, name, "value", rate, pid, 0, "synth");
      }
    }
  }

  // Scope latency percentiles: latency(<name>) instant at end-of-trace
  {
    std::map<std::string, std::vector<double>> lat;
    for (auto& e : in) {
      if (e.ph == Phase::X && e.name[0]) {
        lat[e.name].push_back((double)e.dur_us); // us
      }
    }
    for (auto& kv : lat) {
      if (kv.second.empty()) continue;
      auto v = kv.second;
      std::sort(v.begin(), v.end());
      char nm[OTRACE_MAX_NAME];
      std::snprintf(nm, sizeof(nm), "latency(%s)", kv.first.c_str());
      // ms for readability (displayTimeUnit is ms)
      CleanEvent* inst = emit_instant(last_ts ? last_ts : 0, nm, pid, 0, "synth");
      if (inst) {
        for (uint32_t i=0; i<cfg.pct_count && inst->argc < OTRACE_MAX_ARGS; ++i) {
          double q = cfg.pct_vals[i];
          size_t idx = (size_t)std::floor(q * (v.size()-1));
          double us = v[idx];
          double ms = us / 1000.0;
          Arg& a = inst->args[inst->argc++];
          std::snprintf(a.key, sizeof(a.key), "%s", cfg.pct_names[i]);
          a.kind = ArgKind::Number; a.num = ms;
        }
      }
    }
  }
}
#endif // OTRACE_SYNTHESIZE_TRACKS






// --- rotation/gzip helpers -------------------------------------------------

inline bool ends_with(const char* s, const char* suff) {
  if (!s || !suff) return false;
  size_t n = std::strlen(s), m = std::strlen(suff);
  return (m <= n) && (std::memcmp(s + (n - m), suff, m) == 0);
}

inline void format_indexed(char* out, size_t out_sz, const char* pattern, uint32_t idx, bool has_index) {
  if (!pattern || !pattern[0]) { out[0]='\0'; return; }
  if (has_index) {
    std::snprintf(out, out_sz, pattern, (unsigned)idx);
  } else {
    // If no %d, append "-%06u"
    std::snprintf(out, out_sz, "%s-%06u", pattern, (unsigned)idx);
  }
}

inline void make_tmp_path(char* out, size_t out_sz, const char* final_path) {
  std::snprintf(out, out_sz, "%s.tmp", final_path ? final_path : "trace.json");
}

inline uint64_t file_size_bytes(const char* path) {
  if (!path) return 0;
#if defined(_WIN32)
  struct _stat64 st; if (_stat64(path, &st) == 0) return (uint64_t)st.st_size; else return 0;
#else
  struct stat st; if (stat(path, &st) == 0) return (uint64_t)st.st_size; else return 0;
#endif
}

// Write JSON trace to a FILE*
inline void write_trace_json_FILE(FILE* f, const std::vector<CleanEvent>& all) {
  std::fputs("{\n\"traceEvents\":[\n", f);
  for (size_t i = 0; i < all.size(); ++i) {
    write_event_json_common(f, all[i]);
    if (i + 1 != all.size()) std::fputs(",\n", f);
  }
  std::fputs("\n],\n\"displayTimeUnit\":\"ms\"\n}\n", f);
}

#if OTRACE_USE_ZLIB || OTRACE_USE_MINIZ
// Stream-compress a whole file to gzip (.gz) using zlib-compatible API.
inline bool compress_file_to_gzip(const char* in_path, const char* out_path, int level /*1..9*/) {
  if (!in_path || !out_path) return false;
  FILE* fin = std::fopen(in_path, "rb");
  if (!fin) return false;
  FILE* fout = std::fopen(out_path, "wb");
  if (!fout) { std::fclose(fin); return false; }

  // zlib/miniz stream
  z_stream zs{};
  // windowBits=15, +16 -> gzip header/trailer
  int rc = deflateInit2(&zs, level, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
  if (rc != Z_OK) { std::fclose(fin); std::fclose(fout); return false; }

  const size_t IN_CHUNK  = 256 * 1024;
  const size_t OUT_CHUNK = 256 * 1024;
  std::vector<unsigned char> inbuf(IN_CHUNK);
  std::vector<unsigned char> outbuf(OUT_CHUNK);

  bool ok = true;
  for (;;) {
    zs.avail_in = (uInt)std::fread(inbuf.data(), 1, IN_CHUNK, fin);
    zs.next_in  = inbuf.data();
    int flush = std::feof(fin) ? Z_FINISH : Z_NO_FLUSH;

    do {
      zs.avail_out = (uInt)OUT_CHUNK;
      zs.next_out  = outbuf.data();
      rc = deflate(&zs, flush);
      if (rc == Z_STREAM_ERROR) { ok = false; break; }
      size_t have = OUT_CHUNK - zs.avail_out;
      if (have && std::fwrite(outbuf.data(), 1, have, fout) != have) { ok = false; break; }
    } while (zs.avail_out == 0);

    if (!ok || flush == Z_FINISH) break;
  }

  deflateEnd(&zs);
  std::fclose(fin);
  if (std::fclose(fout) != 0) ok = false;

  if (!ok) {
    std::remove(out_path);

  }
  return ok;
}
#endif // OTRACE_USE_ZLIB || OTRACE_USE_MINIZ

// Configure rotation + optional gzip
inline void set_output_pattern(const char* pattern, uint32_t max_size_mb, uint32_t max_files) {
  Registry& R = reg();
  if (!pattern || !pattern[0]) {
    R.pattern[0] = '\0';
    R.pattern_has_index = false;
    R.pattern_use_gzip = false;
    R.max_files = 0; R.max_size_mb = 0; R.rot_index = 0;
    return;
  }

  std::snprintf(R.pattern, sizeof(R.pattern), "%s", pattern);
  R.max_files   = (max_files == 0 ? 1u : max_files);
  R.max_size_mb = max_size_mb;

  // cheap scan: contains any %<...>d sequence?
  R.pattern_has_index = false;
  for (const char* p = R.pattern; *p; ++p) {
    if (*p == '%') {
      ++p;
      // skip flags/width/precision like %04d etc.
      while (*p && std::strchr("0-+ #", *p)) ++p;
      while (*p && (*p >= '0' && *p <= '9')) ++p;
      if (*p == 'd' || *p == 'u') { R.pattern_has_index = true; break; }
    }
  }

  // gzip if .gz suffix AND a gzip backend is available
  bool want_gz = ends_with(R.pattern, ".gz");
#if OTRACE_USE_ZLIB || OTRACE_USE_MINIZ
  R.pattern_use_gzip = want_gz;
#else
  R.pattern_use_gzip = false; // no gzip backend compiled in
#endif
  R.rot_index = 0;
}



// The big “do-it-all” rotated writer. Writes one fresh file per flush:
// - Builds final path from pattern + rot_index (optionally appending "-%06u")
// - Writes JSON to a .tmp
// - Optionally gzips .tmp -> final .gz (needs zlib/miniz)
// - Renames .tmp -> final if not gzipping
// - Bumps rot_index and enforces max_files via wrap-around naming.
inline void write_rotated_trace(const std::vector<CleanEvent>& all) {
  Registry& R = reg();
  char final_path[512], tmp_path[512];

  // choose target path
  format_indexed(final_path, sizeof(final_path), R.pattern, R.rot_index, R.pattern_has_index);

  // Safety: if pattern ends with .gz but gzip is not available, drop suffix
  char adjusted_final[512];
  if (ends_with(final_path, ".gz") && !R.pattern_use_gzip) {
    std::snprintf(adjusted_final, sizeof(adjusted_final), "%.*s",
                  (int)(std::strlen(final_path) - 3), final_path);
  } else {
    std::snprintf(adjusted_final, sizeof(adjusted_final), "%s", final_path);
  }

  make_tmp_path(tmp_path, sizeof(tmp_path), adjusted_final);
  otrace::mkpath(adjusted_final);                 
  // 1) Write plain JSON into tmp file
  FILE* ftmp = std::fopen(tmp_path, "wb");
  if (!ftmp) return;
  write_trace_json_FILE(ftmp, all);
  std::fclose(ftmp);

  // Enforce max size *post factum* (we don't split): if too big, we still keep it.
  // This knob is mostly advisory for now.
  (void)R.max_size_mb; // reserved for future chunking

  // 2) If gzip requested and available, compress tmp -> final.gz
  bool wrote_ok = true;
  if (R.pattern_use_gzip && ends_with(final_path, ".gz")) {
#if OTRACE_USE_ZLIB || OTRACE_USE_MINIZ
    wrote_ok = compress_file_to_gzip(tmp_path, final_path, 6 /*balanced*/);
#else
    wrote_ok = false;
#endif
    // remove tmp either way
std::remove(tmp_path);

} else {
  // 3) No gzip: rename tmp -> final (overwrite)
  std::remove(adjusted_final); // ensure we can replace
  wrote_ok = (0 == std::rename(tmp_path, adjusted_final));
  if (!wrote_ok) {
    // fallback: copy+remove
    FILE* src = std::fopen(tmp_path, "rb");
    FILE* dst = std::fopen(adjusted_final, "wb");
    if (src && dst) {
      char buf[256*1024];
      size_t n;
      while ((n = std::fread(buf, 1, sizeof(buf), src)) != 0) {
        if (std::fwrite(buf, 1, n, dst) != n) { wrote_ok = false; break; }
      }
    } else wrote_ok = false;
    if (src) std::fclose(src);
    if (dst) std::fclose(dst);
    std::remove(tmp_path);
  }
}


  // 4) Bump index (wrap)
  R.rot_index = (R.rot_index + 1) % (R.max_files ? R.max_files : 1);
}

// public API wrapper
inline void set_output_pattern_api(const char* pattern, uint32_t max_size_mb, uint32_t max_files) {
  set_output_pattern(pattern, max_size_mb, max_files);
}


inline void flush_file(const char* path) {
  // Pause new writes without blocking in-flight ones
  bool prev = reg().enabled.exchange(false, std::memory_order_acq_rel);

  std::vector<CleanEvent> all; all.reserve(4096);
  collect_all(all);
    #if OTRACE_HEAP
  // Generate heap report before flushing
        #if 0  // <- disable to avoid deadlock
  heap::generate_report();
  // Re-collect to include heap report events
  all.clear();
  collect_all(all);
  #endif
    #endif


    // Sort for coherent timeline (ts, tid, seq if present)
  std::sort(all.begin(), all.end(), [](const CleanEvent& a, const CleanEvent& b){
    if (a.ts_us != b.ts_us) return a.ts_us < b.ts_us;
    if (a.tid   != b.tid)   return a.tid   < b.tid;
#ifdef OTRACE_HAVE_CLEAN_SEQ
    return a.seq < b.seq;
#else
    return (int)a.ph < (int)b.ph;
#endif
  });

#if OTRACE_SYNTHESIZE_TRACKS
  if (reg().synth_enabled.load(std::memory_order_relaxed)) {
    std::vector<CleanEvent> extra;
    extra.reserve(1024);
    synthesize_tracks(all, extra, reg().synth);
    all.insert(all.end(), extra.begin(), extra.end());
    std::stable_sort(all.begin(), all.end(), [](const CleanEvent& a, const CleanEvent& b){
      if (a.ts_us != b.ts_us) return a.ts_us < b.ts_us;
      if (a.tid   != b.tid)   return a.tid   < b.tid;
#ifdef OTRACE_HAVE_CLEAN_SEQ
      return a.seq < b.seq;
#else
      return (int)a.ph < (int)b.ph;
#endif
    });
  }
#endif


  // If rotation is configured, use it (ignores 'path')
  if (reg().pattern[0]) {
    write_rotated_trace(all);
    reg().enabled.store(prev, std::memory_order_release);
    return;
  }

  // Legacy single-file path
  const char* out_path = path ? path : reg().default_path;
  otrace::mkpath(out_path); 
  FILE* f = std::fopen(out_path, "wb");
  if (!f) { reg().enabled.store(prev, std::memory_order_release); return; }

  write_trace_json_FILE(f, all);
  std::fclose(f);
  #if OTRACE_HEAP
  // Generate heap report before flushing
  heap::generate_report();
  #endif

  reg().enabled.store(prev, std::memory_order_release);
}    

inline void atexit_flush() {
#if OTRACE_ON_EXIT
  flush_file(reg().default_path);
#endif
}
inline bool csv_has(const char* csv, const char* key) {
  if (!csv || !csv[0] || !key || !key[0]) return false;
  const char* p = csv;
  size_t k = std::strlen(key);
  while (*p) {
    while (*p==',' || *p==' ' || *p=='\t') ++p;
    const char* s = p;
    while (*p && *p!=',') ++p;
    size_t n = (size_t)(p - s);
    if (n==k && std::strncmp(s, key, k)==0) return true;
    if (*p==',') ++p;
  }
  return false;
}

inline bool should_emit(const char* name, const char* cat) {
  if (!reg().enabled.load(std::memory_order_relaxed)) return false;

  // sampling
  double keep = reg().sample_keep;
  if (keep < 1.0) {
    // tiny thread-local xorshift
    thread_local uint64_t s = (uint64_t)otrace::tid() * 0x9E3779B97F4A7C15ull + now_us();
    s ^= s << 13; s ^= s >> 7; s ^= s << 17;
    // 53b mantissa -> [0,1)
    double u = (double)((s >> 11) & ((1ull<<53)-1)) / (double)(1ull<<53);
    if (u > keep) return false;
  }

  // allow/deny cats
  if (reg().allow_cats[0] && !csv_has(reg().allow_cats, cat ? cat : "")) return false;
  if (reg().deny_cats[0]  &&  csv_has(reg().deny_cats,  cat ? cat : "")) return false;

  // user filter
  auto f = reg().filter;
  if (f && !f(name ? name : "", cat ? cat : "")) return false;

  return true;
}

// One-time env read inside hook()
struct AtEnvInit {
  AtEnvInit() {
    if (const char* d = std::getenv("OTRACE_DISABLE")) otrace::reg().enabled.store(false, std::memory_order_release);
    if (const char* e = std::getenv("OTRACE_ENABLE"))  otrace::reg().enabled.store(true,  std::memory_order_release);
    if (const char* s = std::getenv("OTRACE_SAMPLE"))  reg().sample_keep = std::atof(s);
  }
};
inline AtEnvInit& envinit() { static AtEnvInit E; return E; }
struct AtExitHook { AtExitHook(){ (void)envinit(); std::atexit(atexit_flush); } };
inline AtExitHook& hook() { static AtExitHook H; return H; }

// --- filter/sampling API (namespace-scope) ---
inline void otrace_set_filter(OtraceFilter f) { reg().filter = f; }
inline void otrace_enable_cats(const char* csv) {
  std::snprintf(reg().allow_cats, sizeof(reg().allow_cats), "%s", csv ? csv : "");
}
inline void otrace_disable_cats(const char* csv) {
  std::snprintf(reg().deny_cats, sizeof(reg().deny_cats), "%s", csv ? csv : "");
}
inline void otrace_set_sampling(double keep) {
  if (keep < 0) keep = 0; if (keep > 1) keep = 1;
  reg().sample_keep = keep;
}
    

} // namespace otrace

#if OTRACE_HEAP && OTRACE_DEFINE_HEAP_HOOKS

// Global new/delete operators
void* operator new(std::size_t size) {
    void* ptr = std::malloc(size);
    if (ptr) otrace::heap::record_alloc(ptr, size);
    return ptr;
}

void operator delete(void* ptr) noexcept {
    otrace::heap::record_free(ptr);
    std::free(ptr);
}

void* operator new[](std::size_t size) {
    void* ptr = std::malloc(size);
    if (ptr) otrace::heap::record_alloc(ptr, size);
    return ptr;
}

void operator delete[](void* ptr) noexcept {
    otrace::heap::record_free(ptr);
    std::free(ptr);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    void* ptr = std::malloc(size);
    if (ptr) otrace::heap::record_alloc(ptr, size);
    return ptr;
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept {
    otrace::heap::record_free(ptr);
    std::free(ptr);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    void* ptr = std::malloc(size);
    if (ptr) otrace::heap::record_alloc(ptr, size);
    return ptr;
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
    otrace::heap::record_free(ptr);
    std::free(ptr);
}

#endif // OTRACE_HEAP && OTRACE_DEFINE_HEAP_HOOKS

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

#ifndef OTRACE_PP_CAT
#define OTRACE_PP_CAT(a,b) OTRACE_PP_CAT_I(a,b)
#define OTRACE_PP_CAT_I(a,b) a##b
#endif

// RAII scopes
#define OTRACE_SCOPE(name) \
  ::otrace::Scope OTRACE_PP_CAT(_otrace_scope_, __LINE__)( \
    ([&](){ (void)::otrace::hook(); return (name); }()) )

#define OTRACE_SCOPE_C(name, cat) \
  ::otrace::Scope OTRACE_PP_CAT(_otrace_scope_, __LINE__)( \
    ([&](){ (void)::otrace::hook(); return (name); }()), (cat) )

#define OTRACE_SCOPE_KV(name, key, val) \
  ::otrace::Scope OTRACE_PP_CAT(_otrace_scope_, __LINE__)( \
    ([&](){ (void)::otrace::hook(); return (name); }()), nullptr, (key), (double)(val) )

#define OTRACE_SCOPE_CKV(name, cat, key, val) \
  ::otrace::Scope OTRACE_PP_CAT(_otrace_scope_, __LINE__)( \
    ([&](){ (void)::otrace::hook(); return (name); }()), (cat), (key), (double)(val) )


#define OTRACE_ZONE(name)            OTRACE_SCOPE_C((name), "zone")

// Begin/End
#define OTRACE_BEGIN(name)           do{ OTRACE_TOUCH(); otrace::emit_begin((name), nullptr); }while(0)
#define OTRACE_BEGIN_C(name, cat)    do{ OTRACE_TOUCH(); otrace::emit_begin((name), (cat)); }while(0)
#define OTRACE_END(name)             do{ OTRACE_TOUCH(); otrace::emit_end((name), nullptr); }while(0)
#define OTRACE_END_C(name, cat)      do{ OTRACE_TOUCH(); otrace::emit_end((name), (cat)); }while(0)

// Instants
#define OTRACE_INSTANT(name)             do{ OTRACE_TOUCH(); otrace::emit_instant((name), nullptr); }while(0)
#define OTRACE_INSTANT_C(name, cat)      do{ OTRACE_TOUCH(); otrace::emit_instant((name), (cat)); }while(0)
#define OTRACE_INSTANT_KV(name, ...)     do{ OTRACE_TOUCH(); otrace::emit_instant_kvs((name), nullptr, __VA_ARGS__); }while(0)
#define OTRACE_INSTANT_CKV(name, cat, ...) \
  do{ OTRACE_TOUCH(); otrace::emit_instant_kvs((name), (cat), __VA_ARGS__); }while(0)

#define OTRACE_ENABLE_SYNTH_TRACKS(on) \
  do{ OTRACE_TOUCH(); ::otrace::reg().synth_enabled.store(!!(on), std::memory_order_release); }while(0)

// Frames
#define OTRACE_MARK_FRAME(idx) \
  do{ OTRACE_TOUCH(); otrace::emit_instant_kvs("frame", "frame", "frame", (double)(idx)); }while(0)
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
// Rotation + gzip (pattern may contain %d or %0Nd for index; ".gz" honored if gzip backend is compiled)
#define OTRACE_SET_OUTPUT_PATTERN(pattern, max_size_mb, max_files) \
  do{ OTRACE_TOUCH(); ::otrace::set_output_pattern_api((pattern), (uint32_t)(max_size_mb), (uint32_t)(max_files)); }while(0)


// Call-by-name single macro: OTRACE_CALL(SCOPE, "name"), OTRACE_CALL(COUNTER, "n", v), etc.
#define OTRACE_CALL(name, ...) OTRACE_##name(__VA_ARGS__)
#if !OTRACE_NO_SHORT_MACROS
  #define TRACE(name, ...) OTRACE_##name(__VA_ARGS__)
#endif

// Filters & sampling (public macros)
#define OTRACE_SET_FILTER(fn)        do{ OTRACE_TOUCH(); ::otrace::otrace_set_filter((fn)); }while(0)
#define OTRACE_ENABLE_CATS(csv)      do{ OTRACE_TOUCH(); ::otrace::otrace_enable_cats((csv)); }while(0)
#define OTRACE_DISABLE_CATS(csv)     do{ OTRACE_TOUCH(); ::otrace::otrace_disable_cats((csv)); }while(0)
#define OTRACE_SET_SAMPLING(p)       do{ OTRACE_TOUCH(); ::otrace::otrace_set_sampling((p)); }while(0)

#if OTRACE_HEAP
#define OTRACE_HEAP_ENABLE(on)        do{ OTRACE_TOUCH(); ::otrace::heap::enable(!!(on)); }while(0)
#define OTRACE_HEAP_SET_SAMPLING(p)   do{ OTRACE_TOUCH(); ::otrace::heap::set_sampling((p)); }while(0)
#define OTRACE_HEAP_REPORT()          do{ OTRACE_TOUCH(); ::otrace::heap::generate_report(); }while(0)
#else
#define OTRACE_HEAP_ENABLE(on)        ((void)0)
#define OTRACE_HEAP_SET_SAMPLING(p)   ((void)0)
#define OTRACE_HEAP_REPORT()          ((void)0)
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
  #define TRACE_SET_OUTPUT_PATTERN(...)  OTRACE_SET_OUTPUT_PATTERN(__VA_ARGS__)
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
#define OTRACE_ENABLE_SYNTH_TRACKS(...)         ((void)0)


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
  #define TRACE_ENABLE_SYNTH_TRACKS(...)        OTRACE_ENABLE_SYNTH_TRACKS(__VA_ARGS__)
#endif

#endif // OTRACE
#endif // OTRACE_HPP_INCLUDED
