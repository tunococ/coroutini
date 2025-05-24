module;

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <numeric>
#include <set>
#include <thread>
#include <vector>

export module coroutini_threadpool;

namespace coroutini::threadpool {

/**
 * @brief A class that holds a collection of threads and a task queue, and
 * manages task execution among the threads.
 *
 * The main use of ThreadPool is to execute a task on a thread that belongs to
 * this thread pool. The user of ThreadPool is expected to call add_task().
 *
 * Although add_task() is usually called by a thread that does not belong to the
 * thread pool, it is possible to call it from the thread that belongs to the
 * thread pool. This means it is possible to call add_task() inside the task. In
 * fact, this is how add_periodic_task() works.
 *
 * ### Exception handling
 *
 * Each thread in the thread pool will suppress any exception raised by the task
 * that it executes.
 *
 * ### Customizable clock type
 *
 * ThreadPool can work with any clock type (that satisfies the [TrivialClock
 * requirements](https://en.cppreference.com/w/cpp/named_req/TrivialClock)) with
 * a compatible condition variable type that exposes `wait`, `wait_until`, and
 * `notify_one`.
 * (See coroutini::utils::ManualClock for more details.)
 *
 * @tparam Allocator An allocator for internal data structures.
 *
 * @tparam Clock A clock that provides time. ThreadPool supports a clock type
 * other than std::chrono::steady_clock provided that @p ConditionVariable works
 * with it.
 *
 * @tparam ConditionVariable A `Clock`-compatible condition variable type.
 */
export template <class Allocator = std::allocator<std::byte>,
                 class Clock = std::chrono::steady_clock,
                 class ConditionVariable = std::condition_variable>
struct ThreadPool {
  using allocator_type = Allocator;
  using clock_type = Clock;
  using condition_variable = ConditionVariable;
  using this_type = ThreadPool<allocator_type, clock_type, condition_variable>;

  using time_point = Clock::time_point;
  using duration = Clock::duration;

  ThreadPool(ThreadPool&&) = delete;
  ThreadPool(ThreadPool const&) = delete;

  ThreadPool operator=(ThreadPool&&) = delete;
  ThreadPool operator=(ThreadPool const&) = delete;

  /**
   * @brief Creates a ThreadPool.
   *
   * @param num_threads The number of threads.
   * @param allocator An allocator.
   */
  ThreadPool(std::size_t num_threads = 1,
             allocator_type const& allocator = allocator_type());

  /**
   * @brief An overload of ThreadPool() that supports the leading-allocator
   * convention.
   */
  constexpr ThreadPool(std::allocator_arg_t, allocator_type const& allocator,
                       std::size_t num_threads = 1)
      : ThreadPool{num_threads, allocator} {}

  /**
   * @brief Destroys the ThreadPool.
   *
   * When the destructor is called, it
   * - calls block_new_tasks();
   * - sends a _stop request_ to all the threads in the pool; then
   * - waits for the threads to exit.
   *
   * When a thread receives the stop request, it will
   * - finish the task it is currently executing, if any; then
   * - exit.
   *
   * Consequently, ~ThreadPool() will block until all the executing tasks are
   * completed. All the pending tasks that have not begun executing will be
   * lost.
   */
  ~ThreadPool();

  /**
   * @brief Returns the number of threads that this thread pool manages.
   */
  std::size_t get_num_threads() const;

  /**
   * @brief Returns the number of tasks that have been added but have not
   * started executing.
   */
  std::size_t get_num_pending_tasks() const;

  /**
   * @brief Returns the number of tasks that are executing.
   */
  std::size_t get_num_executing_tasks() const;

  /**
   * @brief Returns the number of tasks that have not finished executing.
   * This is similar to `get_num_threads() + get_num_pending_tasks()`, but the
   * sum is atomic.
   */
  std::size_t get_num_tasks() const;

  void add_threads(std::size_t num_new_threads);

  /**
   * @brief Disallows (or allows) subsequent add_task() calls to add a task to
   * the task queue.
   *
   * @param block Whether to block or unblock new tasks.
   */
  void block_new_tasks(bool block = true) noexcept;

