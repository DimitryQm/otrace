**Type-aware, variadic key/values for Instants (since 0.2.0)**

Instants can now carry multiple key/value pairs in a single emission, and values may be numeric or strings. The spellings stay the same `TRACE_INSTANT_KV` for the zero-category form and `TRACE_INSTANT_CKV` when you want a category, but under the hood they route to a variadic, type-aware path. If you were previously constrained to a single numeric value, that limitation is gone; mixed sets like `("phase", 2, "stage", "copy", "ok", 1)` are valid and preserved in the output JSON. Nothing about the semantics of instants changed: they are still zero-duration markers with arguments serialized under `args`. The only difference is expressive power and fewer calls on the hot path when you want to attach several small facts to the same moment.
```cpp
// single numeric KV
TRACE_INSTANT_KV("speed", "mps", 12.5);

// single string KV
TRACE_INSTANT_KV("note", "text", "hello world");

// multiple KVs with a category; mixed numeric+string
TRACE_INSTANT_CKV("tick", "frame",
                  "phase", 2,
                  "stage", "copy",
                  "ok",    1);
```

**Rationale**
The previous “one numeric KV per instant” design forced either multiple instants at the same timestamp or out-of-band stringification. The new path keeps the fast case fast, avoids extra emissions, and makes traces easier to read in Perfetto because all the details for that instant live in a single details pane entry.

**Output shape**

The JSON remains Chrome Trace Event compatible. Each instant appears with `"ph":"I"` and all supplied key/values folded under `args`. Strings are emitted as JSON strings; numbers are emitted as JSON numbers. Perfetto reads this directly.

```json
{
  "ph": "I",
  "name": "tick",
  "cat": "frame",
  "ts": 1234567,
  "args": { "phase": 2, "stage": "copy", "ok": 1 }
}
```
**Performance and boundaries**

There is no semantic sampling or buffering change. The event still consumes from the per-thread ring and is subject to the same budgeting rules. The number of recorded key/values on an event is capped by `OTRACE_MAX_ARGS`; once that budget is exhausted, further pairs are ignored for that emission. Keys and string values are copied into bounded, fixed-size fields and truncated if you exceed the compile-time limits; the defaults are sized for short identifiers rather than sentences. Numeric values are promoted to `double` at the point of storage as before. If you care about the exact cutoff lengths or the per-event budget, tune `OTRACE_MAX_ARGS`, `OTRACE_MAX_ARGK`, and `OTRACE_MAX_ARGV` at build time.

**Scope KVs are intentionally unchanged**

Only instants gained variadic and string-aware arguments. `TRACE_SCOPE_KV` and `TRACE_SCOPE_CKV` stay single-numeric on purpose to keep the RAII path compact and predictable in very hot code. If you need several labels at a specific entry or exit point, attach them with a single instant inside the scope.
```cpp
{
  TRACE_SCOPE_C("render_pass", "gpu");
  TRACE_INSTANT_CKV("pass_meta", "gpu",
                    "triangles", triangle_count,
                    "technique", "forward+");
  // …heavy work…
}
```

**Compatibility and migration**

Existing code continues to compile and behave the same. If your code already used `TRACE_INSTANT_KV("x","k",42);`, nothing changes; if you previously worked around the single-KV restriction by emitting several adjacent instants or by string-packing multiple facts into one value, simplify those sites to a single `TRACE_INSTANT_CKV` call. There are no new headers to include; `<type_traits>` is pulled in internally to discriminate strings from numbers at compile time.
