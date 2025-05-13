#include <atomic>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <latch>
#include <mutex>
#include <numeric>
#include <shared_mutex>
#include <thread>
#include <vector>

import coroutini_utils;

namespace {

using namespace coroutini::utils;
using namespace std::chrono_literals;

constexpr static char const TEST_CLOCK_NAME[] =
    "coroutini/tests/utils/manual_clock";

template <std::size_t id, char const* name = TEST_CLOCK_NAME>
using TestClock = DefaultManualClock<id, TEST_CLOCK_NAME>;

TEST_CASE("ManualClock -- setting and adding time", "[utils][manual_clock]") {
  using Clock = TestClock<__LINE__>;
  using time_point = Clock::time_point;

  Clock::reset();

  CHECK(Clock::now().time_since_epoch() == 0s);

  Clock::set_time(time_point(1s));

  CHECK(Clock::now().time_since_epoch() == 1s);

  Clock::add_time(2s);

  CHECK(Clock::now().time_since_epoch() == 3s);

  Clock::reset();

  CHECK(Clock::now().time_since_epoch() == 0s);
}

struct ChooseConditionVariable {
  template <class Clock>
  using condition_variable = Clock::condition_variable;
};

struct ChooseConditionVariableAny {
  template <class Clock>
  using condition_variable = Clock::condition_variable_any;
};

TEMPLATE_TEST_CASE(
    "ManualClock -- condition_variable and condition_variable_any",
    "[utils][manual_clock][condition_variable][condition_variable_any]",
    ChooseConditionVariable, ChooseConditionVariableAny) {
  SECTION("wait") {
    using Clock = TestClock<__LINE__>;
    using condition_variable = TestType::template condition_variable<Clock>;

    constexpr static std::size_t num_workers = 5;

    std::vector<int> expected;

    std::mutex mutex;
    std::vector<int> result;
    std::size_t worker_index = -1;
    condition_variable switch_worker;
    condition_variable work_done;

    auto worker_function = [&](std::size_t index) {
      std::unique_lock lock{mutex};
      switch_worker.wait(lock, [&]() { return index == worker_index; });
      result.emplace_back(index);
      work_done.notify_one();
    };

    std::vector<std::jthread> workers;
    for (std::size_t i = 0; i < num_workers; ++i) {
      workers.emplace_back(worker_function, i);
    }

    for (std::size_t i = num_workers; i-- > 0;) {
      expected.emplace_back(i);
      std::unique_lock lock{mutex};
      worker_index = i;
      std::size_t num_completed = result.size();
      switch_worker.notify_all();
      work_done.wait(lock, [&]() { return result.size() > num_completed; });
    }
    CHECK(result == expected);
  };

  SECTION("wait_until timeout") {
    using Clock = TestClock<__LINE__>;
    using condition_variable = TestType::template condition_variable<Clock>;

    Clock::reset();

    constexpr static std::size_t num_workers = 5;

    std::mutex mutex;
    std::uint64_t result = 0;
    std::uint64_t old_result = result;
    condition_variable never_happens;
    condition_variable work_done;

    auto start_time = Clock::now();

    auto worker_function = [&](std::size_t index) {
      std::unique_lock lock{mutex};
      auto wait_result = never_happens.wait_until(
          lock, start_time + 1s * (index + 1), []() { return false; });
      CHECK(!wait_result);  // The wait must time out.
      // The work is setting a bit to 1.
      result |= 1 << index;
      work_done.notify_one();
    };

    std::vector<std::jthread> workers;
    for (std::size_t i = 0; i < num_workers; ++i) {
      workers.emplace_back(worker_function, i);
    }

    for (std::size_t i = 0; i < num_workers; ++i) {
      std::unique_lock lock{mutex};
      Clock::add_time(1s);
      work_done.wait(lock, [&]() { return result != old_result; });
      old_result = result;
    }
    CHECK(result == (1 << num_workers) - 1);
  }

  SECTION("wait_until with and without timeout") {
    using Clock = TestClock<__LINE__>;
    using condition_variable = TestType::template condition_variable<Clock>;

    auto start_time = Clock::now();

    constexpr static std::size_t num_workers = 10;
    constexpr static std::size_t num_blocked_workers = 5;

    std::mutex mutex;
    std::uint64_t result = 0;
    std::uint64_t old_result = result;
    bool unblock = false;
    condition_variable unblock_change;
    condition_variable work_done;

    auto worker_function = [&](std::size_t index, bool can_unblock) {
      std::unique_lock lock{mutex};
      bool wait_result =
          unblock_change.wait_until(lock, start_time + 1s * (index + 1),
                                    [&]() { return can_unblock && unblock; });
      CHECK(wait_result == can_unblock);
      // The work is setting a bit to 1.
      result |= 1 << index;
      work_done.notify_one();
    };

    std::vector<std::jthread> workers;
    for (std::size_t i = 0; i < num_workers; ++i) {
      workers.emplace_back(worker_function, i, i >= num_blocked_workers);
    }

    {
      std::scoped_lock lock{mutex};
      CHECK(result == 0);
    }

    // Workers that cannot be unblocked will time out one at a time.
    for (std::size_t i = 0; i < num_blocked_workers; ++i) {
      std::unique_lock lock{mutex};
      Clock::add_time(1s);
      work_done.wait(lock, [&]() { return result != old_result; });
      old_result = result;
      CHECK(result == (1 << (i + 1)) - 1);
    }
    CHECK(result == (1 << num_blocked_workers) - 1);

    // Workers that can be unblocked will all unblock after notify_all.
    {
      std::unique_lock lock{mutex};
      unblock = true;
      unblock_change.notify_all();
      work_done.wait(lock, [&]() { return result == (1 << num_workers) - 1; });
    }
    CHECK(result == (1 << num_workers) - 1);
  }

  SECTION("wait_for with and without timeout") {
    using Clock = TestClock<__LINE__>;
    using condition_variable = TestType::template condition_variable<Clock>;

    constexpr static std::size_t num_workers = 10;
    constexpr static std::size_t num_blocked_workers = 5;

    std::mutex mutex;
    std::uint64_t result = 0;
    std::uint64_t old_result = result;
    bool unblock = false;
    condition_variable unblock_change;
    condition_variable work_done;
    std::latch workers_initialized{num_workers};

    auto worker_function = [&](std::size_t index, bool can_unblock) {
      std::unique_lock lock{mutex};
      workers_initialized.count_down();
      bool wait_result = unblock_change.wait_for(
          lock, 1s * (index + 1), [&]() { return can_unblock && unblock; });
      CHECK(wait_result == can_unblock);
      // The work is setting a bit to 1.
      result |= 1 << index;
      work_done.notify_one();
    };

    std::vector<std::jthread> workers;
    for (std::size_t i = 0; i < num_workers; ++i) {
      workers.emplace_back(worker_function, i, i >= num_blocked_workers);
    }

    workers_initialized.wait();

    {
      std::scoped_lock lock{mutex};
      CHECK(result == 0);
    }

    // Workers that cannot be unblocked will time out one at a time.
    for (std::size_t i = 0; i < num_blocked_workers; ++i) {
      std::unique_lock lock{mutex};
      Clock::add_time(1s);
      work_done.wait(lock, [&]() { return result != old_result; });
      old_result = result;
      CHECK(result == (1 << (i + 1)) - 1);
    }
    CHECK(result == (1 << num_blocked_workers) - 1);

    // Workers that can be unblocked will all unblock after notify_all.
    {
      std::unique_lock lock{mutex};
      unblock = true;
      unblock_change.notify_all();
      work_done.wait(lock, [&]() { return result == (1 << num_workers) - 1; });
    }
    CHECK(result == (1 << num_workers) - 1);
  }

  SECTION("wait with stop_token") {
    using Clock = TestClock<__LINE__>;
    using condition_variable_any = Clock::condition_variable_any;

    constexpr static std::size_t num_threads_to_request_stop = 5;
    constexpr static std::size_t num_threads_to_timeout = 5;
    constexpr static std::size_t num_threads_to_notify = 5;
    constexpr static std::size_t num_threads = num_threads_to_request_stop +
                                               num_threads_to_timeout +
                                               num_threads_to_notify;

    std::shared_mutex mutex;
    std::vector<int> results(num_threads, 0);
    std::vector<int> expected(num_threads, 0);
    bool ready = false;
    condition_variable_any check_if_ready;
    condition_variable_any work_done;
    std::atomic<std::size_t> num_stopped_threads = 0;
    std::latch workers_initialized{num_threads};

    auto worker_function = [&](std::stop_token stoken, std::size_t index) {
      std::shared_lock lock{mutex};
      workers_initialized.count_down();
      if (check_if_ready.wait_for(lock, stoken, 1s * (index + 1),
                                  [&]() { return ready; })) {
        results[index] = 1;
      } else if (stoken.stop_requested()) {
        results[index] = -1;
      } else {
        results[index] = 0;
      }
      num_stopped_threads.fetch_add(1, std::memory_order_relaxed);
      work_done.notify_one();
    };

    std::vector<std::jthread> workers;
    for (std::size_t i = 0; i < num_threads; ++i) {
      workers.emplace_back(worker_function, i);
    }

    workers_initialized.wait();

    std::unique_lock lock{mutex};
    {
      std::size_t i = 0;
      // First, request stop on the first num_threads_to_request_stop threads.
      for (; i < num_threads_to_request_stop; ++i) {
        workers[i].request_stop();
        expected[i] = -1;
      }
      check_if_ready.notify_all();
      work_done.wait(lock, [&]() {
        return num_stopped_threads.load(std::memory_order_relaxed) ==
               num_threads_to_request_stop;
      });

      // Second, move up the clock time to cause num_threads_to_timeout threads
      // to finish due to a timeout.
      constexpr static std::size_t num_threads_with_condition_not_satisfied =
          num_threads_to_request_stop + num_threads_to_timeout;
      for (; i < num_threads_with_condition_not_satisfied; ++i) {
        expected[i] = 0;
      }
      Clock::add_time(1s * num_threads_with_condition_not_satisfied);
      check_if_ready.notify_all();
      work_done.wait(lock, [&]() {
        return num_stopped_threads.load(std::memory_order_relaxed) ==
               num_threads_with_condition_not_satisfied;
      });

      // Finally, set ready to true and notify again.
      for (; i < num_threads; ++i) {
        expected[i] = 1;
      }
      ready = true;
      check_if_ready.notify_all();
      work_done.wait(lock, [&]() {
        return num_stopped_threads.load(std::memory_order_relaxed) ==
               num_threads;
      });
    }
    CHECK(results == expected);
  }
}

TEST_CASE("ManualClock -- condition_variable_any with non-basic mutexes",
          "[utils][manual_clock][condition_variable_any]") {
  SECTION("shared_mutex") {
    using Clock = TestClock<__LINE__>;
    using condition_variable_any = Clock::condition_variable_any;

    constexpr static std::size_t num_threads = 5;
    constexpr static std::size_t expected_count = 100;
    constexpr static std::size_t data_length = num_threads * expected_count;
    constexpr static std::size_t write_batch_size = 11;

    std::shared_mutex mutex;
    condition_variable_any data_written;
    condition_variable_any data_read;
    std::vector<int> data;
    std::vector<std::size_t> counts(num_threads, 0);

    auto reader_func = [&](std::stop_token stoken, std::size_t modulus) {
      std::shared_lock lock{mutex};
      std::size_t last_read_index = 0;
      while (!stoken.stop_requested()) {
        if (!data_written.wait(lock, stoken, [&]() {
              return data.size() > last_read_index;
            })) {
          break;
        }
        for (std::size_t i = last_read_index; i < data.size(); ++i) {
          if (i % int(num_threads) == int(modulus)) {
            ++counts[modulus];
          }
        }
        last_read_index = data.size();
        data_read.notify_one();
      }
    };

    std::vector<std::jthread> readers;
    for (std::size_t i = 0; i < num_threads; ++i) {
      readers.emplace_back(reader_func, i);
    }

    std::unique_lock write_lock{mutex};
    for (std::size_t i = 0; i < data_length;) {
      std::size_t current_batch_size =
          std::min(write_batch_size, data_length - i);
      for (std::size_t j = 0; j < current_batch_size; ++j) {
        data.emplace_back(i);
        ++i;
      }
      data_written.notify_all();
      data_read.wait(write_lock, [&]() {
        std::size_t count_sum = std::reduce(counts.begin(), counts.end());
        return count_sum == i;
      });
    }

    std::size_t count_sum = std::reduce(counts.begin(), counts.end());
    CHECK(count_sum == data_length);
    for (auto& thread : readers) {
      thread.request_stop();
    }
  };

  SECTION("recursive_mutex") {
    using Clock = TestClock<__LINE__>;
    using condition_variable_any = Clock::condition_variable_any;

    std::recursive_mutex mutex;
    condition_variable_any ready_to_write;
    condition_variable_any counter_updated;
    std::size_t counter = 0;

    constexpr static std::size_t num_levels = 5;
    auto worker_function_helper = [&](auto& next, std::size_t index) {
      std::unique_lock lock{mutex};
      if (index == num_levels) {
        return;
      }
      ready_to_write.wait(lock, [&]() { return counter == index; });
      ++counter;
      counter_updated.notify_one();
      next(next, counter);
    };
    auto worker_function = [&](std::size_t index) {
      return worker_function_helper(worker_function_helper, index);
    };

    std::jthread worker(worker_function, 0);

    std::unique_lock lock{mutex};
    for (std::size_t i = 0; i < num_levels; ++i) {
      counter_updated.wait(lock, [&]() { return counter > i; });
      ready_to_write.notify_one();
    }

    CHECK(counter == num_levels);
  };
}

TEST_CASE("ManualClock -- periodic_broadcaster", "[utils][manual_clock]") {
  using Clock = TestClock<__LINE__>;
  using condition_variable = Clock::condition_variable;
  using condition_variable_any = Clock::condition_variable_any;

  // The purpose of this test case is to verify that clock_broadcaster will fix
  // the issue of indefinite wait due to missing a condition variable
  // notification after the clock time changes.
  // If the line below is commented out, this test case may get stuck. (You
  // might need to run it many times before you see that happen.)
  auto clock_broadcaster = Clock::periodic_broadcaster(10ms);

  SECTION("condition_variable") {
    auto start_time = Clock::now();
    std::mutex mutex;
    std::unique_lock lock{mutex};
    condition_variable cv;
    std::jthread waiter{[&]() {
      cv.wait_until(lock, start_time + 1s, [&]() { return false; });
    }};

    // In the absence of clock_broadcaster, this sleep_for call increases the
    // likelihood of this test case getting stuck.
    // If you want to experiment with the absence of clock_broadcaster, you
    // might need to adjust the amount of sleep time because it can be
    // machine-dependent.
    std::this_thread::sleep_for(1ns);
    Clock::add_time(1s);
  };
  SECTION("condition_variable_any") {
    auto start_time = Clock::now();
    std::shared_mutex mutex;
    std::shared_lock lock{mutex};
    condition_variable_any cv;
    std::jthread waiter{[&]() {
      cv.wait_until(lock, start_time + 1s, [&]() { return false; });
    }};

    // In the absence of clock_broadcaster, this sleep_for call increases the
    // likelihood of this test case getting stuck.
    // If you want to experiment with the absence of clock_broadcaster, you
    // might need to adjust the amount of sleep time because it can be
    // machine-dependent.
    std::this_thread::sleep_for(1ns);
    Clock::add_time(1s);
  };
}

}  // namespace
