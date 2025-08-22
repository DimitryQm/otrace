// Build: c++ -std=c++17 -O2 -pthread -DOTRACE=1 -DOTRACE_SYNTHESIZE_TRACKS=1  examples/synth_tracks.cpp -o ex_synth
#include "otrace.hpp"

#include <thread>
#include <chrono>
using namespace std::chrono_literals;

int main() {
  TRACE_SET_PROCESS_NAME("ex-synth");
  TRACE_SET_OUTPUT_PATH("synth.json");
  OTRACE_ENABLE_SYNTH_TRACKS(true);

  // FPS from frames
  for (int f = 0; f < 60; ++f) { TRACE_MARK_FRAME(f); std::this_thread::sleep_for(16ms); }

  // Derivative of a counter
  int acc = 0;
  for (int i = 0; i < 40; ++i) { TRACE_COUNTER("bytes_uploaded", acc += 1024); std::this_thread::sleep_for(10ms); }

  // Latency percentiles for a scope
  for (int i = 0; i < 30; ++i) { TRACE_SCOPE("tile"); std::this_thread::sleep_for(std::chrono::microseconds(300 + i*50)); }

  TRACE_FLUSH(nullptr);
  return 0;
}
