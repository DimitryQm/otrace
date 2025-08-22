// Build: c++ -std=c++17 -O2 -pthread -DOTRACE=1 examples/metadata_colors.cpp -o ex_meta
#include "otrace.hpp"

#include <thread>
#include <chrono>
using namespace std::chrono_literals;

int main() {
  TRACE_SET_PROCESS_NAME("ex-metadata");
  TRACE_SET_OUTPUT_PATH("metadata.json");
  TRACE_SET_THREAD_NAME("main-thread");
  TRACE_SET_THREAD_SORT_INDEX(5);

  TRACE_COLOR("good");   // affects next event only
  TRACE_INSTANT("startup");

  std::thread t([]{
    TRACE_SET_THREAD_NAME("worker-A");
    TRACE_SET_THREAD_SORT_INDEX(20);
    TRACE_COLOR("bad");
    TRACE_INSTANT("work-start");
  });

  t.join();
  TRACE_FLUSH(nullptr);
  return 0;
}
