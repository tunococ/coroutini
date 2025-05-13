module;

#include <utility>

export module coroutini_utils:defer;

namespace coroutini::utils {

export template <class Function>
struct Defer {
  Function function;
  Defer(Function&& function) : function{std::forward<Function>(function)} {}
  ~Defer() { std::forward<Function>(function)(); }
};

}  // namespace coroutini::utils
