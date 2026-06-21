#include "pcrepp.hpp"
#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <vector>

TEST_CASE("TLS reuse with use_tls option", "[tls]") {
    using namespace pcrepp;
    auto re = R"((\w+):(\d+))"_re;
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

    // C7: find returns expected, use find_unchecked for brevity
    auto [m1, w1, k1, v1] = pcrepp::find_unchecked<R"((\w+):(\d+))">(target1);
    CHECK(m1);
    CHECK(k1 == "first");

    auto [m2, w2, k2, v2] = pcrepp::find_unchecked<R"((\w+):(\d+))">(target2);
    CHECK(m2);
    CHECK(k2 == "second");

    // 先にコピーした k1 がまだ有効か確認
    CHECK(k1 == "first");
}

TEST_CASE("TLS reuse with different patterns", "[tls]") {
    using namespace pcrepp;

    // Pattern with 2 groups
    auto [m1, w1, g1, g2] = pcrepp::find_unchecked<R"((\w+)-(\w+))">("hello-world");
    CHECK(m1);
    CHECK(g1 == "hello");
    CHECK(g2 == "world");

    // Pattern with 1 group (should reuse and work fine)
    auto [m2, w2, g3] = pcrepp::find_unchecked<R"((\d+))">("123");
    CHECK(m2);
    CHECK(g3 == "123");

    // Pattern with more groups
    auto [m3, w3, a, b, c] = pcrepp::find_unchecked<R"((\w):(\w):(\w))">("x:y:z");
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
    auto const& big_ctx = *big_ctx_res;
    auto const& small_ctx = *small_ctx_res;

    for (auto const _ : std::views::iota(0, 5000)) {
        std::ignore = _;
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
