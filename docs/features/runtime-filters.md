## Runtime filters, category allow/deny, and sampling

otrace can decide at emit time whether to keep or discard an event, and it does that before reserving a slot in the per-thread ring. The gate is deliberately small. First the global “enabled” flag is checked with a relaxed atomic read; then, if you have set a keep probability below one, a tiny thread-local xorshift produces a uniform random number and the event is dropped with probability `1-p`; then the category CSVs are consulted to enforce an allowlist and denylist; finally an optional user callback decides on name and category. Only if the event survives all four checks does the code touch the ring buffer.

**API and shape**

The filter callback is a plain function pointer of type `bool(const char* name, const char* cat)`. Return true to keep the event. Category gates accept simple CSV strings such as `"io,frame"` and perform token equality checks. Sampling expects a probability in the closed interval `[0,1]` and is applied independently to every event on every thread.
```cpp
// turn on only two categories
OTRACE_ENABLE_CATS("io,frame");

// drop noisy categories regardless of allowlist
OTRACE_DISABLE_CATS("debug,noise");

// keep ten percent of what remains
OTRACE_SET_SAMPLING(0.10);

// custom filter: keep gpu “hot_loop”, drop the rest of that name
static bool my_filter(const char* name, const char* cat) {
  if (name && std::strcmp(name, "hot_loop") == 0) return cat && std::strcmp(cat, "gpu") == 0;
  return true;
}
OTRACE_SET_FILTER(&my_filter);
```
**Environment control**

The process reads three environment variables the first time any macro is touched. `OTRACE_DISABLE` forces recording off. `OTRACE_ENABLE` forces it on and wins if both are present. `OTRACE_SAMPLE` sets the same keep probability as the API, for example `OTRACE_SAMPLE=0.05` keeps five percent. These values are latched; change the environment only if you are starting a fresh process.

**Scopes and begin/end**

RAII scopes make the decision at scope entry. If a scope is filtered out at entry, its destructor does not emit a completion slice, which guarantees that a kept scope is always written as a single complete (“X”) event with a duration. If you use explicit `TRACE_BEGIN` and `TRACE_END`, each call is filtered independently at its call site. Keep the name and category consistent and avoid filter conditions that might diverge between the two sides, or prefer the RAII form.

**Cost and determinism**

When nothing is configured the gate is effectively free. With sampling or category checks enabled the cost stays in the nanosecond range and does not allocate. Sampling is independent per event and per thread, so two events from the same source site can diverge by design. The filter runs on the emit path, not at flush, so it changes what gets recorded rather than what gets written.
