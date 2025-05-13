
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <iostream>
#include <thread>

import coroutini_threadpool;

using namespace coroutini::threadpool;
using namespace std::chrono_literals;

using Clock = std::chrono::steady_clock;

TEST_CASE("ThreadPool", "[threadpool]") {
  ThreadPool thread_pool(1);

  Clock::time_point start_time = Clock::now();

  auto print_time = [start_time](char const* message = "") {
    std::cout << message
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                     Clock::now() - start_time)
              << std::endl;
  };

  thread_pool.add_task([]() {}, 1s);
  thread_pool.wait_for_tasks();

  print_time("test finished in ");
}
