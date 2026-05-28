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
        auto matches = re.find_all("abc");
        CHECK(matches.begin() != matches.end());
    }

    SECTION("With PCRE2_NOTBOL") {
        auto matches = re.find_all("abc", PCRE2_NOTBOL);
        CHECK(matches.begin() == matches.end());
    }
}

#ifdef PCREPP_HAS_FROZENCHARS
TEST_CASE("find_all_frozen with options", "[frozenchars][find_all][options]") {
    using namespace pcrepp;
    using namespace frozenchars::literals;
    static constexpr auto pattern = "^abc"_fs;
    
    SECTION("Without options") {
        auto matches = find_all_frozen<pattern>("abc");
        CHECK(matches.begin() != matches.end());
    }

    SECTION("With PCRE2_NOTBOL") {
        auto matches = find_all_frozen<pattern>("abc", PCRE2_NOTBOL);
        CHECK(matches.begin() == matches.end());
    }
}
#endif

TEST_CASE("NTTP find_all with options", "[nttp][find_all][options]") {
    using namespace pcrepp;
    std::string_view target = "abc";
    
    auto results = pcrepp::find_all<"^abc">("abc", PCRE2_NOTBOL);
    CHECK(results.begin() == results.end());
}