  /**
   * @brief Calls \ref block_new_tasks "block_new_tasks"(false).
   */
  void unblock_new_tasks() noexcept { block_new_tasks(false); }

  /**
   * @brief Removes all tasks that have not started executing.
   *
   * @param wait_for_executing_tasks Whether this function will wait for
   * executing tasks to finish before removing all the pending tasks or not.
   */
  void clear_pending_tasks(bool wait_for_executing_tasks = true);

  /**
   * @brief Adds a task that will start executing after a scheduled time to the
   * task queue.
   *
   * Note that if all the threads in the thread pool are busy when the clock
   * time reaches @p scheduled_time, the task will not start executing until one
   * thread in the thread pool becomes free.
   *
   * If block_new_tasks() has been called (with parameter `block = true`),
   * add_task() will not add a task to the task queue and return `false`.
   * Otherwise, add_task() will return `true`.
   *
   * @param scheduled_time The time to start executing the task.
   * @param task The task to execute.
   * @return Whether the task was added to the task queue or not.
   */
  template <class TaskFunc>
  bool add_task(time_point const& scheduled_time, TaskFunc&& task_func);

  /**
   * @brief An overload of add_task() that operates as if `scheduled_time =
   * clock_type::now()`.
   */
  template <class TaskFunc>
  bool add_task(TaskFunc&& task_func);

  /**
   * @brief An overload of add_task() that operates as if `scheduled_time =
   * clock_type::now() + delay`.
   */
  template <class TaskFunc>
  bool add_task(duration const& delay, TaskFunc&& task_func);

  /**
   * @brief Adds a task to the task queue periodically.
   *
   * This is a convenience function that will package a given @p task_func into
   * a new task that will call @p task_func and add_periodic_task() with the
   * same @p task_func, then add the packaged task to the task queue with
   * add_task(). The return value of add_periodic_task() reflects the success of
   * the addition of the first packaged task only.
   *
   * There are two operating modes of add_periodic_task() that can be chosen:
   *
   * - If `wait_for_task == true`, @p task_func will be executed before the
   * add_periodic_task() call, and the next execution will be scheduled at
   * `now() + period`.
   *
   * - If `wait_for_task == false`, add_periodic_task() will be called before
   * @p task_func, and the next execution will be scheduled at `scheduled_time +
   * period`.
   *
   * However, if `wait_for_task == false` and @p period is not
   * strictly positive, the packaged task that calls add_periodic_task() before
   * @p task_func may cause an unbounded number of tasks to be added even before
   * the first execution of @p task_func. For this reason, add_periodic_task()
   * will behave as if `wait_for_task == true` if @p period is not strictly
   * positive.
   *
   * @param scheduled_time The starting time of the first execution.
   *
   * @param task_func The task to execute.
   *
   * @param period The period between the starting times of subsequent
   * executions.
   *
   * @param wait_for_task If `wait_for_task == true`, add_periodic_task()
   * will be called after @p task_func finishes executing. Otherwise, the order
   * will be reversed.
   *
   * @return Whether the first task was added to the task queue or not.
   */
  template <class TaskFunc>
  bool add_periodic_task(time_point const& scheduled_time, TaskFunc&& task_func,
                         duration period = duration(0),
                         bool wait_for_task = false);

  /**
   * @brief An overload of add_periodic_task() that operates as if
   * `scheduled_time = clock_type::now()`.
   */
  template <class TaskFunc>
  bool add_periodic_task(TaskFunc&& task_func, duration period = duration(0),
                         bool wait_for_task = false);

  /**
   * @brief An overload of add_periodic_task() that operates as if
   * `scheduled_time = clock_type::now() + initial_delay`.
   */
  template <class TaskFunc>
  bool add_periodic_task(duration const& initial_delay, TaskFunc&& task_func,
                         duration period = duration(0),
                         bool wait_for_task = false);

protected:
  std::atomic_bool block_new_tasks_{false};
  condition_variable thread_update_;

  allocator_type allocator_;

  struct Thread;
  using ThreadAllocator =
      std::allocator_traits<allocator_type>::template rebind_alloc<Thread>;

