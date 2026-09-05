#include <catch2/catch_test_macros.hpp>
#include "pcrepp.hpp"
#include <iostream>
#include <vector>

TEST_CASE("NTTP find_all caching and compile API", "[nttp]") {
    using namespace pcrepp;
    std::cout << "Starting NTTP find_all caching test" << std::endl;

    SECTION("Direct find_all call") {
        auto results_res = find_all<"abc">("abc abc abc");
        REQUIRE(results_res.has_value());
        int count = 0;
        for (auto const& mr : *results_res) {
            CHECK(mr.get(0) == "abc");
            count++;
        }
        CHECK(count == 3);
    }

    SECTION("Using compile and nttp_regex object") {
        static constexpr auto re = compile<"(\\w+)">();
        auto target = std::string_view{"hello world"};
        auto results_res = re.find_all(target);

        REQUIRE(results_res.has_value());
        REQUIRE(std::ranges::distance(*results_res) == 2);

        auto it = results_res->begin();
        auto r1 = *it;
        CHECK(r1.get(0) == "hello");
        CHECK(r1.get(1) == "hello");
    }

    SECTION("Using literal operator _re") {
        auto re = "(\\d+)"_re;
        auto results_res = re.find_all("123 456");
        REQUIRE(results_res.has_value());
        REQUIRE(std::ranges::distance(*results_res) == 2);
        auto it = results_res->begin();
        auto r1 = *it;
        CHECK(r1.get(0) == "123");
        CHECK(r1.get(1) == "123");
    }
}
