## Macro namespacing and aliases

the public surface consists of a long, namespaced family (`OTRACE_*`) and an optional set of short aliases (`TRACE_*`) that forward to the namespaced forms. The default is to expose both because the short names read well in code. If you need to avoid collisions with other libraries that also use `TRACE_*`, define `OTRACE_NO_SHORT_MACROS=1` before you include the header and only the `OTRACE_*` names will exist.
```cpp
#define OTRACE_NO_SHORT_MACROS 1
#include "otrace.hpp"

OTRACE_SCOPE("parse");         // always available
// TRACE_SCOPE("parse");       // undefined when OTRACE_NO_SHORT_MACROS=1
```
**Call-by-name convenience**

Some codebases prefer a single macro that takes the operation as the first token because it is easier to wrap or generate. `OTRACE_CALL` is exactly that: it pastes the token and forwards the rest of the arguments unchanged. The `TRACE` variant exists when short aliases are enabled.
```cpp
// expands to OTRACE_SCOPE("init") and OTRACE_COUNTER("queue_len", q.size())
OTRACE_CALL(SCOPE, "init");
OTRACE_CALL(COUNTER, "queue_len", q.size());

#if !OTRACE_NO_SHORT_MACROS
TRACE(SCOPE, "render");
TRACE(COUNTER, "fps", fps);
#endif
```
**Build-time off switch**

All macros, including `OTRACE_CALL` and the short aliases, compile to no-ops when you build with `OTRACE=0`. This is a compile-time decision that removes the tracing fast path entirely. Code that calls the macros continues to compile but emits nothing; you do not need `#ifdef` fences around call sites.
