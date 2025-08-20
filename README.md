`otrace.hpp` is a single header that lets you annotate your C++ code with timeline events and then inspect them in Perfetto UI or chrome://tracing. You sprinkle a few macros around the code you care about, scopes, instants, counters, flows, frames, and the program produces a compact `trace.json`. Open that file in a timeline viewer and you can finally _see_ what your application did, when it did it, and on which threads, with microsecond resolution and essentially no setup beyond including one header.

This isn’t a sampling profiler or a system-wide tracer. It is deliberate, in-process instrumentation you control. Nothing is captured unless you emit it, which means the timeline shows the exact narrative you intended: your critical sections, your I/O, your queues, your handoffs, the shape of a frame, the path of a task.

## Getting started

Add the header to your project. Build your program with C++17 or later and define `OTRACE=1` to enable instrumentation. When `OTRACE` is left at the default `0`, every macro compiles out to a no-op, so there’s no overhead in production by default.
```cpp
// main.cpp
#define OTRACE 1
#include "otrace.hpp"

#include <thread>
#include <vector>
#include <chrono>

void work_unit(int i) {
  TRACE_SCOPE_CKV("work_unit", "compute", "i", i);
  std::this_thread::sleep_for(std::chrono::milliseconds(5 + (i % 3)));
  TRACE_COUNTER("queue_len", 42 - i);
}

int main() {
  TRACE_SET_PROCESS_NAME("sample-app");
  TRACE_SET_OUTPUT_PATH("trace.json");       // optional; defaults to "trace.json"

  {
    TRACE_SCOPE("startup");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    TRACE_INSTANT("tick");
  }

  // a few tasks, to make the timeline interesting
  for (int i = 0; i < 6; ++i) {
    work_unit(i);
  }

  TRACE_FLUSH(nullptr);                      // optional; on-exit flush is on by default
  return 0;
}

```

otrace requires **C++17+** and works on Windows/Linux/macOS so you can build it with your compiler of choice:
- Clang or GCC:
    
    `c++ -std=c++17 -O2 -DOTRACE=1 main.cpp -o demo`
    
- MSVC:
    
    `cl /std:c++17 /O2 /EHsc /DOTRACE=1 main.cpp`
Run the program; you’ll find a `trace.json` in the current directory (or whatever path you set). Drag and drop that file into **Perfetto UI** at https://ui.perfetto.dev or open **chrome://tracing** in Chrome and use the “Load” button. You’ll see your process and threads on the left, and on the right a time axis with colored slices for scopes, vertical pins for instants, graphs for counters, and optional arrows for flows.

## What you can emit (and how it appears)

The goal is that the timeline tells a story without needing a log window. The primitives are small, but expressive.

**Scopes** are the backbone. A scope is a block of time that starts when you enter a piece of code and ends when you leave it. The macro captures this as a single “complete” event with a duration. You can optionally attach a category and a small numeric argument.
```cpp
{
  TRACE_SCOPE("parse_config");                 // a single colored bar in the timeline
  // your code here
}

{
  TRACE_SCOPE_C("db_roundtrip", "io");         // categorize similar scopes
  // your code here
}

{
  TRACE_SCOPE_CKV("render", "gpu", "triangles", triangle_count);
  // your code here
}

```
In the viewer, each becomes a colored slice on the thread’s lane, with the name and category available on hover, and with the extra argument visible in the details. The width of the slice is its measured duration.

**Begin/End pairs** let you mark a long-lived operation when RAII isn’t convenient. They produce two separate events that the viewer stitches into a block.
```cpp
TRACE_BEGIN("upload");
// do the upload
TRACE_END("upload");
```
Most of the time, the RAII `TRACE_SCOPE*` is simpler and safer; reach for begin/end when you truly need to separate boundaries.

