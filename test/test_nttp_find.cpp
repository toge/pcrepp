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

  // 構造化束縛を用いたループ（ユーザーの例）
  int count = 0;
  for (auto const& [whole, key, value] : all) {
    if (count == 0) {
      CHECK(whole == "age:30");
      CHECK(key == "age");
      CHECK(value == "30");
    } else {
      CHECK(whole == "height:180");
      CHECK(key == "height");
      CHECK(value == "180");
    }
    count++;
  }
  CHECK(count == 2);

  auto it = all.begin();
  auto const& r1 = *it;
  CHECK(std::get<1>(r1) == "age");
  CHECK(std::get<2>(r1) == "30");
}

TEST_CASE("NTTP find throws on invalid pattern", "[nttp_find]") {
  CHECK_THROWS_AS((void)pcrepp::find<"(">("x"), std::runtime_error);
}

TEST_CASE("NTTP find accepts string literal template argument", "[nttp_find]") {
  static_assert(requires { pcrepp::find<"a+">("aaaa"); });
  auto const res = pcrepp::find<"a+">("aaaa");
  CHECK(res);
  CHECK(res.get<1>() == "aaaa");
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

  SECTION("_re literal") {
    auto const all = pcrepp::find_all<R"re(<li class=\"item-card[\\s\\S]*?data-product-id=\"([0-9]+)\"[\\s\\S]*?data-product-price=\"([0-9]+)\"[\\s\\S]*?<div class=\"item-card__title\"><a[\\s\\S]*?href=\"(https://booth\\.pm/ja/items/[0-9]+)\">([\\s\\S]*?)</a>[\\s\\S]*?<div class=\"item-card__shop-name\">([\\s\\S]*?)</div>\000")re">("aaaa");
    for (auto const& [w, g1, g2, g3, g4, g5] : all) {
      CHECK(w.empty());
      CHECK(g1.empty());
      CHECK(g2.empty());
      CHECK(g3.empty());
      CHECK(g4.empty());
      CHECK(g5.empty());
    }
  }
}
