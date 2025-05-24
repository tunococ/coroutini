module;

#include <chrono>
#include <condition_variable>
#include <functional>
#include <list>
#include <mutex>
#include <stop_token>
#include <thread>

export module coroutini_utils:manual_clock;
import :defer;

namespace coroutini::utils {

export constexpr char const MANUAL_CLOCK_DEFAULT_NAME[] =
    "coroutini::utils::ManualClock";

/**
 * @brief Clock type whose time only moves when explicitly requested.
 *
 * The primary purpose of ManualClock is to facilitate testing of time-dependent
 * code. By using ManualClock effectively, time-dependent code can be tested
 * more deterministically, ensuring consistent results. Additionally, tests can
 * complete faster, as ManualClock removes reliance on real-world time.
 *
 * Since a clock instance is represented as a class, different instances can
 * only be created via different template parameters. The template parameters
 * @p name and @p id are introduced for this purpose. (Dynamic creation of
 * ManualClock instances is not possible.)
 *
 * @tparam Rep Arithmetic type representing the number of ticks in the clock's
 * duration
 *
 * @tparam Period A
 * [`std::ratio`](https://en.cppreference.com/w/cpp/numeric/ratio/ratio) type
 * representing  the tick period of the clock, in seconds
 *
 * @tparam steady A `bool` value that indicates whether this clock is steady or
 * not
 *
 * @tparam name A pointer value that represents one part of the clock's id
 *
 * @tparam id A number (`std::size_t`) that represents the other part of the
 * clock's id
 *
 * ### General usage guidelines
 *
 * - Create a unique clock name as a `constexpr static char []` and use it as
 * the @p name template parameter of ManualClock. The @p id template parameter
 * can also be used to spawn multiple clock instances of the same name.
 * - Create a _periodic broadcaster_ by calling the static function
 * periodic_broadcaster() and store the return value in a scope that will last
 * as long as the clock is needed.
 * - Call set_time() and add_time() to change the clock time.
 * - Use condition_variable and condition_variable_any from this class instead
 * of `std::condition_variable` and `std::condition_variable_any`. These
 * clock-specific condition variables will work with time_point and duration of
 * this clock.
 *
 * ### Advanced usage
 *
 * The _periodic broadcaster_ is, technically, not necessary if you know when to
 * call broadcast_change() manually to unblock waiting threads.
 *
 */
export template <
    class Rep = std::chrono::steady_clock::rep,
    class Period = std::chrono::steady_clock::period, bool steady = false,
    char const* name = MANUAL_CLOCK_DEFAULT_NAME, std::size_t id = -1>
struct ManualClock {
  /**
   * @brief Arithmetic type representing the number of ticks in the clock's
   * duration.
   */
  using rep = Rep;
  /**
   * @brief
   * [`std::ratio`](https://en.cppreference.com/w/cpp/numeric/ratio/ratio) type
   * representing  the tick period of the clock, in seconds.
   */
  using period = Period;
  constexpr static bool is_steady = steady;
  constexpr static auto clock_name = name;
  constexpr static auto instance_id = id;
  /// @brief Type of this class.
  using this_type =
      ManualClock<rep, period, is_steady, clock_name, instance_id>;

  /// @brief Type of durations for this clock.
  using duration = std::chrono::duration<rep, period>;
  /// @brief Type of time points for this clock.
  using time_point = std::chrono::time_point<this_type, duration>;

  /**
   * @brief Returns the current time of this clock.
   *
   * @return The current time of this clock.
   */
  constexpr static time_point now() { return instance().get_time_(); }

  /**
   * @brief Sets the current time of this clock.
   *
   * @param time time_point to set the clock to.
   * @return A reference to this clock.
   */
  constexpr static ManualClock& set_time(time_point const& time) {
    return instance().set_time_(time);
  }

