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
        auto [matched, whole, key, value] = pcrepp::find<R"((\w+):(\d+))">(target);
        CHECK(matched);
        CHECK(key == "age");
        CHECK(value == "30");
    }
}

TEST_CASE("TLS reuse across multiple calls", "[tls]") {
    using namespace pcrepp;
    auto const target1 = "first:100";
    auto const target2 = "second:200";

    auto [m1, w1, k1, v1] = pcrepp::find<R"((\w+):(\d+))">(target1);
    CHECK(m1);
    CHECK(k1 == "first");

    auto [m2, w2, k2, v2] = pcrepp::find<R"((\w+):(\d+))">(target2);
    CHECK(m2);
    CHECK(k2 == "second");
    
    // Check that previous result (which was copied) is still valid
    CHECK(k1 == "first");
}

TEST_CASE("TLS reuse with different patterns", "[tls]") {
    using namespace pcrepp;
    
    // Pattern with 2 groups
    auto [m1, w1, g1, g2] = pcrepp::find<R"((\w+)-(\w+))">("hello-world");
    CHECK(m1);
    CHECK(g1 == "hello");
    CHECK(g2 == "world");

    // Pattern with 1 group (should reuse and work fine)
    auto [m2, w2, g3] = pcrepp::find<R"((\d+))">("123");
    CHECK(m2);
    CHECK(g3 == "123");

    // Pattern with more groups
    auto [m3, w3, a, b, c] = pcrepp::find<R"((\w):(\w):(\w))">("x:y:z");
    CHECK(m3);
    CHECK(a == "x");
    CHECK(b == "y");
    CHECK(c == "z");
}
