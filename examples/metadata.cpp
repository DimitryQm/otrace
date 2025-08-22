#include "otrace.hpp"
#include <thread>
#include <chrono>

int main() {
  TRACE_SET_OUTPUT_PATH("ex_metadata.json");
  OTRACE_ENABLE();

  TRACE_SET_PROCESS_NAME("demo-app");
  TRACE_SET_THREAD_NAME("main");
  TRACE_SET_THREAD_SORT_INDEX(5);

  TRACE_INSTANT("meta_touch");
  { TRACE_SCOPE_C("read_config", "io"); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }

  TRACE_FLUSH(nullptr);
}
