// Build: c++ -std=c++17 -O2 -pthread -DOTRACE=1 examples/env_bootstrap.cpp -o ex_env
#include "otrace.hpp"

#if defined(_WIN32)
  #include <cstdlib>
#else
  #include <stdlib.h>
#endif
#include <iostream>

int main() {
  TRACE_SET_PROCESS_NAME("ex-env");
  TRACE_SET_OUTPUT_PATH("env.json");

#if defined(_WIN32)
  _putenv_s("OTRACE_DISABLE", "1");
  _putenv_s("OTRACE_ENABLE",  "1");   // wins over DISABLE
  _putenv_s("OTRACE_SAMPLE",  "1.0");
#else
  setenv("OTRACE_DISABLE", "1", 1);
  setenv("OTRACE_ENABLE",  "1", 1);
  setenv("OTRACE_SAMPLE",  "1.0", 1);
#endif

  OTRACE_TOUCH(); // forces env read on first use
  std::cout << "enabled? " << (TRACE_IS_ENABLED() ? "yes" : "no") << "\n";

  TRACE_INSTANT("env_checked");
  TRACE_FLUSH(nullptr);
  return 0;
}
