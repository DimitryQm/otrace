// Build: c++ -std=c++17 -O2 -pthread -DOTRACE=1 examples/edge_cases.cpp -o ex_edges
#include "otrace.hpp"

int main() {
  TRACE_SET_PROCESS_NAME("ex-edges");
  TRACE_SET_OUTPUT_PATH("edges.json");

  TRACE_INSTANT("very_very_long_name_that_pushes_limits_but_is_kept");
  TRACE_INSTANT_C("spaces in category ok", "category with spaces");
  TRACE_INSTANT_CKV("quotes_and_backslashes", "test",
                    "value", 3.14, "quote", "\"hi\"", "slash", "\\ok");

  TRACE_DISABLE();
  TRACE_INSTANT("should_not_appear");
  TRACE_ENABLE();
  TRACE_INSTANT("back_on");

  TRACE_FLUSH(nullptr);
  return 0;
}
