#include "otrace.hpp"
#include <thread>
#include <chrono>

int main() {
  TRACE_SET_OUTPUT_PATH("ex_basic_scopes.json");
  OTRACE_ENABLE();

  { // RAII scope
    TRACE_SCOPE("outer");
    std::this_thread::sleep_for(std::chrono::milliseconds(4));
    {
      TRACE_SCOPE_C("parse_cfg", "io");
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      TRACE_INSTANT_KV("cfg_version", "v", 3.2);
    }
    TRACE_INSTANT_CKV("boot_tag", "boot", "phase", 1, "mode", "cold");
  }

  // begin/end pair
  TRACE_BEGIN("connect");
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  TRACE_END("connect");

  TRACE_FLUSH(nullptr);
}
