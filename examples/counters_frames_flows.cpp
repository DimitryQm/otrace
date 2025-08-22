// Build: c++ -std=c++17 -O2 -pthread -DOTRACE=1 examples/counters_frames_flows.cpp -o ex_counts
#include "otrace.hpp"

#include <thread>
#include <chrono>
using namespace std::chrono_literals;

int main() {
  TRACE_SET_PROCESS_NAME("ex-counters-frames-flows");
  TRACE_SET_OUTPUT_PATH("counts_frames_flows.json");

  // A rising counter and a few frames
  int total = 0;
  for (int f = 0; f < 30; ++f) {
    TRACE_COUNTER("items_processed", total += 5);
    TRACE_MARK_FRAME(f);
    if (f % 3 == 0) TRACE_MARK_FRAME_S("present");
    std::this_thread::sleep_for(4ms);
  }

  // A flow: id hops across instants
  const uint64_t flow = 0xC0FFEEu;
  TRACE_FLOW_BEGIN(flow);
  std::this_thread::sleep_for(2ms);
  TRACE_FLOW_STEP(flow);
  std::this_thread::sleep_for(2ms);
  TRACE_FLOW_END(flow);

  TRACE_FLUSH(nullptr);
  return 0;
}
