// Build: c++ -std=c++17 -O2 -g -pthread -rdynamic -DOTRACE=1 -DOTRACE_HEAP=1 -DOTRACE_HEAP_STACKS=1 -DOTRACE_DEFINE_HEAP_HOOKS=1 examples/heap_demo.cpp -o heap_demo

#include "otrace.hpp"
#include <vector>
#include <thread>
#include <chrono>

int main() {
  TRACE_SET_OUTPUT_PATH("ex_heap.json");
  OTRACE_ENABLE();

  TRACE_INSTANT("program_start");

  OTRACE_HEAP_SET_SAMPLING(1.0);
  OTRACE_HEAP_ENABLE(true);

  std::vector<char*> keep;
  for (int i = 0; i < 80; ++i) keep.push_back(new char[1 << 13]);
  (void)new char[1536];
  (void)new char[4096];

  OTRACE_HEAP_SET_SAMPLING(0.0);     // keep heap enabled, quiet hooks for report
  OTRACE_HEAP_REPORT();
  TRACE_INSTANT("report_done");

  TRACE_FLUSH(nullptr);
  OTRACE_HEAP_ENABLE(false);
  OTRACE_DISABLE();
}
