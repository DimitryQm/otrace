# Macro namespacing & aliases

otrace exposes both a long, namespaced macro family and (optionally) short `TRACE_*` aliases.

- **Always available:** `OTRACE_*` macros (e.g., `OTRACE_SCOPE`, `OTRACE_COUNTER`).
- **Optional short aliases:** `TRACE_*` macros mapping directly to `OTRACE_*`.

## Hiding the short aliases

Define this **before** including `otrace.hpp`:

```cpp
#define OTRACE_NO_SHORT_MACROS 1
#include "otrace.hpp"
```
Now only `OTRACE_*` exist. This avoids collisions if another library also uses `TRACE_*`.

## One-macro “call-by-name” helper

If you prefer a single entry point for macros:
```cpp
// expands to OTRACE_SCOPE("name"), OTRACE_COUNTER("n", value), etc.
OTRACE_CALL(SCOPE, "init");
OTRACE_CALL(COUNTER, "queue_len", q.size());
```
If you kept the short aliases enabled (default), there’s also:
```cpp
TRACE(SCOPE, "init");
TRACE(COUNTER, "queue_len", q.size());
```
> Why this exists: it makes it easier to gate, generate, or parameterize macro calls (e.g., wrapping with another macro that injects a category).
