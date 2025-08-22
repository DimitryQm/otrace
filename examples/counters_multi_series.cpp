// Build: c++ -std=c++17 -O2 -pthread -DOTRACE=1 examples/counters_multi_series.cpp -o ex_cmulti
#include "otrace.hpp"

#include <thread>
#include <chrono>
using namespace std::chrono_literals;

int main() {
  TRACE_SET_PROCESS_NAME("ex-counters-multi");
  TRACE_SET_OUTPUT_PATH("counters_multi.json");

  // Two-series counter: emits a single row "dual" with two series "x" and "y".
  for (int i = 0; i < 40; ++i) {
    TRACE_COUNTER2("dual", "x", i, "y", i*i);
    std::this_thread::sleep_for(2ms);
  }

  // Three-series counter: same row name, three series.
  for (int i = 0; i < 40; ++i) {
    TRACE_COUNTER3("triple", "a", i, "b", i+1, "c", i+2);
    std::this_thread::sleep_for(2ms);
  }

  // Categorized counter goes under its own category in Perfetto.
  for (int i = 0; i < 20; ++i) {
    TRACE_COUNTER_C("bytes_sent", "net", i * 4096);
    std::this_thread::sleep_for(3ms);
  }

  TRACE_FLUSH(nullptr);
  return 0;
}
