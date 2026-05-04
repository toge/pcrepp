#include "catch2/catch_all.hpp"
#include "pcrepp.hpp"
#include <format>

TEST_CASE("Basic usage from user example", "[basic]") {
    using namespace std::string_view_literals;

    SECTION("Compile and Match") {
        auto ctx_res = pcrepp::context<>::create(R"((?<name>\w+):\s*(?<value>\d+))");
        REQUIRE(ctx_res.has_value());
        auto const& ctx = *ctx_res;

        auto const target = "Apple: 100, Banana: 200"sv;
        auto results = ctx.search_all(target);
        auto it = results.begin();

        REQUIRE(it != results.end());
        auto const& res1 = *it;
        CHECK(res1[0] == "Apple: 100");
        CHECK(res1["name"] == "Apple");
        CHECK(res1["value"] == "100");

        ++it;
        REQUIRE(it != results.end());
        auto const& res2 = *it;
        CHECK(res2[0] == "Banana: 200");
        CHECK(res2["name"] == "Banana");
        CHECK(res2["value"] == "200");

        ++it;
        CHECK(it == results.end());
    }

    SECTION("Dynamic Replacement") {
        auto ctx_res = pcrepp::context<>::create(R"((?<name>\w+):\s*(?<value>\d+))");
        REQUIRE(ctx_res.has_value());
        auto const& ctx = *ctx_res;

        auto const target = "Apple: 100, Banana: 200"sv;
        auto dynamic_res = ctx.replace(target, [](auto const& res) {
            auto value = std::stoi(std::string(res["value"]));
            return std::format("{}({} USD)", res["name"], value / 100);
        });

        CHECK(dynamic_res == "Apple(1 USD), Banana(2 USD)");
    }

    SECTION("Split") {
        auto ctx_res = pcrepp::context<>::create(R"(,\s*)");
        REQUIRE(ctx_res.has_value());
        auto const& ctx = *ctx_res;

        auto const target = "apple, banana, cherry"sv;
        auto parts = ctx.split(target);
        
        REQUIRE(parts.size() == 3);
        CHECK(parts[0] == "apple");
        CHECK(parts[1] == "banana");
        CHECK(parts[2] == "cherry");
    }

    SECTION("Formatter") {
        auto ctx_res = pcrepp::context<>::create(R"((\w+))");
        REQUIRE(ctx_res.has_value());
        auto const& ctx = *ctx_res;

        pcrepp::match_result mr(ctx.code);
        auto res = ctx.search("hello", mr);
        REQUIRE(res.has_value());
        REQUIRE(*res > 0);

        auto formatted = std::format("{}", mr);
        CHECK(formatted == "[hello, hello]"); // group 0 is whole match, group 1 is (\w+)
    }
}
