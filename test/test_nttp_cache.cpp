#include <catch2/catch_test_macros.hpp>
#include "pcrepp.hpp"
#include <iostream>
#include <vector>

TEST_CASE("NTTP find_all caching and compile API", "[nttp]") {
    using namespace pcrepp;
    std::cout << "Starting NTTP find_all caching test" << std::endl;

    SECTION("Direct find_all call") {
        auto results = find_all<"abc">("abc abc abc");
        int count = 0;
        for (auto [matched, g0] : results) {
            CHECK(matched);
            CHECK(g0 == "abc");
            count++;
        }
        CHECK(count == 3);
    }

    SECTION("Using compile and nttp_regex object") {
        static constexpr auto re = compile<"(\\w+)">();
        auto target = std::string_view{"hello world"};
        auto results = re.find_all(target);

        REQUIRE(std::ranges::distance(results) == 2);

        auto [m1, g1_0, g1_1] = *results.begin();
        CHECK(m1);
        CHECK(g1_0 == "hello");
        CHECK(g1_1 == "hello");
    }

    SECTION("Using literal operator _re") {
        auto re = "(\\d+)"_re;
        auto results = re.find_all("123 456");
        REQUIRE(std::ranges::distance(results) == 2);
        auto [m, g0, g1] = *results.begin();
        CHECK(m);
        CHECK(g0 == "123");
        CHECK(g1 == "123");
    }
}
