#include "pcrepp.hpp"
#include <catch2/catch_test_macros.hpp>
#include <string_view>
#include <tuple>
#include <type_traits>

TEST_CASE("NTTP find returns tuple<bool, string_view...>", "[nttp_find]") {
  auto const res = pcrepp::find<R"((\w+):(\d+))">("age:30");
  REQUIRE(res.has_value());

  using expected_t = std::tuple<bool, std::string_view, std::string_view, std::string_view>;
  static_assert(std::same_as<decltype(*res), expected_t const&>);

  auto const& tup = *res;
  CHECK(std::get<0>(tup));
  CHECK(std::get<1>(tup) == "age:30");
  CHECK(std::get<2>(tup) == "age");
  CHECK(std::get<3>(tup) == "30");
}

TEST_CASE("NTTP find returns false tuple when no match", "[nttp_find]") {
  auto const res = pcrepp::find<R"((\w+):(\d+))">("nomatch");
  REQUIRE(res.has_value());
  auto const& tup = *res;
  CHECK_FALSE(std::get<0>(tup));
  CHECK(std::get<1>(tup).empty());
  CHECK(std::get<2>(tup).empty());
  CHECK(std::get<3>(tup).empty());
}

TEST_CASE("NTTP find_all returns tuple vector", "[nttp_find]") {
  auto const all = pcrepp::find_all<R"((\w+):(\d+))">("age:30 height:180");
  REQUIRE(all.has_value());
  REQUIRE(all->size() == 2);

  CHECK(std::get<0>((*all)[0]));
  CHECK(std::get<1>((*all)[0]) == "age:30");
  CHECK(std::get<2>((*all)[0]) == "age");
  CHECK(std::get<3>((*all)[0]) == "30");

  CHECK(std::get<0>((*all)[1]));
  CHECK(std::get<1>((*all)[1]) == "height:180");
  CHECK(std::get<2>((*all)[1]) == "height");
  CHECK(std::get<3>((*all)[1]) == "180");
}
