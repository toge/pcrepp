#include "pcrepp.hpp"
#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <type_traits>

TEST_CASE("NTTP find returns tuple directly", "[nttp_find]") {
  using expected_t = std::tuple<bool, std::string_view, std::string_view, std::string_view>;
  static_assert(std::same_as<decltype(pcrepp::find<R"((\\w+):(\\d+))">("age:30")), expected_t>);

  auto [matched, whole, key, value] = pcrepp::find<R"((\w+):(\d+))">("age:30");
  CHECK(matched);
  CHECK(whole == "age:30");
  CHECK(key == "age");
  CHECK(value == "30");
}

TEST_CASE("NTTP find returns false tuple when no match", "[nttp_find]") {
  auto [matched, whole, key, value] = pcrepp::find<R"((\w+):(\d+))">("nomatch");
  CHECK_FALSE(matched);
  CHECK(whole.empty());
  CHECK(key.empty());
  CHECK(value.empty());
}

TEST_CASE("NTTP find_all returns tuple vector directly", "[nttp_find]") {
  using expected_vec_t = std::vector<std::tuple<bool, std::string_view, std::string_view, std::string_view>>;
  static_assert(std::same_as<decltype(pcrepp::find_all<R"((\\w+):(\\d+))">("age:30")), expected_vec_t>);

  auto const all = pcrepp::find_all<R"((\w+):(\d+))">("age:30 height:180");
  REQUIRE(all.size() == 2);

  auto const [m1, w1, k1, v1] = all[0];
  CHECK(m1);
  CHECK(w1 == "age:30");
  CHECK(k1 == "age");
  CHECK(v1 == "30");

  auto const [m2, w2, k2, v2] = all[1];
  CHECK(m2);
  CHECK(w2 == "height:180");
  CHECK(k2 == "height");
  CHECK(v2 == "180");
}

TEST_CASE("NTTP find throws on invalid pattern", "[nttp_find]") {
  CHECK_THROWS_AS((void)pcrepp::find<"(">("x"), std::runtime_error);
}
