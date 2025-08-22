#include "otrace.hpp"

#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>

struct Job { uint64_t id; int payload; };

int main() {
  TRACE_SET_PROCESS_NAME("otrace-showcase");
  TRACE_SET_OUTPUT_PATH("trace.json");

  {
    TRACE_SCOPE("startup");
    std::this_thread::sleep_for(std::chrono::milliseconds(12));
    TRACE_INSTANT_C("tick", "boot");
  }

  std::queue<Job> q;
  std::mutex m;
  std::condition_variable cv;
  bool done = false;

  auto producer = std::thread([&]{
    TRACE_SET_THREAD_NAME("producer");
    TRACE_SET_THREAD_SORT_INDEX(10);

    for (int i = 0; i < 12; ++i) {
      TRACE_SCOPE_CKV("make_job", "compute", "i", i);
      std::this_thread::sleep_for(std::chrono::milliseconds(3 + (i % 2)));
      Job j { (uint64_t)i, i };
      TRACE_FLOW_BEGIN(j.id);
      {
        std::lock_guard<std::mutex> lk(m);
        q.push(j);
        TRACE_COUNTER("queue_len", (int)q.size());
      }
      cv.notify_one();
      TRACE_MARK_FRAME(i);
    }

    { std::lock_guard<std::mutex> lk(m); done = true; }
    cv.notify_all();
    TRACE_MARK_FRAME_S("present");
  });

  auto consumer = std::thread([&]{
    TRACE_SET_THREAD_NAME("consumer");
    TRACE_SET_THREAD_SORT_INDEX(20);

    while (true) {
      Job j{};
      {
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk, [&]{ return done || !q.empty(); });
        if (q.empty()) break;
        j = q.front(); q.pop();
        TRACE_COUNTER("queue_len", (int)q.size());
      }

      TRACE_COLOR("good");
      TRACE_SCOPE_CKV("process", "io", "job", (double)j.id);
      TRACE_FLOW_STEP(j.id);
      std::this_thread::sleep_for(std::chrono::milliseconds(2 + (j.payload % 3)));
      TRACE_FLOW_END(j.id);
    }

    TRACE_INSTANT_C("tick", "shutdown");
  });

  producer.join();
  consumer.join();

  TRACE_FLUSH(nullptr);
  return 0;
}
