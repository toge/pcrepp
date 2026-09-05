#include "pcrepp.hpp"
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <vector>

TEST_CASE("TLS reuse with use_tls option", "[tls]") {
  using namespace pcrepp;
  auto const target = "age:30";

  SECTION("Explicit use_tls") {
    auto ctx_res = context<>::create(R"((\w+):(\d+))");
    REQUIRE(ctx_res);
    auto const& ctx = *ctx_res;

    auto res = ctx.find(target, use_tls);
    REQUIRE(res);
    CHECK((*res)[1] == "age");
    CHECK((*res)[2] == "30");
  }

  SECTION("NTTP find uses TLS automatically") {
    // C7: find returns expected
    auto res = pcrepp::find<R"((\w+):(\d+))">(target);
    REQUIRE(res);
    auto [matched, whole, key, value] = *res;
    CHECK(matched);
    CHECK(key == "age");
    CHECK(value == "30");
  }
}

TEST_CASE("TLS reuse across multiple calls", "[tls]") {
  using namespace pcrepp;
  auto const target1 = "first:100";
  auto const target2 = "second:200";

  auto const r1 = pcrepp::find_unchecked<R"((\w+):(\d+))">(target1);
  REQUIRE(r1.has_value());
  auto [m1, w1, k1, v1] = *r1;
  CHECK(m1);
  CHECK(k1 == "first");

  auto const r2 = pcrepp::find_unchecked<R"((\w+):(\d+))">(target2);
  REQUIRE(r2.has_value());
  auto [m2, w2, k2, v2] = *r2;
  CHECK(m2);
  CHECK(k2 == "second");

  // 先にコピーした k1 がまだ有効か確認
  CHECK(k1 == "first");
}

TEST_CASE("TLS reuse with different patterns", "[tls]") {
  using namespace pcrepp;

  // Pattern with 2 groups
  auto const r1 = pcrepp::find_unchecked<R"((\w+)-(\w+))">("hello-world");
  REQUIRE(r1.has_value());
  auto [m1, w1, g1, g2] = *r1;
  CHECK(m1);
  CHECK(g1 == "hello");
  CHECK(g2 == "world");

  // Pattern with 1 group (should reuse and work fine)
  auto const r2 = pcrepp::find_unchecked<R"((\d+))">("123");
  REQUIRE(r2.has_value());
  auto [m2, w2, g3] = *r2;
  CHECK(m2);
  CHECK(g3 == "123");

  // Pattern with more groups
  auto const r3 = pcrepp::find_unchecked<R"((\w):(\w):(\w))">("x:y:z");
  REQUIRE(r3.has_value());
  auto [m3, w3, a, b, c] = *r3;
  CHECK(m3);
  CHECK(a == "x");
  CHECK(b == "y");
  CHECK(c == "z");
}

TEST_CASE("TLS reuse does not overflow when capture counts shrink", "[tls][stress]") {
  using namespace pcrepp;

  auto big_pattern = std::string{};
  for (auto const _ : std::views::iota(0, 80)) {
    std::ignore = _;
    big_pattern += "(a)";
  }

  auto big_target = std::string(80, 'a');

  auto const big_ctx_res = context<>::create(big_pattern);
  REQUIRE(big_ctx_res);
  auto const small_ctx_res = context<>::create(R"((\d+))");
  REQUIRE(small_ctx_res);
  auto const& big_ctx   = *big_ctx_res;
  auto const& small_ctx = *small_ctx_res;

  for (auto const _ : std::views::iota(0, 5000)) {
    std::ignore  = _;
    auto big_res = big_ctx.find(big_target, use_tls);
    REQUIRE(big_res);
    REQUIRE(*big_res);
    CHECK((*big_res).get(1) == "a");

    auto small_res = small_ctx.find("123", use_tls);
    REQUIRE(small_res);
    REQUIRE(*small_res);
    CHECK((*small_res).get(1) == "123");
  }
}

TEST_CASE("TLS multithreaded concurrent find", "[tls][multithreaded]") {
#ifndef __EMSCRIPTEN__
  using namespace pcrepp;
  auto const ctx1    = context<>::create(R"((\w+):(\d+))").value();
  auto const ctx2    = context<>::create(R"((\d+))").value();
  auto const n       = 24;
  auto       errors  = std::atomic<int>{0};
  auto       threads = std::vector<std::thread>{};
  threads.reserve(static_cast<size_t>(n));
  for (auto const i : std::views::iota(0, n)) {
    threads.emplace_back([&, i] {
      auto const& ctx    = (i % 2 == 0) ? ctx1 : ctx2;
      auto const  target = (i % 2 == 0) ? "key:42" : "123";
      for (auto const _ : std::views::iota(0, 200)) {
        std::ignore    = _;
        auto const res = ctx.find(target, use_tls);
        if (not res || not *res) {
          ++errors;
        }
      }
    });
  }
  for (auto& t : threads) {
    if (t.joinable())
      t.join();
  }
  threads.clear();  // join
  CHECK(errors.load() == 0);
#else
  // Emscripten: std::thread 未対応のためスキップ
  CHECK(true);
#endif
}