**Instants** are zero-duration markers. They show up as thin vertical pins in the timeline. They are perfect for “we hit this branch,” “we just swapped buffers,” or “tick” style annotations.
```cpp
TRACE_INSTANT("tick");
TRACE_INSTANT_C("tick", "frame");
TRACE_INSTANT_CKV("gc", "runtime", 1);        // small numeric detail attached
```
**Counters** are values over time. They render as line charts under the timeline so you can correlate trends against phases above, queue sizes draining, memory climbing, FPS oscillating. You can emit a single series or a small group under the same name.
```cpp
TRACE_COUNTER("queue_len", queue.size());
TRACE_COUNTER2("mem_kb", "rss", rss_kb, "cache", cache_kb);
```
**Flows** connect related points, even across threads. You supply an id that is stable for that logical item (a pointer cast to uint64_t is a handy candidate). The viewer draws arrows so you can visually follow a handoff.
```cpp
uint64_t id = (uint64_t)task_ptr;
TRACE_FLOW_BEGIN(id);                         // where the task is born or enqueued
// ... maybe enqueued to another thread ...
TRACE_FLOW_STEP(id);                          // intermediate step(s)
TRACE_FLOW_END(id);                           // finally handled or completed
```
**Frames** are just a convention for a recurring instant with name `"frame"`. The library provides helpers so you don’t have to repeat the details.
```cpp
TRACE_MARK_FRAME(frame_index);                // cat:"frame", args:{ frame:<index> }
TRACE_MARK_FRAME_S("present");                // args:{ label:"present" }
```
In Perfetto you can filter for `name:"frame"` to line up “a frame worth of work.”

**Metadata** isn’t about time; it’s about making the view readable. Give the process and each thread a human name and a sort position if you want control over their vertical order in the UI.
```cpp
TRACE_SET_PROCESS_NAME("imgproc");
TRACE_SET_THREAD_NAME("worker-0");
TRACE_SET_THREAD_SORT_INDEX(10);              // bigger numbers go lower in the stack
```
If you want one specific event to pop, you can hint a **color** for the next emission:
```cpp
TRACE_COLOR("good");                          // the next event will carry cname:"good"
TRACE_SCOPE("hot_path");                      // that one event gets the color hint
```
## How timestamps work (and what to choose)

Every timestamp in `trace.json` is **microseconds since first use** within your process. The source can be chosen at build time:

- The default uses `std::chrono::steady_clock`. It is monotonic and immune to wall-clock corrections. It’s the right answer for almost everyone.
    
- On x86 you can choose an `RDTSC` timebase by compiling with `-DOTRACE_CLOCK=2`. The header fences the instruction to reduce out-of-order noise and calibrates cycles to microseconds against `steady_clock` during startup. It is extremely cheap to read. Use it when you need very fast timestamps and you’re confident your environment has an invariant TSC.
    
- A third option uses `std::chrono::system_clock` (compile with `-DOTRACE_CLOCK=3`). This gives you wall-clock deltas and is susceptible to NTP or manual adjustments; it is rarely needed unless you want to align a trace with external logs by wall time.
    

If you compile with `OTRACE_CLOCK=2` on a non-x86 target, the header silently falls back to `steady_clock`.
## Buffering, flushing, and the file on disk

Each thread that touches the API gets a per-thread ring buffer with a fixed number of events (the default is `32768` per thread and can be tuned with `-DOTRACE_THREAD_BUFFER_EVENTS=<N>`). Appending an event reserves a slot, fills in the fields, and finally marks it committed with a single atomic store. The thread never locks. If the ring wraps, the oldest entries on that thread are overwritten.

When you call `TRACE_FLUSH(path)` (or when the process exits, because the default `-DOTRACE_ON_EXIT=1` arranges an atexit handler), the library briefly stops accepting new appends, copies committed events out of each thread’s buffer, adds metadata, sorts the events by time (with a stable tiebreaker), and writes them as a single JSON object with a `"traceEvents"` array. If the file cannot be opened, it restores the previous state and returns without aborting your program. You can change the default path at runtime via `TRACE_SET_OUTPUT_PATH("runs/trace.json")`.

## Building and toggling features

The header is designed to be self-contained and friendly to typical build setups. You include it exactly once per translation unit where you want to instrument (there are no link-time components). At build time, a handful of macros control behavior:

- `OTRACE` set to `1` enables instrumentation. Leaving it at `0` compiles every macro out to a no-op.
    
- `OTRACE_THREAD_BUFFER_EVENTS` lets you scale the per-thread ring size up or down.
    
- `OTRACE_DEFAULT_PATH` can change the default filename from `"trace.json"` to something like `"out/trace.json"`.
    
- `OTRACE_ON_EXIT` controls whether a flush happens at process exit; it is `1` by default.
    
- `OTRACE_CLOCK` chooses the timebase as described above.
    

