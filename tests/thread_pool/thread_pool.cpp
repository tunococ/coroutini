#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <barrier>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <unordered_set>

import coroutini_utils;
import coroutini_threadpool;

namespace {

using namespace std::chrono_literals;

constexpr static char const TEST_CLOCK_NAME[] =
    "coroutini/tests/thread_pool/thread_pool";

template <std::size_t id>
using TestClock = coroutini::utils::DefaultManualClock<id, TEST_CLOCK_NAME>;

template <class Clock, class Lock, class ConditionVariable, class Pred>
bool wait_and_step_until(Lock& lock, ConditionVariable& cv,
                         typename Clock::time_point deadline, Pred pred,
                         typename Clock::duration const& step = 1s) {
  using time_point = Clock::time_point;
  using duration = Clock::duration;
  typename Lock::mutex_type& mutex = *lock.mutex();
  std::barrier barrier{2};
  bool done = false;
  bool result = false;
  std::jthread waiter([&]() {
    time_point previous_time = time_point::min();
    std::unique_lock lock{mutex};
    result = cv.wait_until(lock, deadline, [&]() {
      time_point now = Clock::now();
      // Prevent a spurious wakeup and a timeout from hitting the barrier.
      if (now == previous_time) {
        return false;
      }
      if (pred()) {
        return true;
      }
      if (now >= deadline) {
        return false;
      }
      previous_time = now;
      barrier.arrive_and_wait();
      return false;
    });
    done = true;
    barrier.arrive_and_wait();
  });
  lock.unlock();
  barrier.arrive_and_wait();
  lock.lock();
  while (!done) {
    Clock::add_time(step);
    lock.unlock();
    barrier.arrive_and_wait();
    lock.lock();
  }
  return result;
}

template <class Clock, class Lock, class ConditionVariable, class Pred>
bool wait_and_step(Lock& lock, ConditionVariable& cv, Pred pred,
                   typename Clock::duration const& step = 1s) {
  return wait_and_step_until<Clock>(lock, cv, Clock::time_point::max(),
                                    std::move(pred), step);
}

TEST_CASE("ThreadPool -- single thread", "[thread_pool]") {
  using Clock = TestClock<__LINE__>;
  using condition_variable = Clock::condition_variable;
  using time_point = Clock::time_point;
  using duration = Clock::duration;
  using ThreadPool =
      coroutini::threadpool::ThreadPool<std::allocator<std::byte>, Clock,
                                        condition_variable>;

  auto clock_broadcaster = Clock::periodic_broadcaster(10ms);
  ThreadPool thread_pool(1);

  Clock::time_point start_time = Clock::now();
  std::mutex mutex;
  std::unique_lock lock{mutex};

  SECTION("add_task") {
    std::size_t num_tasks_finished = 0;
    condition_variable task_finished;
    auto task = [&]() {
      // std::cout << std::format("trying to execute task...\n");
      std::scoped_lock lock{mutex};
      ++num_tasks_finished;
      // std::cout << std::format(
      //     "  task finished, num_tasks_finished={}, notifying...\n",
      //     num_tasks_finished);
      task_finished.notify_one();
    };
    thread_pool.add_task(60s, task);
    thread_pool.add_task(40s, task);
    thread_pool.add_task(20s, task);
    std::this_thread::yield();
    CHECK(thread_pool.get_num_pending_tasks() == 3);
    CHECK(num_tasks_finished == 0);

    thread_pool.add_task(-10s, task);
    task_finished.wait(lock, [&]() { return num_tasks_finished == 1; });

    CHECK(thread_pool.get_num_pending_tasks() == 3);
    thread_pool.add_task(start_time + 15s, task);
    thread_pool.add_task(start_time + 10s, task);
    thread_pool.add_task(start_time + 5s, task);
    thread_pool.add_task(start_time + 5s, task);
    CHECK(thread_pool.get_num_pending_tasks() == 7);
    // std::cout << std::format("about to add 10s\n");
    Clock::add_time(10s);
    // std::cout << std::format("added 10s\n");
    task_finished.wait(lock, [&]() {
      // std::cout << std::format("expected num_tasks_finished=4, actual={}\n",
      //                          num_tasks_finished);
      return num_tasks_finished == 4;
    });
    // std::cout << std::format("Waiting done\n");

    CHECK(thread_pool.get_num_pending_tasks() == 4);

    // std::cout << std::format("about to add 35s\n");
    Clock::add_time(35s);
    task_finished.wait(lock, [&]() { return num_tasks_finished == 7; });
    CHECK(thread_pool.get_num_pending_tasks() == 1);

    thread_pool.clear_pending_tasks();
    CHECK(thread_pool.get_num_pending_tasks() == 0);

    num_tasks_finished = 0;
    for (std::size_t i = 0; i < 1000; ++i) {
      thread_pool.add_task(start_time + 50s, task);
      thread_pool.add_task(start_time + 10s, task);
    }
    // std::cout << "About to wait for 1000 tasks\n";
    task_finished.wait(lock, [&]() {
      // std::cout << std::format("num_tasks_finished={}, expected 1000\n",
      //                          num_tasks_finished);
      return num_tasks_finished == 1000;
    });
    // std::cout << "About to add 5s\n";
    Clock::add_time(5s);
    // std::cout << "About to wait for 1000 more tasks\n";
    task_finished.wait(lock, [&]() { return num_tasks_finished == 2000; });

    thread_pool.block_new_tasks();
    thread_pool.clear_pending_tasks();
    // std::cout << std::format("Finishing up\n");
  };

  SECTION("add_periodic_task") {
    //    Clock::time_point start_time = Clock::now();
    std::vector<size_t> counters(2, 0);
    condition_variable task_finished;
    auto create_task = [&](std::size_t index) {
      return [&, index]() {
        std::scoped_lock lock{mutex};
        ++counters[index];
        task_finished.notify_one();
      };
    };
    thread_pool.add_periodic_task(30s, create_task(0), 20s);
    thread_pool.add_periodic_task(10s, create_task(1), 30s);
    CHECK(thread_pool.get_num_pending_tasks() == 2);

    Clock::add_time(20s);
    task_finished.wait(lock, [&]() { return counters[1] == 1; });
    Clock::add_time(10s);
    task_finished.wait(lock, [&]() { return counters[0] == 1; });
    Clock::add_time(10s);
    task_finished.wait(lock, [&]() { return counters[1] == 2; });
    Clock::add_time(10s);
    task_finished.wait(lock, [&]() { return counters[0] == 2; });

    // thread_pool.dump_tasks(time_point{});

    CHECK(thread_pool.get_num_pending_tasks() == 2);

    // Verify that no tasks are run from time start_time+50s to start_time+65s.
    {
      time_point deadline = start_time + 65s;
      bool timeout = !wait_and_step_until<Clock>(
          lock, task_finished, deadline,
          [&]() { return counters[0] != 2 || counters[1] != 2; }, 1s);
      CHECK(timeout);
      CHECK(Clock::now() == deadline);
    }

    Clock::add_time(5s);
    task_finished.wait(lock,
                       [&]() { return counters[0] == 3 && counters[1] == 3; });
    CHECK(thread_pool.get_num_pending_tasks() == 2);
    std::this_thread::sleep_for(1ns);
    Clock::add_time(120s);
    task_finished.wait(lock,
                       [&]() { return counters[0] == 9 && counters[1] == 7; });
    REQUIRE(thread_pool.get_num_pending_tasks() == 2);
  };

  SECTION("long tasks") {
    condition_variable never_notify;
    condition_variable task_updated;
    std::unordered_set<std::size_t> started;
    std::unordered_set<std::size_t> done;

    auto create_task = [&](duration wait_time, std::size_t id) {
      return [&, wait_time, id]() {
        time_point start_work = Clock::now();
        std::unique_lock lock{mutex};
        started.emplace(id);
        task_updated.notify_one();
        never_notify.wait_until(lock, start_work + wait_time,
                                [&]() { return false; });
        done.emplace(id);
        task_updated.notify_one();
      };
    };

    thread_pool.add_task(start_time + 11s, create_task(20s, 0));
    thread_pool.add_task(start_time + 10s, create_task(50s, 1));
    thread_pool.add_task(start_time + 100s, create_task(50s, 2));
    thread_pool.add_task(start_time + 150s, create_task(50s, 3));
    thread_pool.add_task(start_time + 201s, create_task(50s, 4));
    CHECK(!wait_and_step_until<Clock>(lock, task_updated, start_time + 9s,
                                      [&]() { return started.contains(1); }));
    Clock::add_time(1s);
    task_updated.wait(
        lock, [&]() { return !started.contains(0) && started.contains(1); });
    CHECK(!wait_and_step_until<Clock>(lock, task_updated, start_time + 59s,
                                      [&]() { return done.contains(1); }));
    Clock::add_time(1s);
    task_updated.wait(
        lock, [&]() { return started.contains(0) && done.contains(1); });

    CHECK(!wait_and_step_until<Clock>(lock, task_updated, start_time + 79s,
                                      [&]() { return done.contains(0); }));
    Clock::set_time(start_time + 80s);
    task_updated.wait(lock, [&]() { return done.contains(0); });

    CHECK(thread_pool.get_num_executing_tasks() == 0);

    Clock::set_time(start_time + 100s);
    task_updated.wait(lock, [&]() {
      return thread_pool.get_num_executing_tasks() == 1 &&
             thread_pool.get_num_pending_tasks() == 2;
    });
    CHECK(!wait_and_step_until<Clock>(lock, task_updated, start_time + 149s,
                                      [&]() { return done.contains(2); }));
    Clock::add_time(1s);
    task_updated.wait(
        lock, [&]() { return done.contains(2) && started.contains(3); });
    Clock::add_time(50s);
    task_updated.wait(
        lock, [&]() { return done.contains(3) && !started.contains(4); });
    Clock::add_time(1s);
    task_updated.wait(lock, [&]() { return started.contains(4); });

    thread_pool.add_task(start_time, create_task(10s, 5));
    Clock::add_time(50s);
    task_updated.wait(
        lock, [&]() { return done.contains(4) && started.contains(5); });
    Clock::add_time(10s);
    task_updated.wait(lock, [&]() { return done.contains(5); });
  };

  SECTION("batched tasks") {
    condition_variable never_notify;
    condition_variable task_updated;
    std::vector<std::size_t> done;

    constexpr static std::size_t num_groups = 11;
    constexpr static std::size_t group_size = 7;
    constexpr static std::size_t num_tasks = num_groups * group_size;

    auto create_task = [&](duration wait_time, std::size_t id) {
      return [&, wait_time, id]() {
        time_point start_work = Clock::now();
        std::unique_lock lock{mutex};
        never_notify.wait_until(lock, start_work + wait_time,
                                [&]() { return false; });
        done.emplace_back(id);
        task_updated.notify_one();
      };
    };

    std::vector<std::size_t> task_groups;
    task_groups.reserve(num_tasks);

    for (std::size_t i = 0; i < num_groups; ++i) {
      for (std::size_t j = 0; j < group_size; ++j) {
        task_groups.emplace_back(i);
      }
    }

    constexpr static std::uint_fast32_t seed = 0x12345678;
    std::mt19937 rand_gen(seed);

    std::uniform_int_distribution<std::size_t> pick_length{1, num_tasks};
    for (std::size_t i = task_groups.size(); i-- > 0;) {
      std::uniform_int_distribution<std::size_t> pick_index{
          0, task_groups.size() - 1};
      std::size_t index = pick_index(rand_gen);
      std::size_t task_group = task_groups[index];
      std::size_t task_length = pick_length(rand_gen);
      thread_pool.add_task(start_time + task_group * group_size * 1s,
                           create_task(task_length * 1s, task_group));
      task_groups[index] = task_groups.back();
      task_groups.pop_back();
    }

    wait_and_step<Clock>(lock, task_updated,
                         [&]() { return done.size() == num_tasks; });

    REQUIRE(done.size() == num_tasks);
    for (std::size_t i = 0; i < num_tasks; ++i) {
      CHECK(done[i] == i / group_size);
    }
  };
}

TEST_CASE("ThreadPool - multiple threads", "[thread_pool]") {
  using Clock = TestClock<__LINE__>;
  using condition_variable = Clock::condition_variable;
  using time_point = Clock::time_point;
  using duration = Clock::duration;
  using ThreadPool =
      coroutini::threadpool::ThreadPool<std::allocator<std::byte>, Clock,
                                        condition_variable>;
  constexpr static std::size_t num_threads = 3;

  auto clock_broadcaster = Clock::periodic_broadcaster(10ms);
  ThreadPool thread_pool(num_threads);

  Clock::time_point start_time = Clock::now();
  std::mutex mutex;
  std::unique_lock lock{mutex};

  SECTION("batched tasks") {
    condition_variable never_notify;
    condition_variable task_updated;
    std::vector<std::size_t> done;

    constexpr static std::size_t num_groups = 43;
    constexpr static std::size_t group_size = 9;
    constexpr static std::size_t num_tasks = num_groups * group_size;

    auto create_task = [&](duration wait_time, std::size_t id) {
      return [&, wait_time, id]() {
        time_point start_work = Clock::now();
        std::unique_lock lock{mutex};
        never_notify.wait_until(lock, start_work + wait_time,
                                [&]() { return false; });
        done.emplace_back(id);
        task_updated.notify_one();
      };
    };

    std::vector<std::size_t> task_groups;
    task_groups.reserve(num_tasks);

    for (std::size_t i = 0; i < num_groups; ++i) {
      for (std::size_t j = 0; j < group_size; ++j) {
        task_groups.emplace_back(i);
      }
    }

    constexpr static std::uint_fast32_t seed = 0x12345678;
    std::mt19937 rand_gen(seed);

    // Add tasks in random order and random lengths.
    std::uniform_int_distribution<std::size_t> pick_length{1, num_tasks};
    for (std::size_t i = task_groups.size(); i-- > 0;) {
      std::uniform_int_distribution<std::size_t> pick_index{
          0, task_groups.size() - 1};
      std::size_t index = pick_index(rand_gen);
      std::size_t task_group = task_groups[index];
      std::size_t task_length = pick_length(rand_gen);
      thread_pool.add_task(start_time + task_group * group_size * 1s,
                           create_task(task_length * 1s, task_group));
      task_groups[index] = task_groups.back();
      task_groups.pop_back();
    }

    wait_and_step<Clock>(lock, task_updated,
                         [&]() { return done.size() == num_tasks; });

    REQUIRE(done.size() == num_tasks);
    std::vector<std::size_t> id_hist(num_groups, 0);
    for (auto id : done) {
      ++id_hist[id];
    }
    for (auto count : id_hist) {
      CHECK(count == group_size);
    }
  };

  SECTION("concurrent tasks") {
    std::barrier barrier{num_threads + 1};
    std::unordered_set<std::size_t> started;
    std::unordered_set<std::size_t> done;
    condition_variable task_updated;
    auto create_task = [&](std::size_t id) {
      return [&, id]() {
        std::unique_lock lock{mutex};
        started.emplace(id);
        // std::cout << std::format("inserted {}, size = {}\n", id,
        //                          started.size());
        task_updated.notify_one();
        lock.unlock();
        // std::cout << std::format(
        //     "inserted {}, size = {}, arriving at barrier\n", id,
        //     started.size());
        barrier.arrive_and_wait();
        // std::cout << std::format("inserted {}, size = {}, leaving barrier\n",
        //                          id, started.size());
        lock.lock();
        done.emplace(id);
        task_updated.notify_one();
      };
    };

    for (std::size_t i = 0; i < num_threads; ++i) {
      thread_pool.add_task(create_task(i));
      // std::cout << std::format("*** Added task {}\n", i);
      // thread_pool.dump_tasks(start_time);
    }
    task_updated.wait(lock, [&]() {
      // std::cout << std::format("Waiting...\n");
      // thread_pool.dump_tasks(start_time);
      return started.size() == num_threads;
    });
    CHECK(done.empty());
    lock.unlock();
    barrier.arrive_and_wait();
    lock.lock();
    task_updated.wait(lock, [&]() { return done == started; });
  };
}

}  // namespace
