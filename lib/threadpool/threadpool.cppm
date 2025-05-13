module;

#include <chrono>
#include <condition_variable>
#include <functional>
#include <set>
#include <thread>

export module coroutini:threadpool;

namespace coroutini::threadpool {

export template <class Allocator, class Clock = std::chrono::steady_clock,
                 class ConditionVariable = std::condition_variable_any>
struct ThreadPool {
  using allocator_type = Allocator;
  using clock_type = Clock;
  using condition_variable_type = ConditionVariable;
  using this_type =
      ThreadPool<allocator_type, clock_type, condition_variable_type>;

  using time_point = Clock::time_point;
  using duration = Clock::duration;

  ThreadPool(ThreadPool&&) = delete;
  ThreadPool(ThreadPool const&) = delete;

  ThreadPool operator=(ThreadPool&&) = delete;
  ThreadPool operator=(ThreadPool const&) = delete;

  constexpr ThreadPool(std::size_t num_threads,
                       Allocator const& allocator = Allocator())
      : thread_list_{num_threads, allocator},
        allocator_{allocator},
        thread_indices_ordered_by_time_{std::cref(thread_list_), allocator} {}

  constexpr ThreadPool(Allocator const& allocator = Allocator()) noexcept(
      Allocator())
      : thread_list_{allocator},
        allocator_{allocator},
        thread_indices_ordered_by_time_{std::cref(thread_list_), allocator} {}

  constexpr ThreadPool(std::allocator_arg_t, Allocator const& allocator,
                       std::size_t num_threads = 1)
      : ThreadPool{num_threads, allocator} {}

protected:
  Allocator allocator_;

  struct ThreadData;
  std::vector<ThreadData, Allocator> thread_data_;
  std::mutex thread_data_mutex_;

  struct TaskData;
  using TaskQueue = std::set<TaskData, typename TaskData::Compare, Allocator>;
  TaskQueue task_queue_;
  using TaskRef = TaskQueue::iterator;

  using Task = std::function<void()>;
  struct TaskData {
    Task task_func;
    time_point scheduled_time;
    std::optional<std::size_t> assigned_thread;
    struct Compare {
      using is_transparent = void;
      constexpr bool operator()(ThreadData const& a,
                                ThreadData const& b) const {
        return a.scheduled_time < b.scheduled_time;
      }
      constexpr bool operator()(ThreadData const& a,
                                time_point const& b) const {
        return a.scheduled_time < b;
      }
      constexpr bool operator()(time_point const& a,
                                ThreadData const& b) const {
        return a < b.scheduled_time;
      }
    };

    template <class TaskFunc>
      requires(std::convertible_to<TaskFunc &&, Task>)
    constexpr TaskData(TaskFunc&& task_func, time_point const& scheduled_time)
        : task_func{std::forward<TaskFunc>(task_func)},
          scheduled_time{scheduled_time} {}

    struct ThreadData {
      std::jthread thread;
    };

    constexpr TaskData(TaskData&&) = default;
    constexpr TaskData(TaskData const&) = default;
    constexpr TaskData& operator=(TaskData&&) = default;
    constexpr TaskData& operator=(TaskData const&) = default;
  };

  struct ThreadData {
    std::jthread thread;
    std::optional<TaskRef> scheduled_task;
    bool executing{false};
  };

  struct ThreadList : std::vector<ThreadData, Allocator> {
    using super_type = std::vector<ThreadData, Allocator>;

    // Inherit constructors from super_type.
    using super_type::super_type;

    // Add a comparison function to ThreadList so it can be used to sort
    // thread_indices_ordered_by_time_.
    using is_transparent = void;
    constexpr bool operator()(std::size_t thread_index_1,
                              std::size_t thread_index_2) const {
      std::optional<TaskRef> const& task_1 =
          (*this)[thread_index_1].scheduled_task;
      std::optional<TaskRef> const& task_2 =
          (*this)[thread_index_2].scheduled_task;
      if (!task_1) {
        return false;
      }
      if (!task_2) {
        return true;
      }
      return task_1.value()->scheduled_time < task_2.value()->scheduled_time;
    }
  };

  ThreadList thread_list_;
  std::set<std::size_t, std::reference_wrapper<ThreadList const>, Allocator>
      thread_indices_ordered_by_time_;

  template <class TaskFunc>
    requires(std::convertible_to<TaskFunc &&, Task>)
  constexpr void add_task(
      TaskFunc&& task_func,
      time_point const& scheduled_time = clock_type::now()) {
    task_queue_.emplace(std::forward<TaskFunc>(task_func), scheduled_time);
  }
};

}  // namespace coroutini::threadpool
