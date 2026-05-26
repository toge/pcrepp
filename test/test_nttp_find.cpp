#include "pcrepp.hpp"
#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include <string_view>

TEST_CASE("NTTP find supports structured binding", "[nttp_find]") {
  auto [matched, whole, key, value] = pcrepp::find<R"((\w+):(\d+))">("age:30");
  CHECK(matched);
  CHECK(whole == "age:30");
  CHECK(key == "age");
  CHECK(value == "30");
}

TEST_CASE("NTTP find supports get by index", "[nttp_find]") {
  auto const res = pcrepp::find<R"((\w+):(\d+))">("age:30");
  CHECK(pcrepp::get<0>(res));
  CHECK(pcrepp::get<1>(res) == "age:30");
  CHECK(pcrepp::get<2>(res) == "age");
  CHECK(pcrepp::get<3>(res) == "30");
}

TEST_CASE("NTTP find supports get by named capture", "[nttp_find]") {
  auto const res = pcrepp::find<R"((?<key>\w+):(?<value>\d+))">("age:30");
  CHECK(res.get<"key">() == "age");
  CHECK(res.get<"value">() == "30");
  CHECK(res.get<"missing">().empty());
}

TEST_CASE("NTTP find returns false result when no match", "[nttp_find]") {
  auto const res = pcrepp::find<R"((\w+):(\d+))">("nomatch");
  CHECK_FALSE(static_cast<bool>(res));
  CHECK_FALSE(pcrepp::get<0>(res));
  CHECK(pcrepp::get<1>(res).empty());
  CHECK(pcrepp::get<2>(res).empty());
  CHECK(pcrepp::get<3>(res).empty());
}

TEST_CASE("NTTP find_all returns result vector", "[nttp_find]") {
  auto const all = pcrepp::find_all<R"((?<key>\w+):(?<value>\d+))">("age:30 height:180");
  REQUIRE(std::ranges::distance(all) == 2);

  auto it = all.begin();
  auto const& r1 = *it;
  CHECK(r1);
  CHECK(r1.get<"key">() == "age");
  CHECK(r1.get<"value">() == "30");

  ++it;
  auto const [m2, whole2, key2, value2] = *it;
  CHECK(m2);
  CHECK(whole2 == "height:180");
  CHECK(key2 == "height");
  CHECK(value2 == "180");
}

TEST_CASE("NTTP find throws on invalid pattern", "[nttp_find]") {
  CHECK_THROWS_AS((void)pcrepp::find<"(">("x"), std::runtime_error);
}

TEST_CASE("NTTP compile and _re literal", "[nttp_find]") {
  using namespace pcrepp;

  SECTION("compile API") {
    static constexpr auto re = compile<R"((\w+):(\d+))">();
    auto [m, whole, key, value] = re.find("age:30");
    CHECK(m);
    CHECK(key == "age");
    CHECK(value == "30");

    auto all = re.find_all("age:30 height:180");
    CHECK(std::ranges::distance(all) == 2);
  }

  SECTION("_re literal") {
    auto re = R"((\w+):(\d+))"_re;
    auto [m, whole, key, value] = re.find("age:30");
    CHECK(m);
    CHECK(key == "age");
    CHECK(value == "30");
  }
}
