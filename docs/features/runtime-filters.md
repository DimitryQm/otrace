# Runtime filters, category allow/deny, and sampling

otrace can prune events at runtime with near-zero overhead on the hot path. You can:
- Install a **filter callback** to decide per event.
- Enable/disable by **category CSV** (allow/deny lists).
- Apply **probabilistic sampling** (keep a fraction of events).
- Nudge behavior with **environment variables** read on first use.

These are *opt-in*; by default, everything you emit is recorded.

## API

```cpp
// Filter signature: return true to keep, false to drop.
using OtraceFilter = bool(*)(const char* name, const char* cat);

void OTRACE_SET_FILTER(OtraceFilter fn);
void OTRACE_ENABLE_CATS(const char* csv);   // e.g. "io,frame,gpu"
void OTRACE_DISABLE_CATS(const char* csv);  // e.g. "debug,noise"
void OTRACE_SET_SAMPLING(double keep);      // 0.0..1.0
```

### Environment variables (read once on first use)

- `OTRACE_DISABLE` if set (to anything), disables recording.
    
- `OTRACE_ENABLE` if set, enables recording (wins over DISABLE if both set).
    
- `OTRACE_SAMPLE` sampling keep probability, e.g. `0.1` keeps 10% of events.
    

> Tip: these are read when the tracer is first touched (on first macro call). If you need to force a re-read, restart the process.

## Cost model

Filtering happens before we reserve a slot in the ring buffer, so dropped events don’t contend or allocate. The common checks are:

- a relaxed atomic read of the global “enabled” flag;
    
- optional CSV membership checks for categories;
    
- optional user filter call;
    
- optional per-thread xorshift RNG (only if sampling < 1.0).
    

## Examples

### Keep only “io” and “frame” categories
```cpp
int main() {
  #define OTRACE 1
  #include "otrace.hpp"

  OTRACE_ENABLE_CATS("io,frame");  // allowlist
  // ... your code
}
```
### Drop “debug,noise” categories and sample at 10%
```cpp
OTRACE_DISABLE_CATS("debug,noise");
OTRACE_SET_SAMPLING(0.10);
```
### Custom filter (drop any scope named “hot_loop” unless its category is “gpu”)
```cpp
static bool my_filter(const char* name, const char* cat) {
  if (name && std::strcmp(name, "hot_loop")==0)
    return (cat && std::strcmp(cat, "gpu")==0);
  return true;
}

OTRACE_SET_FILTER(&my_filter);

```
### Drive behavior via environment
```cpp
# disable by default
export OTRACE_DISABLE=1
# re-enable and keep 5%
export OTRACE_ENABLE=1
export OTRACE_SAMPLE=0.05
```
## Interaction with RAII scopes

For `TRACE_SCOPE*`/`OTRACE_SCOPE*`, the filter decision is made **at scope entry**. If a scope is filtered out at entry, its destructor won’t emit a completion slice. This guarantees that a kept scope always produces both start/end as a single “complete” (X) event with a duration.

## Gotchas

- Category lists are **CSV strings** with simple token matches (no regex). Whitespace and empty tokens are ignored.
    
- Sampling is **independent per event** and uses a per-thread RNG. Two events from the same code site can diverge under sampling.
    
- Filter, allow/deny, and sampling are **composed** as: enabled? → sampling → allow/deny → user filter.
