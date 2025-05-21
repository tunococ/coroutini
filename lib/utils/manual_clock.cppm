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

constexpr static char const MANUAL_CLOCK_DEFAULT_NAME[] =
    "coroutini::utils::ManualClock";

export template <
    class Rep = std::chrono::steady_clock::rep,
    class Period = std::chrono::steady_clock::period, bool steady = false,
    char const* name = MANUAL_CLOCK_DEFAULT_NAME, std::size_t id = -1>
struct ManualClock {
  using rep = Rep;
  using period = Period;
  constexpr static bool is_steady = steady;
  constexpr static auto clock_name = name;
  constexpr static auto instance_id = id;
  using this_type =
      ManualClock<rep, period, is_steady, clock_name, instance_id>;

  using duration = std::chrono::duration<rep, period>;
  using time_point = std::chrono::time_point<this_type, duration>;

  constexpr static time_point now() { return instance().get_time_(); }

  constexpr static ManualClock& set_time(time_point const& time) {
    return instance().set_time_(time);
  }

  template <class Duration>
  constexpr static ManualClock& add_time(Duration&& d) {
    return instance().add_time_(
        static_cast<duration>(std::forward<Duration>(d)));
  }

  constexpr static ManualClock& reset() { return set_time(time_point{}); }

  constexpr static ManualClock& broadcast_change() {
    return instance().broadcast_change_();
  }

  /**
   * A thread that periodically notifies all the condition variables that are
   * waiting on this clock with `wait_for` or `wait_until`.
   *
   * This is necessary because the clock time may be changed around the same
   * time that `wait_for` or `wait_until` is called, and the time-change
   * notification may be missed by the wait call. (Note that although `wait_for`
   * and `wait_until` will check the clock time before waiting on the condition
   * variable, the wait operation does not follow the time check atomically. If
   * the clock time changes between the time check and the wait operation, the
   * wait operation will not receive the time change notification.)
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

  static std::jthread periodic_broadcaster(
      std::chrono::steady_clock::duration period) {
    return periodic_broadcaster(std::chrono::steady_clock::now(), period);
  }

  static std::jthread periodic_broadcaster(
      std::chrono::steady_clock::duration delay,
      std::chrono::steady_clock::duration period) {
    return periodic_broadcaster(std::chrono::steady_clock::now() + delay,
                                period);
  }

  struct condition_variable {
    using clock = this_type;
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
        const std::chrono::time_point<Clock, Duration>& abs_time) {
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
                    const std::chrono::time_point<Clock, Duration>& abs_time,
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

  struct condition_variable_any {
    using clock = this_type;
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
        Lock& lock, const std::chrono::time_point<Clock, Duration>& abs_time) {
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
                    const std::chrono::time_point<Clock, Duration>& abs_time,
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
                    const std::chrono::time_point<Clock, Duration>& abs_time,
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

  time_point now_;
  std::mutex mutex_;

  template <class Cv = std::condition_variable>
  using CvList = std::list<std::reference_wrapper<Cv>>;

  template <class Cv = std::condition_variable>
  struct CvHandle {
    using condition_variable = Cv;
    using List = std::list<std::reference_wrapper<condition_variable>>;
    using Iterator = List::iterator;

    Iterator iterator;
    CvHandle(condition_variable& cv) : iterator{instance().register_cv(cv)} {}
    ~CvHandle() { instance().unregister_cv(*this); }
  };

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

constexpr static char const DEFAULT_MANUAL_CLOCK_NAME[] =
    "coroutini::utils::DefaultManualClock";
export template <std::size_t id = -1,
                 char const* name = DEFAULT_MANUAL_CLOCK_NAME,
                 bool steady = false>
using DefaultManualClock = ManualClock<long long, std::nano, steady, name, id>;

}  // namespace coroutini::utils