  /**
   * @brief Adds a time duration to the current time of the clock.
   *
   * @param d duration to add.
   * @return A reference to this clock.
   */
  template <class Duration>
  constexpr static ManualClock& add_time(Duration&& d) {
    return instance().add_time_(
        static_cast<duration>(std::forward<Duration>(d)));
  }

  /**
   * @brief Sets the clock time to the epoch time of this clock.
   */
  constexpr static ManualClock& reset() { return set_time(time_point{}); }

  /**
   * @brief Notifies all the threads that are waiting on condition variables
   * from this clock.
   */
  constexpr static ManualClock& broadcast_change() {
    return instance().broadcast_change_();
  }

  /**
   * @brief Creates a thread that periodically notify all the condition
   * variables that are waiting on this clock with `wait_for` or `wait_until`.
   *
   * When a thread call `wait_for` or `wait_until`, it will do the following
   * (written as pseudocode):
   *
   * ```
   * while (clock_type::now() < expiration_time) {  // (1)
   *   wait(...);                                   // (2)
   * }
   * ```
   *
   * The `wait(...)` part will unblock when the clock time is changed.
   * However, if the clock time changes to a value higher than `expiration_time`
   * after (1) but before (2), the `wait(...)` call may not receive the
   * time-change notification and the thread will not unblock (until the next
   * time change).
   *
   * A periodic broadcaster solves this problem by periodically sending a
   * time-change notification regardless of any time changes.
   * Note that this does not break any correctness of the wait operation as
   * spurious wakeups are expected.
   *
   * @param start_time Time to start broadcasting notifications.
   *
   * @param period Period between two broadcasts.
   *
   * @return A
   * [`std::jthread`](https://en.cppreference.com/w/cpp/thread/jthread) that
   * periodically notifies all condition variables created from this clock.
   *
   * The caller should keep the returned thread object alive for as long as the
   * periodic broadcast is needed.
   */
  static std::jthread periodic_broadcaster(
      std::chrono::steady_clock::time_point start_time,
      std::chrono::steady_clock::duration period) {
    return std::jthread{[start_time = std::move(start_time),
                         period = std::move(period)](std::stop_token stoken) {
      std::this_thread::sleep_until(start_time);
      std::chrono::steady_clock::time_point wake_time = start_time + period;
      broadcast_change();
      while (!stoken.stop_requested()) {
        std::this_thread::sleep_until(wake_time);
        wake_time += period;
        broadcast_change();
      }
    }};
  }

  /**
   * @brief An overload of periodic_broadcaster() that assumes `start_time =
   * now()`.
   */
  static std::jthread periodic_broadcaster(
      std::chrono::steady_clock::duration period) {
    return periodic_broadcaster(std::chrono::steady_clock::now(), period);
  }

  /**
   * @brief An overload of periodic_broadcaster() that assumes `start_time =
   * now() + delay`.
   */
  static std::jthread periodic_broadcaster(
      std::chrono::steady_clock::duration delay,
      std::chrono::steady_clock::duration period) {
    return periodic_broadcaster(std::chrono::steady_clock::now() + delay,
                                period);
  }

  /**
   * @brief An analog of
   * [`std::condition_variable`](https://en.cppreference.com/w/cpp/thread/condition_variable)
   * that works with this ManualClock.
   *
   * This class exposes and interface similar to `std::condition_variable`, but
   * its wait_for() and wait_until() functions work on time types
   * (\ref duration and time_point) of this clock.
   */
  struct condition_variable {
    using clock_type =
        ManualClock<rep, period, is_steady, clock_name, instance_id>;
    using this_type = condition_variable;

    condition_variable() {}

    void notify_one() noexcept { base_cv_.notify_one(); }
    void notify_all() noexcept { base_cv_.notify_all(); }

    void wait(std::unique_lock<std::mutex>& lock) { base_cv_.wait(lock); }
    template <class Predicate>
    void wait(std::unique_lock<std::mutex>& lock, Predicate&& pred) {
      base_cv_.wait(lock, std::forward<Predicate>(pred));
    }

