#include "otrace.hpp"
#include <thread>
#include <chrono>

int main() {
  TRACE_SET_OUTPUT_PATH("ex_frames.json");
  OTRACE_ENABLE();

  for (int f = 0; f < 120; ++f) {
    if (f % 3 == 0) TRACE_COLOR("good");
    if (f % 3 == 1) TRACE_COLOR("bad");
    if (f % 3 == 2) TRACE_COLOR("terrible");
    TRACE_MARK_FRAME(f);
    std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 fps
  }

  TRACE_FLUSH(nullptr);
}
