// Build: c++ -std=c++17 -O2 -pthread -DOTRACE=1 examples/basics_scopes_instants.cpp -o ex_basics
#include "otrace.hpp"

#include <thread>
#include <chrono>
using namespace std::chrono_literals;

int main() {
  TRACE_SET_PROCESS_NAME("ex-basics");
  TRACE_SET_OUTPUT_PATH("basics.json");

  { // simple RAII scope + inner work
    TRACE_SCOPE("startup");
    std::this_thread::sleep_for(8ms);
    TRACE_INSTANT("ready");
  }

  // explicit begin/end pair
  TRACE_BEGIN("step-A");
  std::this_thread::sleep_for(3ms);
  TRACE_END("step-A");

  // variadic, type-aware instants (numbers + strings)
  TRACE_INSTANT_KV ("speed", "mps", 12.5);
  TRACE_INSTANT_CKV("tick", "frame", "phase", 2, "stage", "copy", "ok", 1);

  TRACE_FLUSH(nullptr);
  return 0;
}
