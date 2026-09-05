#include "pcrepp.hpp"
#include <catch2/catch_test_macros.hpp>
#include <pcre2.h>
#include <vector>
#include <string_view>

TEST_CASE("context::find_all with options", "[find_all][options]") {
    using namespace pcrepp;
    auto ctx_res = context<>::create("abc");
    REQUIRE(ctx_res);
    auto& ctx = *ctx_res;

    std::string_view target = "abc abc";

    SECTION("Without options") {
        std::vector<std::string_view> matches;
        for (auto const& mr : ctx.find_all(target)) {
            matches.push_back(mr[0]);
        }
        CHECK(matches.size() == 2);
    }

    SECTION("With PCRE2_ANCHORED") {
        // This should only match at the start of the search (pos 0, then pos 3, etc.)
        // For "abc abc", first match at 0. Next search starts at 4 (after space).
        // Since it's anchored, "abc" at 4 won't match if we start at 4 but it's not anchored?
        // Wait, PCRE2_ANCHORED means it MUST match at the start offset.
        
        // Let's use PCRE2_NOTBOL with ^
        auto ctx_res2 = pcrepp::context<>::create("^abc");
        REQUIRE(ctx_res2);
        auto& ctx2 = *ctx_res2;

        SECTION("Default ^ matches at start") {
            auto matches = ctx2.find_all("abc");
            auto it = matches.begin();
            REQUIRE(it != matches.end());
            CHECK((*it)[0] == "abc");
            ++it;
            CHECK(it == matches.end());
        }

        SECTION("With PCRE2_NOTBOL ^ should not match") {
            auto matches = ctx2.find_all("abc", PCRE2_NOTBOL);
            CHECK(matches.begin() == matches.end());
        }
    }
}

TEST_CASE("nttp_regex::find_all with options", "[nttp][find_all][options]") {
    using namespace pcrepp;
    static constexpr auto re = "^abc"_re;

    SECTION("Without options") {
        auto matches_res = re.find_all("abc");
        REQUIRE(matches_res.has_value());
        CHECK(std::ranges::distance(*matches_res) > 0uz);
    }

    SECTION("With PCRE2_NOTBOL") {
        auto matches_res = re.find_all("abc", PCRE2_NOTBOL);
        REQUIRE(matches_res.has_value());
        CHECK(std::ranges::distance(*matches_res) == 0uz);
    }
}

#ifdef PCREPP_HAS_FROZENCHARS
TEST_CASE("find_all_frozen with options", "[frozenchars][find_all][options]") {
    using namespace pcrepp;
    using namespace frozenchars::literals;
    static constexpr auto pattern = "^abc"_fs;

    SECTION("Without options") {
        auto matches_res = find_all_frozen<pattern>("abc");
        REQUIRE(matches_res.has_value());
        CHECK(std::ranges::distance(*matches_res) > 0uz);
    }

    SECTION("With PCRE2_NOTBOL") {
        auto matches_res = find_all_frozen<pattern>("abc", PCRE2_NOTBOL);
        REQUIRE(matches_res.has_value());
        CHECK(std::ranges::distance(*matches_res) == 0uz);
    }
}
#endif

TEST_CASE("NTTP find_all with options", "[nttp][find_all][options]") {
    using namespace pcrepp;
    auto results_res = pcrepp::find_all<"^abc">("abc", PCRE2_NOTBOL);
    REQUIRE(results_res.has_value());
    auto const& results = *results_res;
    CHECK(std::ranges::distance(results) == 0uz);
}

TEST_CASE("option set covers multiline dotall extended noteol", "[options][h4]") {
    auto const multiline_ctx = pcrepp::context<>::create(R"(^\w+)", PCRE2_MULTILINE).value();
    auto multiline_count = 0uz;
    for ([[maybe_unused]] auto const& mr : multiline_ctx.find_all("foo\nbar\nbaz")) {
        ++multiline_count;
    }
    CHECK(multiline_count == 3uz);

    auto const dotall_ctx = pcrepp::context<>::create(R"(a.b)", PCRE2_DOTALL).value();
    auto const dotall_res = dotall_ctx.find("a\nb");
    REQUIRE(dotall_res);
    CHECK(dotall_res->get(0uz) == "a\nb");

    auto const extended_ctx = pcrepp::context<>::create(R"(a \s+ b)", PCRE2_EXTENDED).value();
    auto const extended_res = extended_ctx.find("a   b");
    REQUIRE(extended_res);
    CHECK(extended_res->get(0uz) == "a   b");

    auto const noteol_ctx = pcrepp::context<>::create(R"(\d+$)").value();
    auto const noteol_res = noteol_ctx.find("123", 0uz, PCRE2_NOTEOL);
    REQUIRE(noteol_res);
    CHECK_FALSE(static_cast<bool>(*noteol_res));
}

TEST_CASE("no_utf_check constant usage", "[options][h15]") {
    auto const ctx = pcrepp::context<>::create(R"(\w+)", PCRE2_UTF).value();
    auto const res = ctx.find("hello", 0uz, pcrepp::no_utf_check);
    REQUIRE(res);
    CHECK(res->get(0uz) == "hello");
}
