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

  constexpr ThreadPool(std::size_t num_threads = 1,
                       allocator_type const& allocator = allocator_type())
      : allocator_{allocator},
        tasks_{typename Task::Compare(), allocator},
        unassigned_tasks_{typename Task::CompareRef(), allocator},
        threads_{num_threads, allocator_},
        idle_threads_{std::cref(threads_), allocator_} {
    for (std::size_t i = 0; i < num_threads; ++i) {
      threads_[i].thread = std::jthread(thread_function, this, i);
    }
    for (std::size_t i = 0; i < num_threads; ++i) {
      idle_threads_.emplace(i);
    }
  }

  constexpr ThreadPool(std::allocator_arg_t, allocator_type const& allocator,
                       std::size_t num_threads = 1)
      : ThreadPool{num_threads, allocator} {}

  ~ThreadPool() {
    for (Thread& thread : threads_) {
      thread.thread.request_stop();
      thread.wake_up.notify_one();
    }
  }

  void delete_pending_tasks() {
    std::scoped_lock lock{tasks_mutex_};
    for (Thread& thread : threads_) {
      thread.assigned_task.reset();
    }
    idle_threads_.clear();
    for (std::size_t i = 0; i < threads_.size(); ++i) {
      idle_threads_.emplace(i);
    }
    unassigned_tasks_.clear();
    tasks_.clear();
  }

  void wait_for_tasks(bool block_new_tasks = true) {
    block_new_tasks_.store(block_new_tasks, std::memory_order_relaxed);
    std::unique_lock lock{tasks_mutex_};
    task_completion_.wait(lock, [this]() {
      std::cout << "tasks: " << tasks_.size()
                << ", idle_threads: " << idle_threads_.size()
                << ", threads: " << threads_.size() << std::endl;
      return tasks_.empty() && (idle_threads_.size() == threads_.size());
    });
  }

  void unblock_new_tasks() {
    block_new_tasks_.store(false, std::memory_order_relaxed);
  }

  template <class TaskFunc>
  bool add_task(TaskFunc&& task_func,
                time_point const& scheduled_time = Clock::now());

  template <class TaskFunc>
  bool add_task(TaskFunc&& task_func, duration const& duration);

protected:
  std::atomic_bool block_new_tasks_{false};
  condition_variable task_completion_;

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
  std::set<TaskRef, typename Task::CompareRef, TaskRefAllocator>
      unassigned_tasks_;
  // Mutex for all task data
  std::mutex tasks_mutex_;

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
      return operator()(thread_scheduled_time(thread_index_1),
                        thread_scheduled_time(thread_index_2));
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
  std::multiset<std::size_t, std::reference_wrapper<ThreadList const>,
                IndexAllocator>
      idle_threads_;

  static void thread_function(std::stop_token stop_token,
                              this_type* thread_pool, std::size_t thread_index);
};

template <class Allocator, class Clock, class ConditionVariable>
void ThreadPool<Allocator, Clock, ConditionVariable>::thread_function(
    std::stop_token stop_token, this_type* thread_pool,
    std::size_t thread_index) {
  std::unique_lock lock{thread_pool->tasks_mutex_};
  Thread& thread = thread_pool->threads_[thread_index];
  while (!stop_token.stop_requested()) {
    auto& assigned_task = thread.assigned_task;
    // If this thread is idle and there are no pending tasks, the thread will
    // wait until it gets assigned a task.
    while (!assigned_task && !stop_token.stop_requested()) {
      thread.wake_up.wait(lock);
    }
    if (stop_token.stop_requested()) {
      break;
    }
    // At this point, a task has been assigned to this thread.
    while (!stop_token.stop_requested() && assigned_task &&
           assigned_task.value()->scheduled_time > clock_type::now()) {
      thread.wake_up.wait_until(lock, assigned_task.value()->scheduled_time);
    }
    if (stop_token.stop_requested()) {
      break;
    }
    if (!assigned_task) {
      continue;
    }

    // Move assigned_task out of task_queue_.
    Task task = std::move(**assigned_task);
    thread_pool->tasks_.erase(*assigned_task);
    // Report that this thread is not idle now.
    thread_pool->idle_threads_.erase(thread_index);

    // Execute the task.
    lock.unlock();
    task.task_function();
    lock.lock();

    thread_pool->task_completion_.notify_one();
    // Try to find the next task assignment if any.
    if (thread_pool->unassigned_tasks_.empty()) {
      // No unassigned tasks.
      assigned_task.reset();
    } else {
      // Assign the first task.
      TaskRef first_task = *thread_pool->unassigned_tasks_.begin();
      assigned_task.emplace(first_task);
      first_task->assigned_thread.emplace(thread_index);
    }
    // Mark the thread as idle again.
    thread_pool->idle_threads_.emplace(thread_index);
  }
}

template <class Allocator, class Clock, class ConditionVariable>
template <class TaskFunc>
bool ThreadPool<Allocator, Clock, ConditionVariable>::add_task(
    TaskFunc&& task_func, time_point const& scheduled_time) {
  if (block_new_tasks_.load(std::memory_order_relaxed)) {
    return false;
  }
  std::scoped_lock lock{tasks_mutex_};
  // Create a new task.
  TaskRef task_ref =
      tasks_.emplace(std::forward<TaskFunc>(task_func), scheduled_time);

  // If there are free threads, we will check if the one with the latest
  // scheduled time should be reassigned to this new task.
  if (!idle_threads_.empty()) {
    // Get the iterator of the last element of free_threads_.
    auto thread_index_ref = --idle_threads_.end();
    // The value of the iterator is an index of an idle thread whose scheduled
    // wake-up time is the latest in the future (among all the idle threads).
    std::size_t thread_index_to_reassign = *thread_index_ref;

    // Note: threads_.operator() is a "less" function that works on both
    // thread indices and time points.
    if (threads_(scheduled_time, *thread_index_ref)) {
      // If the thread's scheduled wake-up time is later than the scheduled
      // time of the new task, the thread should be reassigned to the new
      // task.
      Thread& thread_to_reassign = threads_[thread_index_to_reassign];
      auto& assigned_task = thread_to_reassign.assigned_task;
      if (assigned_task) {
        // Unassign the currently assigned task first.
        unassigned_tasks_.emplace(*assigned_task);
        (*assigned_task)->assigned_thread.reset();
      }
      // Now, we assign the new task to this thread.
      idle_threads_.erase(thread_index_ref);
      assigned_task.emplace(task_ref);
      idle_threads_.emplace(thread_index_to_reassign);

      task_ref->assigned_thread.emplace(thread_index_to_reassign);
      thread_to_reassign.wake_up.notify_one();
    }
  }
  return true;
}

template <class Allocator, class Clock, class ConditionVariable>
template <class TaskFunc>
bool ThreadPool<Allocator, Clock, ConditionVariable>::add_task(
    TaskFunc&& task_func, duration const& duration) {
  return add_task(std::forward<TaskFunc>(task_func), Clock::now() + duration);
}

}  // namespace coroutini::threadpool
