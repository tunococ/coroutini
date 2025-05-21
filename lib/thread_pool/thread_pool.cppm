module;

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

export module coroutini_threadpool;

namespace coroutini::threadpool {

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
   * Creates a `ThreadPool`.
   *
   * @param num_threads The number of threads.
   * @param allocator An allocator.
   */
  ThreadPool(std::size_t num_threads = 1,
             allocator_type const& allocator = allocator_type());

  /**
   * Creates a `ThreadPool` with the leading-allocator convention.
   */
  constexpr ThreadPool(std::allocator_arg_t, allocator_type const& allocator,
                       std::size_t num_threads = 1)
      : ThreadPool{num_threads, allocator} {}

  ~ThreadPool();

  std::size_t get_num_threads() const;

  std::size_t get_num_pending_tasks() const;

  std::size_t get_num_executing_tasks() const;

  void add_threads(std::size_t num_new_threads);

  void block_new_tasks(bool block = true) noexcept;

  void wait_for_pending_tasks(bool block_new_tasks = true);

  void clear_pending_tasks(bool wait_for_executing_tasks = true);

  void unblock_new_tasks() noexcept { block_new_tasks(false); }

  template <class TaskFunc>
  bool add_task(time_point const& scheduled_time, TaskFunc&& task_func);

  template <class TaskFunc>
  bool add_task(TaskFunc&& task_func);

  template <class TaskFunc>
  bool add_task(duration const& delay, TaskFunc&& task_func);

  template <class TaskFunc>
  bool add_periodic_task(time_point const& scheduled_time, TaskFunc&& task_func,
                         duration period = duration(0));

  template <class TaskFunc>
  bool add_periodic_task(TaskFunc&& task_func, duration period = duration(0));

  template <class TaskFunc>
  bool add_periodic_task(duration const& initial_delay, TaskFunc&& task_func,
                         duration period = duration(0));

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
  std::scoped_lock lock{tasks_mutex_};
  block_new_tasks();
  for (Thread& thread : threads_) {
    thread.thread.request_stop();
    thread.wake_up.notify_one();
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
  return threads_.size() - idle_threads_.size();
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
void ThreadPool<Allocator, Clock, ConditionVariable>::wait_for_pending_tasks(
    bool block_new_tasks) {
  block_new_tasks_.store(block_new_tasks, std::memory_order_relaxed);
  std::unique_lock lock{tasks_mutex_};
  thread_update_.wait(lock, [this]() {
    return tasks_.empty() && (idle_threads_.size() == threads_.size());
  });
}

template <class Allocator, class Clock, class ConditionVariable>
void ThreadPool<Allocator, Clock, ConditionVariable>::clear_pending_tasks(
    bool wait_for_executing_tasks) {
  std::unique_lock lock{tasks_mutex_};
  if (wait_for_executing_tasks) {
    thread_update_.wait(
        lock, [this]() { return idle_threads_.size() == threads_.size(); });
  }
  idle_threads_.clear();
  for (std::size_t i = 0; i < threads_.size(); ++i) {
    Thread& thread = threads_[i];
    thread.assigned_task.reset();
    if (!thread.executing) {
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

      // Move assigned_task out of tasks_.
      Task task = std::move(**assigned_task);
      thread_pool->tasks_.erase(*assigned_task);
      // Report that this thread is not idle now.
      thread_pool->idle_threads_.erase(thread_index);
      assigned_task.reset();
      thread.executing = true;

      // Execute the task.
      lock.unlock();
      task.task_function();
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
      //      thread_pool->idle_threads_.emplace(thread_index);
      //      thread_pool->thread_update_.notify_one();
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
    // thread_pool->idle_threads_.emplace(thread_index);
    // thread_pool->thread_update_.notify_one();
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
    time_point const& scheduled_time, TaskFunc&& task_func, duration period) {
  return add_task(scheduled_time, [this, scheduled_time,
                                   task_func = std::move(task_func), period]() {
    add_periodic_task(scheduled_time + period, task_func, period);
    task_func();
  });
}

template <class Allocator, class Clock, class ConditionVariable>
template <class TaskFunc>
bool ThreadPool<Allocator, Clock, ConditionVariable>::add_periodic_task(
    TaskFunc&& task_func, duration period) {
  return add_periodic_task(clock_type::now(), std::forward<TaskFunc>(task_func),
                           period);
}

template <class Allocator, class Clock, class ConditionVariable>
template <class TaskFunc>
bool ThreadPool<Allocator, Clock, ConditionVariable>::add_periodic_task(
    duration const& initial_delay, TaskFunc&& task_func, duration period) {
  return add_periodic_task(clock_type::now() + initial_delay,
                           std::forward<TaskFunc>(task_func), period);
}

}  // namespace coroutini::threadpool
