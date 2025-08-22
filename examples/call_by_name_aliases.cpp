// Build: c++ -std=c++17 -O2 -pthread -DOTRACE=1 examples/call_by_name_aliases.cpp -o ex_call
#include "otrace.hpp"

int main() {
  TRACE_SET_PROCESS_NAME("ex-call");
  TRACE_SET_OUTPUT_PATH("call.json");

  OTRACE_CALL(SCOPE, "called_scope");
  OTRACE_CALL(INSTANT, "called_instant");
  OTRACE_CALL(COUNTER, "called_counter", 7);

#if !OTRACE_NO_SHORT_MACROS
  TRACE(SCOPE, "trace_scope");
  TRACE(INSTANT, "trace_instant");
  TRACE(COUNTER, "trace_counter", 99);
#endif

  TRACE_FLUSH(nullptr);
  return 0;
}
