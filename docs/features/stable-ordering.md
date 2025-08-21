# Deterministic ordering for same-timestamp events

otrace now tracks a **per-thread sequence number** on each event and uses it during flush sorting. The final order is:
`(ts_us, tid, seq)`

This guarantees that events with the same timestamp appear in the order they were appended on that thread, making traces reproducible across runs where timer quantization hits multiple events.

## What it means in practice

- Two zero-duration instants emitted back-to-back at the same microsecond keep their program order.
- Frame markers and immediately adjacent slices don’t flip-flop when timestamps collide.

No user action required—this is automatic.

## Notes

- The sequence is **per thread** and resets only when a new thread touches otrace for the first time.
- The viewer doesn’t show `seq`; it’s purely used for tie-breaking during JSON output.
