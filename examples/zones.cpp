// Build: c++ -std=c++17 -O2 -pthread -DOTRACE=1 examples/zones.cpp -o ex_zone
#include "otrace.hpp"
#include <thread>
#include <chrono>
using namespace std::chrono_literals;

int main() {
  TRACE_SET_PROCESS_NAME("ex-zone");
  TRACE_SET_OUTPUT_PATH("zone.json");
  { TRACE_ZONE("compile"); std::this_thread::sleep_for(5ms); }
  { TRACE_ZONE("link");    std::this_thread::sleep_for(7ms); }
  TRACE_FLUSH(nullptr);
}
