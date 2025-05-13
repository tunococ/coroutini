module;

#include <coroutine>
#include <exception>
#include <optional>
#include <type_traits>
#include <variant>

export module coroutini_future:future;

import :utils;

namespace coroutini::future {

export template <class Result>
struct ResultHolder {
  Result result;

  constexpr void return_value(Result value) { result = value; }
};

template <>
struct ResultHolder<void> {
  std::optional<std::monostate> result;
  void return_void() {}
};

export template <class Value, bool eager = false>
struct Future {
  using value_type = Value;
  static constexpr bool is_eager = eager;
  using future_type = Future<Value, eager>;

  struct promise_type : ResultHolder<value_type> {
    using handle_type = std::coroutine_handle<promise_type>;
    using storage_type = std::conditional_t<std::is_void_v<value_type>,
                                            value_type, std::monostate>;

    future_type get_return_object() { return future_type{*this}; }

    std::conditional_t<is_eager, std::suspend_never, std::suspend_always>
    initial_suspend() noexcept {
      return {};
    }

    struct FinalAwaiter {
      bool await_ready() noexcept { return false; }
      void await_resume() noexcept {}
      std::coroutine_handle<> await_suspend(handle_type h) noexcept {
        if (std::coroutine_handle<> caller = h.promise().caller; caller) {
          return caller;
        }
        return std::noop_coroutine();
      }
    };

    FinalAwaiter final_suspend() noexcept { return {}; }

    void unhandled_exception() { exception = std::current_exception(); }

    std::exception_ptr exception;
    std::coroutine_handle<> caller;
  };

  using handle_type = std::coroutine_handle<promise_type>;

  struct Awaiter {
    handle_type handle;
    bool await_ready() noexcept { return false; }
    value_type await_resume() {
      if constexpr (std::is_void_v<value_type>) {
        return;
      } else {
        return std::move(handle.promise().result);
      }
    }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) {
      promise_type& p = handle.promise();
      p.caller = caller;
      return handle;
    }
  };
  Awaiter operator co_await() { return Awaiter{handle_}; }

  constexpr operator bool() const noexcept { return handle_.operator bool(); }
  constexpr bool valid() const noexcept { return operator bool(); }

  bool step() {
    if (handle_ && !handle_.done()) {
      handle_.resume();
      return true;
    }
    return false;
  }

  void wait() {
    while (handle_ && !handle_.done()) {
      handle_.resume();
    }
  }

  std::add_lvalue_reference_t<value_type> get() {
    wait();
    if (auto exception = handle_.promise().exception; exception) {
      std::rethrow_exception(exception);
    }
    if constexpr (std::is_void_v<value_type>) {
      return;
    } else {
      return handle_.promise().result;
    }
  }

  std::add_lvalue_reference_t<value_type> operator*() { return get(); }

  std::optional<std::add_lvalue_reference_t<value_type>> try_get_value() const
    requires(!std::is_void_v<value_type>)
  {
    if (!handle_) {
      return {};
    }
    auto promise = handle_.promise();
    if (!promise->done()) {
      return {};
    }
    if (auto exception = promise.exception; exception) {
      std::rethrow_exception(exception);
    }
    if constexpr (std::is_void_v<value_type>) {
      return;
    } else {
      return promise.result;
    }
  }

  std::exception_ptr get_exception() const {
    if (!handle_) {
      return nullptr;
    }
    return handle_.promise().exception ?: nullptr;
  }

  Future(Future const&) = delete;
  Future(Future&& other) : handle_{std::move(other.handle_)} {
    other.handle_ = nullptr;
  }
  ~Future() {
    if (handle_) {
      handle_.destroy();
    }
  }

  future_type& operator=(future_type const&) = delete;
  future_type& operator=(future_type&& other) {
    if (handle_ && (handle_ != other.handle_)) {
      handle_.destroy();
    }
    handle_ = other.handle_;
    other.handle_ = nullptr;
    return *this;
  }
  future_type& operator=(std::nullptr_t) noexcept {
    handle_ = nullptr;
    return *this;
  }

  template <class ThenFunc>
    requires(std::is_void_v<value_type>)
  Future<void> then(ThenFunc&& then_func) {
    handle_type current_handle = handle_;
    handle_ = nullptr;
    return [](future_type current,
              std::remove_reference_t<ThenFunc> then_func) -> Future<void> {
      co_await current;
      co_return then_func();
    }(current_handle.promise(), std::forward<ThenFunc>(then_func));
  }

  template <class ThenFunc>
    requires(!std::is_void_v<value_type>)
  Future<std::invoke_result_t<ThenFunc, value_type>> then(
      ThenFunc&& then_func) {
    using Result = std::invoke_result_t<ThenFunc, value_type>;
    handle_type current_handle = handle_;
    handle_ = nullptr;
    return [](future_type current,
              std::remove_reference_t<ThenFunc> then_func) -> Future<Result> {
      co_return then_func(co_await current);
    }(current_handle.promise(), std::forward<ThenFunc>(then_func));
  }

protected:
  friend promise_type;
  std::coroutine_handle<promise_type> handle_;

  Future(promise_type& promise)
      : handle_{std::coroutine_handle<promise_type>::from_promise(promise)} {}
};

export template <class Func>
Future<std::invoke_result_t<Func>> lazy(Func func) {
  using Result = std::invoke_result_t<Func>;
  if constexpr (std::is_void_v<Result>) {
    co_return func();
  } else {
    co_return std::forward<Result>(func());
  }
}

export template <class Func>
Future<std::invoke_result_t<Func>, true> eager(Func func) {
  using Result = std::invoke_result_t<Func>;
  if constexpr (std::is_void_v<Result>) {
    co_return func();
  } else {
    co_return std::forward<Result>(func());
  }
}

}  // namespace coroutini::future