At runtime you can also toggle collection:
```cpp
TRACE_DISABLE();                 // temporarily stop collecting
// do something noisy
TRACE_ENABLE();                  // resume
if (TRACE_IS_ENABLED()) { /* ... */ }
```
This can be valuable in long-running services where you only want a window of detail.

## Examples on the timeline (with screenshots)

The repository includes a handful of screenshots under `docs/images/` so you can glance at what to expect right in the README. They were captured from Perfetto UI using small sample programs built with the header. Screenshots from chrome://tracing will be added soon.

### 2)  **Overview timeline** threads, scopes, and instants

This view shows the big picture: per-thread rows, named slices from `TRACE_SCOPE` / `TRACE_BEGIN`…`TRACE_END`, and instant markers. In this trace the **producer** thread enqueues work (`make_job`), the **consumer** thread runs it (`process` slices), and the **main** thread is mostly idle. The dark tick marks in the top ruler are scheduler activity; you can gauge overlap and gaps at a glance.

<p align="center">
  <img src="docs/images/overview-timeline.png" alt="Overview of threads and scoped events in Perfetto" title="Overview timeline">
</p>

**Minimal Repro:**
```cpp
TRACE_SET_PROCESS_NAME("otrace-showcase");
TRACE_SET_THREAD_NAME("producer");
{
  TRACE_SCOPE_C("make_job", "compute");
  TRACE_INSTANT_CKV("submit", "flow", "enqueue", 1);
  // enqueue work...
}
TRACE_SET_THREAD_NAME("consumer");
{
  TRACE_SCOPE("process");
  // do the work...
}
```

### 2) Counter track live value over time

Here we’re plotting a queue length. Each purple bar is a sample written with `TRACE_COUNTER("queue_len", value)`. This is different from slices (durations) and instants (points): it gives you a **time-series** you can correlate with other tracks to explain latency spikes or under-utilization.
<p align="center"> <img src="docs/images/counters.png" alt="Queue length counter track in Perfetto" title="queue_len counter"> </p>

**Minimal Repro**:
```cpp
```// On enqueue/dequeue:
TRACE_COUNTER("queue_len", queue.size());
```


### 3) Flows, causal arrows across threads

The orange arrows below connect a `make_job` slice on the producer to the corresponding `process` slice on the consumer. Use `TRACE_FLOW_BEGIN(id)` at the source, `TRACE_FLOW_STEP(id)` for hops, and `TRACE_FLOW_END(id)` at the sink. Pick a stable `id` (e.g., your job id).

<p align="center"> <img src="docs/images/flows.png" alt="Flow arrows connecting producer and consumer slices" title="Flows across threads"> </p>

**Minimal Repro**:
```cpp
uint64_t job_id = next_id();

// Producer:
TRACE_FLOW_BEGIN(job_id);
TRACE_SCOPE("make_job");
// ...enqueue...
TRACE_FLOW_STEP(job_id);

// Consumer:
TRACE_SCOPE("process");
// ...work...
TRACE_FLOW_END(job_id);

```

### 4) Frames per-frame markers

In this example we mark frame indices via `TRACE_MARK_FRAME(i)` (numeric) or phases via `TRACE_MARK_FRAME_S("present")` (string label).
<p align="center"> <img src="docs/images/frames.png" alt="Frame instant markers on the producer thread" title="Frame markers"> </p>

**Minimal Repro:**
```cpp
for (int i = 0; i < N; ++i) {
  TRACE_MARK_FRAME(i);          // numeric index
  // or: TRACE_MARK_FRAME_S("present");

  // Do per-frame work
  TRACE_SCOPE("update");
  TRACE_SCOPE("render");
}
```
## Troubleshooting

If you built and ran and still don’t see a `trace.json`, the most common reasons are straightforward. Make sure you compiled with `-DOTRACE=1` so the macros actually do something. Confirm that the program can write to the working directory or set a different path at runtime with `TRACE_SET_OUTPUT_PATH`. If your process crashes before exit, call `TRACE_FLUSH(nullptr)` earlier to snapshot the buffers; the on-exit flush is best-effort. If your timestamps look strange with `OTRACE_CLOCK=2`, try the default timebase first; VMs and some laptops can present non-invariant TSC behavior that a calibration pass can’t fully tame.
