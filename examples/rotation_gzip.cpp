// Build (plain): c++ -std=c++17 -O2 -pthread -DOTRACE=1 examples/rotation_gzip.cpp -o ex_rotate
// Build (gzip or minizip):  c++ -std=c++17 -O2 -pthread -DOTRACE=1 -DOTRACE_USE_ZLIB=1 examples/rotation_gzip.cpp -o ex_rotate
#include "otrace.hpp"

#include <thread>
#include <chrono>
using namespace std::chrono_literals;

int main() {
  TRACE_SET_PROCESS_NAME("ex-rotation");

  // Rotating plain JSON files
  TRACE_SET_OUTPUT_PATTERN("traces_json/run-%03u.json", 1, 4); // ~1MB advisory, 4 files
  for (int i = 0; i < 600; ++i) {
    TRACE_INSTANT_CKV("emit", "io", "i", i);
    if ((i % 50) == 0) std::this_thread::sleep_for(2ms);
  }
  TRACE_FLUSH(nullptr);

  // Rotating gzip (if built with zlib/miniz; else still writes plain .json)
  TRACE_SET_OUTPUT_PATTERN("traces_gz/run-%03u.json.gz", 1, 3);
  for (int i = 0; i < 600; ++i) {
    TRACE_INSTANT_CKV("emit_gz", "io", "i", i);
    if ((i % 50) == 0) std::this_thread::sleep_for(2ms);
  }
  TRACE_FLUSH(nullptr);

  // Back to single-file mode (optional)
  TRACE_SET_OUTPUT_PATH("rotation_tail.json");
  TRACE_INSTANT("done");

  TRACE_FLUSH(nullptr);
  return 0;
}