  struct Task;
  using TaskAllocator =
      std::allocator_traits<allocator_type>::template rebind_alloc<Task>;
  using TaskQueue = std::multiset<Task, typename Task::Compare, TaskAllocator>;
  using TaskRef = TaskQueue::iterator;
  using TaskRefAllocator =
      std::allocator_traits<allocator_type>::template rebind_alloc<TaskRef>;

  // Set of tasks
  TaskQueue tasks_;
  // Subset of tasks containing unassigned tasks
  std::multiset<TaskRef, typename Task::CompareRef, TaskRefAllocator>
      unassigned_tasks_;
  // Mutex for all task data
  mutable std::mutex tasks_mutex_;

  using TaskFunction = std::function<void()>;
  /// @cond INTERNAL
  /**
   * @brief Data for a task.
   */
  struct Task {
    TaskFunction task_function;
    time_point scheduled_time;
    mutable std::optional<std::size_t> assigned_thread;
    struct Compare {
      using is_transparent = void;
      constexpr bool operator()(Task const& a, Task const& b) const {
        return a.scheduled_time < b.scheduled_time;
      }
      constexpr bool operator()(Task const& a, time_point const& b) const {
        return a.scheduled_time < b;
      }
      constexpr bool operator()(time_point const& a, Task const& b) const {
        return a < b.scheduled_time;
      }
    };
    struct CompareRef {
      constexpr bool operator()(TaskRef const& a, TaskRef const& b) const {
        return a->scheduled_time < b->scheduled_time;
      }
    };

    template <class TaskFunc>
      requires(std::convertible_to<TaskFunc &&, TaskFunc>)
    constexpr Task(TaskFunc&& task_func, time_point const& scheduled_time)
        : task_function{std::forward<TaskFunc>(task_func)},
          scheduled_time{scheduled_time} {}

    constexpr Task(Task&&) = default;
    constexpr Task(Task const&) = default;
    constexpr Task& operator=(Task&&) = default;
    constexpr Task& operator=(Task const&) = default;
  };

  /**
   * @brief Data for a thread in the thread pool.
   */
  struct Thread {
    std::jthread thread;
    std::optional<TaskRef> assigned_task;
    condition_variable wake_up;

    bool executing{false};
  };

  struct ThreadList : std::vector<Thread, ThreadAllocator> {
    using super_type = std::vector<Thread, ThreadAllocator>;

    // Inherit constructors from super_type.
    using super_type::super_type;

    using ScheduledTime =
        std::optional<std::reference_wrapper<time_point const>>;
    // Add a comparison function to ThreadList so it can be used to sort
    // thread_indices_ordered_by_time_.
    using is_transparent = void;
    constexpr ScheduledTime thread_scheduled_time(
        std::size_t thread_index) const {
      std::optional<TaskRef> const& task = (*this)[thread_index].assigned_task;
      return task ? std::cref(task.value()->scheduled_time) : ScheduledTime{};
    }
    constexpr bool operator()(ScheduledTime const& time_1,
                              ScheduledTime const& time_2) const {
      if (!time_1) {
        return false;
      }
      if (!time_2) {
        return true;
      }
      return time_1->get() < time_2->get();
    }
    constexpr bool operator()(std::size_t thread_index_1,
                              std::size_t thread_index_2) const {
      bool compare_time = operator()(thread_scheduled_time(thread_index_1),
                                     thread_scheduled_time(thread_index_2));
      if (compare_time) {
        return true;
      }
      compare_time = operator()(thread_scheduled_time(thread_index_2),
                                thread_scheduled_time(thread_index_1));
      if (compare_time) {
        return false;
      }
      return thread_index_1 < thread_index_2;
    }
    // constexpr bool operator()(time_point const& time_1,
    //                           ScheduledTime const& time_2) const {
    //   if (!time_2) {
    //     return true;
    //   }
    //   return time_1 < *time_2;
    // }
    // constexpr bool operator()(ScheduledTime const& time_1,
    //                           time_point const& time_2) const {
    //   if (!time_1) {
    //     return false;
    //   }
    //   return *time_1 < time_2;
    // }
    // constexpr bool operator()(std::size_t thread_index,
    //                           ScheduledTime const& time) const {
    //   return operator()(thread_scheduled_time(thread_index), time);
    // }
    // constexpr bool operator()(std::size_t thread_index,
    //                           time_point const& time) const {
    //   return operator()(thread_scheduled_time(thread_index), time);
    // }
    // constexpr bool operator()(ScheduledTime const& time,
    //                           std::size_t thread_index) const {
    //   return operator()(time, thread_scheduled_time(thread_index));
    // }
    constexpr bool operator()(time_point const& time,
                              std::size_t thread_index) const {
      return operator()(time, thread_scheduled_time(thread_index));
    }
  };
  /// @endcond

