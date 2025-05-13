#include <catch2/catch_test_macros.hpp>

import coroutini_utils;

namespace {

using coroutini::utils::Defer;

TEST_CASE("defer", "[utils][defer]") {
  int a = 1;
  {
    Defer addOne([&]() { ++a; });
    a *= 2;
  }
  CHECK(a == 3);
};

}  // namespace