    template <class Clock, class Duration>
    std::cv_status wait_until(
        std::unique_lock<std::mutex>& lock,
        std::chrono::time_point<Clock, Duration> abs_time) {
      CvHandle<std::condition_variable> cv_handle{base_cv_};
      if (now() >= abs_time) {
        return std::cv_status::timeout;
      }
      base_cv_.wait(lock);
      if (now() >= abs_time) {
        return std::cv_status::timeout;
      }
      return std::cv_status::no_timeout;
    }

    template <class Clock, class Duration, class Predicate>
    bool wait_until(std::unique_lock<std::mutex>& lock,
                    std::chrono::time_point<Clock, Duration> abs_time,
                    Predicate pred) {
      while (!pred()) {
        if (wait_until(lock, abs_time) == std::cv_status::timeout) {
          return pred();
        }
      }
      return true;
    }

    template <class WaitRep, class WaitPeriod>
    std::cv_status wait_for(
        std::unique_lock<std::mutex>& lock,
        const std::chrono::duration<WaitRep, WaitPeriod>& rel_time) {
      return wait_until(lock, now() + rel_time);
    }

    template <class WaitRep, class WaitPeriod, class Predicate>
    bool wait_for(std::unique_lock<std::mutex>& lock,
                  const std::chrono::duration<WaitRep, WaitPeriod>& rel_time,
                  Predicate pred) {
      return wait_until(lock, now() + rel_time, std::move(pred));
    }

  protected:
    std::condition_variable base_cv_;
  };

  /**
   * @brief An analog of
   * [`std::condition_variable_any`](https://en.cppreference.com/w/cpp/thread/condition_variable_any)
   * that works with this ManualClock.
   *
   * This class exposes and interface similar to `std::condition_variable_any`,
   * but its wait_for() and wait_until() functions work on time types
   * (\ref duration and time_point) of this clock.
   */
  struct condition_variable_any {
    using clock_type =
        ManualClock<rep, period, is_steady, clock_name, instance_id>;
    using this_type = condition_variable_any;

    condition_variable_any() {}

    void notify_one() noexcept { base_cv_.notify_one(); }
    void notify_all() noexcept { base_cv_.notify_all(); }

    template <class Lock>
    void wait(Lock& lock) {
      base_cv_.wait(lock);
    }

    template <class Lock, class Predicate>
    void wait(Lock& lock, Predicate&& pred) {
      base_cv_.wait(lock, std::forward<Predicate>(pred));
    }

    template <class Lock, class StopToken, class Predicate>
    bool wait(Lock& lock, StopToken&& stoken, Predicate&& pred) {
      return base_cv_.wait(lock, std::forward<StopToken>(stoken),
                           std::forward<Predicate>(pred));
    }

    template <class Lock, class Clock, class Duration>

    std::cv_status wait_until(
        Lock& lock, std::chrono::time_point<Clock, Duration> abs_time) {
      CvHandle<std::condition_variable_any> cv_handle{base_cv_};
      if (now() >= abs_time) {
        return std::cv_status::timeout;
      }
      base_cv_.wait(lock);
      if (now() >= abs_time) {
        return std::cv_status::timeout;
      }
      return std::cv_status::no_timeout;
    }

    template <class Lock, class Clock, class Duration, class Predicate>
    bool wait_until(Lock& lock,
                    std::chrono::time_point<Clock, Duration> abs_time,
                    Predicate pred) {
      while (!pred()) {
        if (wait_until(lock, abs_time) == std::cv_status::timeout) {
          return pred();
        }
      }
      return true;
    }

    template <class Lock, class Clock, class Duration, class Predicate>
    bool wait_until(Lock& lock, std::stop_token stoken,
                    std::chrono::time_point<Clock, Duration> abs_time,
                    Predicate pred) {
      while (!stoken.stop_requested()) {
        if (pred()) return true;
        if (wait_until(lock, abs_time) == std::cv_status::timeout)
          return pred();
      }
      return pred();
    }

