## Deterministic ordering for same-timestamp events

otrace records a per-thread sequence number on every event and uses it as the final tie-breaker during flush. Sorting is performed by `(ts_us, tid, seq)`. The outcome is simple: when several events land on the same microsecond, events from the same thread keep the exact program order in which they were appended. That property removes the “flip-flop on equal timestamps” artifact you get with high-resolution timers and very fast code, and it makes traces reproducible across runs as long as your code emits in the same order.

**What this actually fixes**

Zero-duration instants that fire back to back now appear in the viewer in the order you wrote them even if their timestamps compare equal. A frame marker that sits next to a short scope slice will not jump around between flushes when the clock quantizes them to the same tick. Cross-thread ties still order by thread id, which is intentional; the tracer does not try to infer cross-thread causality at sort time.

**How to think about it**

The sequence is local to a thread and starts at one when that thread first touches otrace. It only changes when you append events and it is never exposed to the viewer. If a thread dies and the OS later reuses its numeric TID for another thread, the new thread will start a fresh sequence but its events also have later timestamps, so ordering remains stable.

```cpp
// both instants often share the same microsecond on a fast CPU
TRACE_INSTANT("before");
TRACE_INSTANT("after");     // will appear after “before” in the file and in the UI
```
No knobs to set and nothing to migrate. The behavior is automatic when you update the header.
