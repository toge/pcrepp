#include "catch2/catch_all.hpp"
#include "pcrepp.hpp"
#include <vector>
#include <string_view>

TEST_CASE("Zero-width matches", "[zero_width]") {
    using namespace std::string_view_literals;

    SECTION("find_all with a*") {
        auto ctx_res = pcrepp::context<>::create(R"(a*)");
        REQUIRE(ctx_res.has_value());
        auto const& ctx = *ctx_res;

        auto const target = "baac"sv;
        auto results = ctx.find_all(target);
        std::vector<std::string_view> matches;
        for (auto const& mr : results) {
            matches.push_back(mr[0]);
        }

        // b (pos 0): match ""
        // aa (pos 1): match "aa"
        // c (pos 3): match ""
        // end (pos 4): match ""
        REQUIRE(matches.size() == 4);
        CHECK(matches[0] == "");
        CHECK(matches[1] == "aa");
        CHECK(matches[2] == "");
        CHECK(matches[3] == "");
    }

    SECTION("find_all with (.*)") {
        auto ctx_res = pcrepp::context<>::create(R"((.*))");
        REQUIRE(ctx_res.has_value());
        auto const& ctx = *ctx_res;

        auto const target = "abc"sv;
        auto results = ctx.find_all(target);
        std::vector<std::string_view> matches;
        for (auto const& mr : results) {
            matches.push_back(mr[0]);
        }

        // abc (pos 0): match "abc"
        // end (pos 3): match ""
        REQUIRE(matches.size() == 2);
        CHECK(matches[0] == "abc");
        CHECK(matches[1] == "");
    }

    SECTION("split with zero-width match") {
        auto ctx_res = pcrepp::context<>::create(R"()"); // empty pattern matches everywhere
        REQUIRE(ctx_res.has_value());
        auto const& ctx = *ctx_res;

        auto const target = "abc"sv;
        auto parts = ctx.split(target);

        // Expected behavior for zero-width split:
        // Match at 0: ""
        // Match at 1: "a"
        // Match at 2: "b"
        // Match at 3: "c"
        // After loop: ""
        REQUIRE(parts.size() == 5);
        CHECK(parts[0] == "");
        CHECK(parts[1] == "a");
        CHECK(parts[2] == "b");
        CHECK(parts[3] == "c");
        CHECK(parts[4] == "");
    }

    SECTION("zero-length capture groups") {
        {
            auto ctx_res = pcrepp::context<>::create(R"(a(.*)b)");
            REQUIRE(ctx_res.has_value());
            auto const& ctx = *ctx_res;

            auto const target = "ab"sv;
            auto results = ctx.find_all(target);
            auto it = results.begin();

            REQUIRE(it != results.end());
            auto const& mr = *it;
            CHECK(mr[0] == "ab");
            CHECK(mr[1] == "");
            CHECK(mr[1].empty());
        }
        {
            auto ctx_res = pcrepp::context<>::create(R"(^(.*)a)");
            REQUIRE(ctx_res.has_value());
            auto const& ctx = *ctx_res;

            auto const target = "a"sv;
            auto results = ctx.find_all(target);
            auto it = results.begin();

            REQUIRE(it != results.end());
            auto const& mr = *it;
            CHECK(mr[0] == "a");
            CHECK(mr[1] == "");
        }
        {
            auto ctx_res = pcrepp::context<>::create(R"(a(.*)$)");
            REQUIRE(ctx_res.has_value());
            auto const& ctx = *ctx_res;

            auto const target = "a"sv;
            auto results = ctx.find_all(target);
            auto it = results.begin();

            REQUIRE(it != results.end());
            auto const& mr = *it;
            CHECK(mr[0] == "a");
            CHECK(mr[1] == "");
        }
    }

    SECTION("split with capture groups (0-length)") {
        auto ctx_res = pcrepp::context<>::create(R"((,))");
        REQUIRE(ctx_res.has_value());
        auto const& ctx = *ctx_res;

        auto const target = "a,b"sv;
        auto parts = ctx.split(target);

        REQUIRE(parts.size() == 2);
        CHECK(parts[0] == "a");
        CHECK(parts[1] == "b");
    }
}