    template <class Lock, class WaitRep, class WaitPeriod>
    std::cv_status wait_for(
        Lock& lock,
        const std::chrono::duration<WaitRep, WaitPeriod>& rel_time) {
      return wait_until(lock, now() + rel_time);
    }

    template <class Lock, class WaitRep, class WaitPeriod, class Predicate>
    bool wait_for(Lock& lock,
                  const std::chrono::duration<WaitRep, WaitPeriod>& rel_time,
                  Predicate pred) {
      return wait_until(lock, now() + rel_time, std::move(pred));
    }

    template <class Lock, class WaitRep, class WaitPeriod, class Predicate>
    bool wait_for(Lock& lock, std::stop_token stoken,
                  const std::chrono::duration<WaitRep, WaitPeriod>& rel_time,
                  Predicate pred) {
      return wait_until(lock, std::move(stoken), now() + rel_time,
                        std::move(pred));
    }

  protected:
    std::condition_variable_any base_cv_;
  };

protected:
  static this_type& instance() {
    static this_type clock{};
    return clock;
  }

  time_point now_{};
  std::mutex mutex_;

  template <class Cv = std::condition_variable>
  using CvList = std::list<std::reference_wrapper<Cv>>;

  /// @cond INTERNAL
  template <class Cv = std::condition_variable>
  struct CvHandle {
    using condition_variable = Cv;
    using List = std::list<std::reference_wrapper<condition_variable>>;
    using Iterator = List::iterator;

    Iterator iterator;
    CvHandle(condition_variable& cv) : iterator{instance().register_cv(cv)} {}
    ~CvHandle() { instance().unregister_cv(*this); }
  };
  /// @endcond

  CvList<std::condition_variable> base_cvs_;
  CvList<std::condition_variable_any> base_cv_anys_;

  void broadcast_change_locked_() {
    for (auto& base_cv : base_cvs_) {
      base_cv.get().notify_all();
    }
    for (auto& base_cv_any : base_cv_anys_) {
      base_cv_any.get().notify_all();
    }
  }

  time_point get_time_() {
    std::scoped_lock l{mutex_};
    return now_;
  }

  ManualClock& set_time_(time_point const& time) {
    std::scoped_lock l{mutex_};
    now_ = time;
    broadcast_change_locked_();
    return *this;
  }

  ManualClock& add_time_(duration const& duration) {
    std::scoped_lock l{mutex_};
    now_ += duration;
    broadcast_change_locked_();
    return *this;
  }

  ManualClock& broadcast_change_() {
    std::scoped_lock l{mutex_};
    broadcast_change_locked_();
    return *this;
  }

  template <class Cv>
  CvHandle<Cv>::Iterator register_cv(Cv& cv) {
    std::scoped_lock l{mutex_};
    if constexpr (std::is_same_v<Cv, std::condition_variable>) {
      base_cvs_.emplace_front(std::ref(cv));
      return base_cvs_.begin();
    } else {
      base_cv_anys_.emplace_front(std::ref(cv));
      return base_cv_anys_.begin();
    }
  }
  template <class Handle>
  void unregister_cv(Handle& handle) {
    std::scoped_lock l{mutex_};
    if constexpr (std::is_same_v<typename Handle::condition_variable,
                                 std::condition_variable>) {
      base_cvs_.erase(handle.iterator);
    } else {
      base_cv_anys_.erase(handle.iterator);
    }
  }
};

export constexpr char const DEFAULT_MANUAL_CLOCK_NAME[] =
    "coroutini::utils::DefaultManualClock";

export template <std::size_t id = -1,
                 char const* name = DEFAULT_MANUAL_CLOCK_NAME,
                 bool steady = false>
using DefaultManualClock = ManualClock<long long, std::nano, steady, name, id>;

}  // namespace coroutini::utils
