module;

#include <tuple>

export module coroutini_future:utils;

namespace coroutini::future::utils {

export template <class P>
struct MemberFunctionTraits;

export template <class Class, class Return, class... Args>
struct MemberFunctionTraits<Return (Class::*)(Args...)> {
  using class_type = Class;
  using return_type = Return;
  using arg_types = std::tuple<Args...>;
  template <std::size_t index>
  using arg_type = std::tuple_element_t<index, arg_types>;
};

export template <class Promise>
concept HasReturnValue = requires {
  typename MemberFunctionTraits<decltype(&Promise::return_value)>;
};

export template <class Promise>
concept HasReturnVoid = requires {
  typename MemberFunctionTraits<decltype(&Promise::return_void)>;
};

export template <class Promise>
struct PromiseTraits;

template <HasReturnValue Promise>
struct PromiseTraits<Promise> {
  using return_value_function_type = decltype(&Promise::return_value);
  using result_type =
      MemberFunctionTraits<return_value_function_type>::template arg_type<0>;
  constexpr static bool has_value = true;
};

template <HasReturnVoid Promise>
struct PromiseTraits<Promise> {
  using result_type = void;
  constexpr static bool has_value = false;
};

export template <class Promise>
using PromiseResultType = PromiseTraits<Promise>::result_type;

export template <class Promise>
struct PromiseResultTypeHelper {
  template <typename T>
  static auto test(T&& value) -> std::pair<
      T, decltype(std::declval<Promise>().return_value(std::forward<T>()))>;
  using result_type = decltype(test(std::declval<Promise>()).first);
};

}  // namespace coroutini::future::utils