  using IndexAllocator =
      std::allocator_traits<allocator_type>::template rebind_alloc<std::size_t>;

  // List of threads
  ThreadList threads_;
  // List of threads that are not executing a task, ordered by their scheduled
  // wake-up times
  // Note: ThreadList::operator() is used as the "less" function.
  std::set<std::size_t, std::reference_wrapper<ThreadList const>,
           IndexAllocator>
      idle_threads_;

  static void thread_function_(std::stop_token stop_token,
                               this_type* thread_pool,
                               std::size_t thread_index);
};

// Implementations

template <class Allocator, class Clock, class ConditionVariable>
ThreadPool<Allocator, Clock, ConditionVariable>::ThreadPool(
    std::size_t num_threads, allocator_type const& allocator)
    : allocator_{allocator},
      tasks_{typename Task::Compare(), allocator},
      unassigned_tasks_{typename Task::CompareRef(), allocator},
      threads_{num_threads, allocator_},
      idle_threads_{std::cref(threads_), allocator_} {
  std::scoped_lock lock{tasks_mutex_};
  for (std::size_t i = 0; i < num_threads; ++i) {
    threads_[i].thread = std::jthread(thread_function_, this, i);
  }
  for (std::size_t i = 0; i < num_threads; ++i) {
    idle_threads_.emplace(i);
  }
}

template <class Allocator, class Clock, class ConditionVariable>
ThreadPool<Allocator, Clock, ConditionVariable>::~ThreadPool() {
  {
    std::scoped_lock lock{tasks_mutex_};
    block_new_tasks();
    for (Thread& thread : threads_) {
      thread.thread.request_stop();
    }
  }
  for (Thread& thread : threads_) {
    thread.wake_up.notify_one();
    thread.thread.join();
  }
}

template <class Allocator, class Clock, class ConditionVariable>
std::size_t ThreadPool<Allocator, Clock, ConditionVariable>::get_num_threads()
    const {
  std::scoped_lock lock{tasks_mutex_};
  return threads_.size();
}

template <class Allocator, class Clock, class ConditionVariable>
std::size_t
ThreadPool<Allocator, Clock, ConditionVariable>::get_num_pending_tasks() const {
  std::scoped_lock lock{tasks_mutex_};
  return tasks_.size();
}

template <class Allocator, class Clock, class ConditionVariable>
std::size_t ThreadPool<Allocator, Clock,
                       ConditionVariable>::get_num_executing_tasks() const {
  std::scoped_lock lock{tasks_mutex_};
  return std::accumulate(threads_.begin(), threads_.end(), 0,
                         [](std::size_t acc, Thread const& thread) {
                           return acc + (thread.executing ? 1 : 0);
                         });
}

template <class Allocator, class Clock, class ConditionVariable>
std::size_t ThreadPool<Allocator, Clock, ConditionVariable>::get_num_tasks()
    const {
  std::scoped_lock lock{tasks_mutex_};
  return std::accumulate(threads_.begin(), threads_.end(), 0,
                         [](std::size_t acc, Thread const& thread) {
                           return acc + (thread.executing ? 1 : 0);
                         }) +
         tasks_.size();
}

template <class Allocator, class Clock, class ConditionVariable>
void ThreadPool<Allocator, Clock, ConditionVariable>::add_threads(
    std::size_t num_new_threads) {
  std::scoped_lock lock{tasks_mutex_};
  while (num_new_threads-- > 0) {
    std::size_t thread_index = threads_.size();
    threads_.emplace_back(std::jthread(thread_function_, this, thread_index));
    idle_threads_.emplace(thread_index);
  }
}

template <class Allocator, class Clock, class ConditionVariable>
void ThreadPool<Allocator, Clock, ConditionVariable>::block_new_tasks(
    bool block) noexcept {
  block_new_tasks_.store(block, std::memory_order_relaxed);
}

template <class Allocator, class Clock, class ConditionVariable>
void ThreadPool<Allocator, Clock, ConditionVariable>::clear_pending_tasks(
    bool wait_for_executing_tasks) {
  std::unique_lock lock{tasks_mutex_};
  if (wait_for_executing_tasks) {
    thread_update_.wait(lock, [this]() {
      return std::all_of(
          threads_.begin(), threads_.end(),
          [](Thread const& thread) { return !thread.executing; });
    });
  }
  idle_threads_.clear();
  for (std::size_t i = 0; i < threads_.size(); ++i) {
    Thread& thread = threads_[i];
    thread.assigned_task.reset();
    if (!thread.executing && !thread.thread.get_stop_token().stop_requested()) {
      idle_threads_.emplace(i);
    }
  }
  unassigned_tasks_.clear();
  tasks_.clear();
}

template <class Allocator, class Clock, class ConditionVariable>
void ThreadPool<Allocator, Clock, ConditionVariable>::thread_function_(
    std::stop_token stop_token, this_type* thread_pool,
    std::size_t thread_index) {
  std::unique_lock lock{thread_pool->tasks_mutex_};
  Thread& thread = thread_pool->threads_[thread_index];
  auto& assigned_task = thread.assigned_task;
  while (!stop_token.stop_requested()) {
    // If this thread has an assigned task, try to execute it.
    if (assigned_task) {
      // If the task is not yet scheduled, wait for it.
      while (!stop_token.stop_requested() && assigned_task &&
             assigned_task.value()->scheduled_time > clock_type::now()) {
        thread.wake_up.wait_until(lock, assigned_task.value()->scheduled_time);
      }
      if (stop_token.stop_requested() || !assigned_task) {
        continue;
      }
      // At this point, the scheduled time has passed, so we start executing the
      // task.

      // Report that this thread is not idle now.
      thread_pool->idle_threads_.erase(thread_index);
      thread.executing = true;
      // Move assigned_task out of tasks_.
      Task task = std::move(**assigned_task);
      thread_pool->tasks_.erase(*assigned_task);
      assigned_task.reset();

      // Execute the task.
      lock.unlock();
      try {
        task.task_function();
      } catch (...) {
      }
      lock.lock();

      thread.executing = false;
      thread_pool->idle_threads_.emplace(thread_index);
      thread_pool->thread_update_.notify_one();
      continue;
    }

    // At this point, assigned_task is empty.
    // If there are no unassigned tasks, we declare that this thread is idle,
    // and wait for a task assignment from add_task.
    if (thread_pool->unassigned_tasks_.empty()) {
      while (!assigned_task && !stop_token.stop_requested()) {
        thread.wake_up.wait(lock);
      }
      continue;
    }

    // At this point, assigned_task is empty and unassigned_tasks_ is not.
    // We claim the first unassigned task by assigning it to this thread.
    auto first_unassigned_task = thread_pool->unassigned_tasks_.begin();
    TaskRef task_ref = *first_unassigned_task;
    thread_pool->unassigned_tasks_.erase(first_unassigned_task);
    assigned_task.emplace(task_ref);
    task_ref->assigned_thread.emplace(thread_index);
  }
  if (assigned_task) {
    thread_pool->idle_threads_.erase(thread_index);
    (*assigned_task)->assigned_thread.reset();
    thread_pool->unassigned_tasks_.emplace(*assigned_task);
    assigned_task.reset();
  }
}

template <class Allocator, class Clock, class ConditionVariable>
template <class TaskFunc>
bool ThreadPool<Allocator, Clock, ConditionVariable>::add_task(
    time_point const& scheduled_time, TaskFunc&& task_func) {
  if (block_new_tasks_.load(std::memory_order_relaxed)) {
    return false;
  }
  std::unique_lock lock{tasks_mutex_};
  // Create a new task.
  TaskRef task_ref =
      tasks_.emplace(std::forward<TaskFunc>(task_func), scheduled_time);

  // If there are no idle threads, the new task will be unassigned.
  if (idle_threads_.empty()) {
    unassigned_tasks_.emplace(task_ref);
    return true;
  }

  // If there are free threads, we will check if the one with the latest
  // scheduled time should be reassigned to this new task.

  // Get the iterator of the last element of free_threads_.
  auto thread_index_ref = --idle_threads_.end();
  // The value of the iterator is the index of an idle thread whose scheduled
  // wake-up time is the latest among all the idle threads.
  std::size_t thread_index_to_reassign = *thread_index_ref;

  // If the new task is scheduled later than the scheduled wake-up time of the
  // thread, there will be no task assignment. The new task will be unassigned.
  if (!threads_(scheduled_time, thread_index_to_reassign)) {
    // Note: threads_.operator() is a "less" function that works on both thread
    // indices and time points.
    unassigned_tasks_.emplace(task_ref);
    return true;
  }

  // Otherwise, the new task will be assigned to the thread.
  Thread& thread_to_reassign = threads_[thread_index_to_reassign];
  auto& assigned_task = thread_to_reassign.assigned_task;
  if (assigned_task) {
    // If the thread has already been assigned a task, it must be unassigned
    // first.
    unassigned_tasks_.emplace(*assigned_task);
    (*assigned_task)->assigned_thread.reset();
  }
  idle_threads_.erase(thread_index_ref);
  assigned_task.emplace(task_ref);
  idle_threads_.emplace(thread_index_to_reassign);
  task_ref->assigned_thread.emplace(thread_index_to_reassign);

  lock.unlock();

  // Wake up the thread.
  thread_to_reassign.wake_up.notify_one();
  return true;
}

template <class Allocator, class Clock, class ConditionVariable>
template <class TaskFunc>
bool ThreadPool<Allocator, Clock, ConditionVariable>::add_task(
    TaskFunc&& task_func) {
  return add_task(clock_type::now(), std::forward<TaskFunc>(task_func));
}

template <class Allocator, class Clock, class ConditionVariable>
template <class TaskFunc>
bool ThreadPool<Allocator, Clock, ConditionVariable>::add_task(
    duration const& duration, TaskFunc&& task_func) {
  return add_task(clock_type::now() + duration,
                  std::forward<TaskFunc>(task_func));
}

template <class Allocator, class Clock, class ConditionVariable>
template <class TaskFunc>
bool ThreadPool<Allocator, Clock, ConditionVariable>::add_periodic_task(
    time_point const& scheduled_time, TaskFunc&& task_func, duration period,
    bool wait_for_task) {
  if (period <= duration(0) && !wait_for_task) {
    wait_for_task = true;
    period = duration(0);
  }
  if (wait_for_task) {
    return add_task(scheduled_time, [this, task_func = std::move(task_func),
                                     period, wait_for_task]() {
      task_func();
      add_periodic_task(clock_type::now() + period, task_func, period,
                        wait_for_task);
    });
  }
  return add_task(scheduled_time,
                  [this, scheduled_time, task_func = std::move(task_func),
                   period, wait_for_task]() {
                    add_periodic_task(scheduled_time + period, task_func,
                                      period, wait_for_task);
                    task_func();
                  });
}

template <class Allocator, class Clock, class ConditionVariable>
template <class TaskFunc>
bool ThreadPool<Allocator, Clock, ConditionVariable>::add_periodic_task(
    TaskFunc&& task_func, duration period, bool wait_for_task) {
  return add_periodic_task(clock_type::now(), std::forward<TaskFunc>(task_func),
                           period, wait_for_task);
}

template <class Allocator, class Clock, class ConditionVariable>
template <class TaskFunc>
bool ThreadPool<Allocator, Clock, ConditionVariable>::add_periodic_task(
    duration const& initial_delay, TaskFunc&& task_func, duration period,
    bool wait_for_task) {
  return add_periodic_task(clock_type::now() + initial_delay,
                           std::forward<TaskFunc>(task_func), period,
                           wait_for_task);
}

}  // namespace coroutini::threadpool
