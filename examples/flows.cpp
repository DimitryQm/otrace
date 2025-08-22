#include "otrace.hpp"
#include <thread>
#include <chrono>
#include <cstdint>

static void stage(const char* name, uint64_t id, int ms) {
  TRACE_SCOPE(name);
  TRACE_FLOW_STEP(id);
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

int main() {
  TRACE_SET_OUTPUT_PATH("ex_flows.json");
  OTRACE_ENABLE();

  for (uint64_t id = 100; id < 106; ++id) {
    TRACE_FLOW_BEGIN(id);
    stage("decode", id, 3);
    stage("transform", id, 4);
    stage("upload", id, 5);
    TRACE_FLOW_END(id);
  }

  TRACE_FLUSH(nullptr);
}
