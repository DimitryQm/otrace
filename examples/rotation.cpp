#include "otrace.hpp"
#include <thread>
#include <chrono>

int main() {
  TRACE_SET_OUTPUT_PATH("ex_rotation_single.json");
  OTRACE_ENABLE();

  // rotate to multiple files; with zlib/miniz enabled you can use .json.gz
  TRACE_SET_OUTPUT_PATTERN("ex_rot/run-%03u.json", 1 /*MB*/, 4 /*files*/);
  for (int i = 0; i < 2000; ++i) {
    TRACE_INSTANT_KV("blob", "i", i);
    if (i % 50 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  TRACE_FLUSH(nullptr);

  TRACE_SET_OUTPUT_PATTERN("", 0, 0); // back to single-file mode
  TRACE_FLUSH(nullptr);
}
