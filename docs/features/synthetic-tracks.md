# Synthetic tracks at flush (since 0.2.0)

The recorder can manufacture helpful “explanatory” rows during `TRACE_FLUSH` without changing your hot path. When compiled in, the synthesizer scans the committed events you already emitted and appends derived counters and summary instants. Nothing runs until flush, nothing touches your per-thread rings while recording, and the output is still vanilla Chrome Trace Event JSON that Perfetto understands.

## Enabling, building, and toggling

Compile with the feature enabled if you want it available at runtime.

```bash
# enable recorder + synthesizer
g++ -std=c++17 -O2 -g -pthread \
  -DOTRACE=1 -DOTRACE_SYNTHESIZE_TRACKS=1 \
  main.cpp -o app_synth
```

Force the short `TRACE_*` aliases if your project disables them elsewhere.
```sh
g++ -std=c++17 -O2 -g -pthread \
  -DOTRACE=1 -DOTRACE_NO_SHORT_MACROS=0 -DOTRACE_SYNTHESIZE_TRACKS=1 \
  main.cpp -o app_synth
```
Turn synthesis on or off per run without recompiling.
```cpp
OTRACE_ENABLE_SYNTH_TRACKS(true);   // enable
OTRACE_ENABLE_SYNTH_TRACKS(false);  // disable
```
Tune the rate window and percentile labels at build time; unspecified values fall back to sane defaults.
```sh
# 0.5s window for rolling rates/FPS and the default percentile labels
-DOTRACE_SYNTH_RATE_WINDOW_US=500000 \
-DOTRACE_SYNTH_PCT_NAMES="p50,p95,p99"
```
## What gets synthesized

During flush the code walks your events in time order, derives a few common views, and emits them as regular counters or zero-duration instants. The new rows live under category `"synth"` and use the process id with tid `0` so they appear in per-process rows unless otherwise noted.

The first product is an FPS counter derived from `TRACE_MARK_FRAME(...)`. The synthesizer collects all instants whose name is `"frame"` and category is `"frame"`, runs a sliding window of `OTRACE_SYNTH_RATE_WINDOW_US`, and at each frame timestamp computes the count of frames in the window scaled to frames per second. The resulting counter is called `"fps"` and carries a single series named `"fps"`. It will be sparse if your frame cadence is sparse; there is no interpolation.

The second product is per-series rates for counters. For every counter stream you emitted with `TRACE_COUNTER("name", v)`, the flush path sorts samples by timestamp and computes finite differences between consecutive points. The value is `(Δvalue / Δtime)` and the series is named `"value"`. The row name is `rate(<counter-name>)`. If you emitted multi-series counters, only the first series (the one written by the convenience macros) is considered; additional series are deliberately ignored to avoid guessing at units.

The third product is latency percentiles per scope name. Every complete event (the “X” form produced by `TRACE_SCOPE*` and `TRACE_BEGIN/END` pairs) contributes its `dur_us`. At flush, the durations for a given name are sorted ascending and several quantiles are read using floor(`q * (n-1)`) indexing. The results are emitted as a single instant named `latency(<scope-name>)` at the end of the trace with keys taken from `OTRACE_SYNTH_PCT_NAMES` and values expressed in milliseconds to match the file’s `"displayTimeUnit":"ms"`. If a scope name appears once, the single value is reported for all requested percentiles.

## Minimal usage

You do not need new calls in hot code. If you already mark frames, increment counters, and wrap work in scopes, you get the derived tracks for free at flush.
```cpp
TRACE_SET_OUTPUT_PATH("trace.json");
OTRACE_ENABLE_SYNTH_TRACKS(true);

for (int f = 0; f != 240; ++f) {
  TRACE_MARK_FRAME(f);
  TRACE_COUNTER("items_processed", total++);
  { TRACE_SCOPE("work"); /* …heavy work… */ }
}

TRACE_FLUSH(nullptr);
```
Opening the file in Perfetto shows your original rows plus an `fps` counter, a `rate(items_processed)` counter, and a `latency(work)` instant with percentile keys.

## Output shapes

Everything remains Chrome Trace compatible. The FPS row is a counter with a single series.
```json
{ "ph":"C","name":"fps","cat":"synth","pid":1234,"tid":0,"ts":1000000,
  "args":{"fps":58.7} }
```
A derivative of a counter named `"items_processed"` becomes another counter row.
```json
{ "ph":"C","name":"rate(items_processed)","cat":"synth","pid":1234,"tid":0,"ts":1010000,
  "args":{"value": 530.2} }
```

Latency summaries are instants at the end of the file with percentile keys in `args`. Values are in milliseconds; the trace’s time base is still microseconds internally.
```json
{ "ph":"I","name":"latency(work)","cat":"synth","pid":1234,"tid":0,"ts":LAST_TS,
  "s":"t","args":{"p50":0.37,"p95":0.91,"p99":1.42} }
```

## Behavior, edge conditions, and determinism

All synthesis happens after the recorder has collected and time-sorted the committed events. Extra events are appended and then the whole set is stably re-sorted by timestamp, then by thread id, then by the original per-thread sequence number, so the output is deterministic for a fixed input. Missing inputs simply produce no output: no frames means no `fps`, a counter with fewer than two samples has no `rate(...)`, and a scope name that never closes has no latency summary. The rate window is inclusive of the current sample’s timestamp on the right edge and slides forward monotonically; short windows on bursty series produce spiky derivatives by design. The percentile selection intentionally uses floor based on zero-indexed rank with no interpolation because it is stable and branch-free at this scale.

## Costs

Recording cost is unchanged because synthesis never runs in the recording path. Flush cost grows with the number of frames, counter samples, and completed scopes; the implementation uses linear passes and a single additional stable sort after appending synthetic events. If you are close to end-to-end time limits, disable the synthesizer for those runs or narrow the workload using the existing filter and sampling gates. Memory overhead is proportional to the number of temporary values needed to compute percentiles and rates; everything is released after the file is written.

## Interop with other options

Rotation and gzip work as before because the writer only sees a larger vector of events. Environment-driven enable/disable and CSV category filters affect recording only; synthesized rows are not subject to those gates and always use category `"synth"`. The clock choice does not change semantics; if you select the RDTSC time base the same microsecond deltas are used for rates and latency math.

## Troubleshooting

If you compile with `-DOTRACE_SYNTHESIZE_TRACKS=1` and see no synthetic rows, confirm you actually toggled the runtime switch on with `OTRACE_ENABLE_SYNTH_TRACKS(true)` at some point before `TRACE_FLUSH`. If you rely on FPS but your frame markers use a different name or category, rename them to `TRACE_MARK_FRAME(...)` or emit `TRACE_INSTANT_C("frame","frame")` to match the synthesizer’s selection logic. If your counters are multi-series and you were expecting derivatives of a non-primary series, split that series into its own `TRACE_COUNTER("name", v)` call so the synthesizer can pick it up.
