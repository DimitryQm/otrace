// Build: c++ -std=c++17 -O2 -pthread -DOTRACE=1 examples/filters_sampling.cpp -o ex_filters
#include "otrace.hpp"

#include <thread>
#include <chrono>
#include <cstring>
using namespace std::chrono_literals;

int main() {
  TRACE_SET_PROCESS_NAME("ex-filters");
  TRACE_SET_OUTPUT_PATH("filters.json");

  // Allow only important + frame categories; deny debug
  OTRACE_ENABLE_CATS("important,frame");
  OTRACE_DISABLE_CATS("debug");

  TRACE_INSTANT_C("will-keep", "important");
  TRACE_INSTANT_C("will-drop", "debug");

  // Predicate filter: keep events whose name contains "snap"
  OTRACE_SET_FILTER(+[](const char* name, const char*) {
    return name && std::strstr(name, "snap");
  });
  TRACE_INSTANT("snapshot");      // kept
  TRACE_INSTANT("heartbeat");     // dropped
  OTRACE_SET_FILTER(nullptr);

  // Probabilistic keep gate for volume control
  OTRACE_SET_SAMPLING(0.3);
  for (int i = 0; i < 50; ++i) TRACE_INSTANT("sampled");
  OTRACE_SET_SAMPLING(1.0);

  // reset gates for rest of the program
  OTRACE_ENABLE_CATS("");
  OTRACE_DISABLE_CATS("");

  TRACE_FLUSH(nullptr);
  return 0;
}
