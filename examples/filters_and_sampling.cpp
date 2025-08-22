#include "otrace.hpp"
#include <thread>
#include <chrono>
#include <cstring>

int main() {
  TRACE_SET_OUTPUT_PATH("ex_filters.json");
  OTRACE_ENABLE();

  OTRACE_ENABLE_CATS("net,frame");
  OTRACE_DISABLE_CATS("noise");
  OTRACE_SET_SAMPLING(0.5);

  TRACE_INSTANT_C("tick", "net");     // kept
  TRACE_INSTANT_C("dbg", "noise");    // dropped
  TRACE_INSTANT_C("paint", "frame");  // kept

  OTRACE_ENABLE_CATS("");             // reset gates
  OTRACE_DISABLE_CATS("");
  OTRACE_SET_SAMPLING(1.0);

  // custom predicate: keep only names containing "hot"
  OTRACE_SET_FILTER(+[](const char* name, const char* cat){
    (void)cat; return name && std::strstr(name, "hot");
  });
  TRACE_INSTANT("hot_path");
  TRACE_INSTANT("cold_path");
  OTRACE_SET_FILTER(nullptr);

  TRACE_FLUSH(nullptr);
}
