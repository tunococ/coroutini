
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <iostream>
#include <memory>

import coroutini_utils;
import coroutini_threadpool;

namespace {

using namespace std::chrono_literals;

constexpr static char const TEST_CLOCK_NAME[] =
    "coroutini/tests/thread_pool/thread_pool";

template <std::size_t id>
using TestClock = coroutini::utils::DefaultManualClock<id, TEST_CLOCK_NAME>;

TEST_CASE("ThreadPool", "[threadpool]") {
  SECTION("simple test") {
    using Clock = TestClock<__LINE__>;
    using ThreadPool =
        coroutini::threadpool::ThreadPool<std::allocator<std::byte>, Clock,
                                          Clock::condition_variable>;

    Clock::time_point start_time = Clock::now();

    ThreadPool thread_pool(1);

    thread_pool.add_task([]() {}, 1s);

    Clock::add_time(1s);

    thread_pool.wait_for_tasks();
  };
}

}  // namespace
