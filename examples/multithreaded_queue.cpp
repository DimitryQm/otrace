// Build: c++ -std=c++17 -O2 -pthread -DOTRACE=1 examples/multithreaded_queue.cpp -o ex_mtq
#include "otrace.hpp"

#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
using namespace std::chrono_literals;

struct Item { int id; };

int main() {
  TRACE_SET_PROCESS_NAME("ex-mt-queue");
  TRACE_SET_OUTPUT_PATH("mt_queue.json");

  std::queue<Item> q;
  std::mutex m;
  std::condition_variable cv;
  bool done = false;

  std::thread prod([&]{
    TRACE_SET_THREAD_NAME("prod");
    TRACE_SET_THREAD_SORT_INDEX(10);
    for (int i=0;i<40;++i) {
      { TRACE_SCOPE("produce"); std::this_thread::sleep_for(1ms); }
      { std::lock_guard<std::mutex> lk(m); q.push({i}); TRACE_COUNTER("q_len", (int)q.size()); }
      cv.notify_one();
    }
    { std::lock_guard<std::mutex> lk(m); done = true; }
    cv.notify_all();
  });

  std::thread consA([&]{
    TRACE_SET_THREAD_NAME("consA");
    TRACE_SET_THREAD_SORT_INDEX(20);
    while (true) {
      Item it{};
      {
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk,[&]{ return done || !q.empty(); });
        if (q.empty()) break;
        it = q.front(); q.pop(); TRACE_COUNTER("q_len", (int)q.size());
      }
      { TRACE_SCOPE_CKV("consume", "io", "id", it.id); std::this_thread::sleep_for(2ms); }
    }
  });

  std::thread consB([&]{
    TRACE_SET_THREAD_NAME("consB");
    TRACE_SET_THREAD_SORT_INDEX(21);
    while (true) {
      Item it{};
      {
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk,[&]{ return done || !q.empty(); });
        if (q.empty()) break;
        it = q.front(); q.pop(); TRACE_COUNTER("q_len", (int)q.size());
      }
      { TRACE_SCOPE_CKV("consume", "io", "id", it.id); std::this_thread::sleep_for(1ms); }
    }
  });

  prod.join(); consA.join(); consB.join();
  TRACE_FLUSH(nullptr);
  return 0;
}
