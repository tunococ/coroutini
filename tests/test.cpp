#include <catch2/catch_test_macros.hpp>

import coroutini;

TEST_CASE("say_hello returns something", "") {
  REQUIRE(coroutini::say_hello() != nullptr);
}
