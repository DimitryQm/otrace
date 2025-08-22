#include "otrace.hpp"
#include <thread>
#include <chrono>
#include <cmath>

int main() {
  TRACE_SET_OUTPUT_PATH("ex_counters.json");
  OTRACE_ENABLE();

  double t = 0.0;
  for (int i = 0; i < 120; ++i) {
    double load = 50.0 + 40.0 * std::sin(t);
    TRACE_COUNTER("cpu_load_pct", (int)load);
    TRACE_COUNTER("queue_depth", i % 17);
    t += 0.12;
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
  }

  TRACE_FLUSH(nullptr);
}
