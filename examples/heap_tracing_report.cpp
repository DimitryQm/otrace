// Build: c++ -std=c++17 -O2 -pthread -rdynamic \
//        -DOTRACE=1 -DOTRACE_HEAP=1 -DOTRACE_DEFINE_HEAP_HOOKS=1 -DOTRACE_HEAP_STACKS=1 \
//        examples/heap_tracing_report.cpp -o ex_heap
#include "otrace.hpp"

#include <vector>

int main() {
  TRACE_SET_PROCESS_NAME("ex-heap");
  TRACE_SET_OUTPUT_PATH("heap_demo.json");
  TRACE_INSTANT("program_start");

  OTRACE_HEAP_SET_SAMPLING(1.0);    // guarantee attribution in this window
  OTRACE_HEAP_ENABLE(true);

  std::vector<char*> hold;
  for (int i = 0; i < 120; ++i) hold.push_back(new char[1 << 14]); // retained
  (void)new char[1024];   // intentional leaks
  (void)new char[2048];

  OTRACE_HEAP_SET_SAMPLING(0.0);    // keep heap enabled; quiet hooks during report
  OTRACE_HEAP_REPORT();             // emits heap_report_stats / heap_leaks / heap_sites
  TRACE_INSTANT("report_done");

  TRACE_FLUSH(nullptr);
  OTRACE_HEAP_ENABLE(false);
  return 0;
}
