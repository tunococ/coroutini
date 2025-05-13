#include <catch2/catch_test_macros.hpp>
#include <coroutine>
#include <functional>
#include <iostream>

import coroutini;

using namespace coroutini::future;

TEST_CASE("Future can defer computation", "[future]") {
  std::vector<int> values;

  auto appendAndReturn = [&values](int value) {
    return [&values, value]() {
      values.push_back(value);
      return value;
    };
  };

  Future<int> a = lazy(appendAndReturn(3));
  Future<int> b = lazy(appendAndReturn(5));
  auto c = [&]() -> Future<int> {
    int v1 = co_await a;
    int v2 = co_await b;
    co_return appendAndReturn(v1 + v2)();
  }();

  values.push_back(1);
  CHECK(*c == 8);
  CHECK(values == std::vector{1, 3, 5, 8});
}

TEST_CASE("Future can hold an exception", "[future]") {
  auto throwIfZero = [&](int value) {
    return lazy([&]() {
      if (!value) {
        throw 1;
      }
      return value;
    });
  };
  CHECK(throwIfZero(1).get() == 1);

  int caught_exception = 0;
  try {
    int value = throwIfZero(0).get();
  } catch (int exception) {
    caught_exception = exception;
  }
  CHECK(caught_exception == 1);
}

TEST_CASE("Future can return void", "[future]") {
  std::vector<int> values;

  auto a = lazy([&]() { values.push_back(0); });
  auto b = lazy([&]() { values.push_back(1); });
  CHECK(values.empty());
  b.get();
  CHECK(values == std::vector<int>{1});
  a.get();
  CHECK(values == std::vector<int>{1, 0});
}

TEST_CASE("Eager Future will resolve immediately", "[future]") {
  std::vector<int> values;

  auto a = eager([&]() { values.push_back(0); });
  CHECK(values == std::vector<int>{0});

  auto b = lazy([&]() { values.push_back(1); });
  b.get();
  a.get();
  CHECK(values == std::vector<int>{0, 1});
}

TEST_CASE("co_await can be called from a void Future", "[future]") {
  std::vector<int> values;

  auto appendAndReturn = [&values](int value) -> Future<int> {
    return lazy([&values, value] {
      values.push_back(value);
      return value;
    });
  };

  auto task = [&]() -> Future<void> {
    int a = co_await appendAndReturn(100);
    int b = co_await appendAndReturn(200);
    co_await appendAndReturn(a + b);
  }();

  CHECK(values.empty());
  task.wait();
  CHECK(values == std::vector{100, 200, 300});
}

TEST_CASE("Future can be moved", "[future]") {
  int value = 0;

  auto a = lazy([&]() { return value += 1; });
  auto b = std::move(a);
  auto c = lazy([&]() { return value += 2; });
  c = std::move(b);

  CHECK(!a);
  CHECK(!b);
  CHECK(c);
  CHECK(c.get() == 1);
}

TEST_CASE("Futures can be chained with then", "[future]") {
  using List = std::vector<int>;
  std::size_t flags = 0;
  auto a = lazy([&flags]() {
             flags += 1;
             return List{0};
           })
               .then([&flags](auto list) {
                 flags += 2;
                 list.push_back(1);
                 return list;
               })
               .then([&flags](auto list) {
                 flags += 4;
                 list.push_back(2);
                 return list;
               })
               .then([&flags](auto list) {
                 flags += 8;
                 list.push_back(3);
                 return list;
               });
  CHECK(flags == 0);
  auto list = *a;
  CHECK(flags == 15);
  CHECK(list.size() == 4);
  CHECK(list == List{0, 1, 2, 3});
}

TEST_CASE("Exception can be caught with catch", "[future]") {}
