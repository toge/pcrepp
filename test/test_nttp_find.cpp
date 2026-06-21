#include "pcrepp.hpp"
#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include <string_view>

TEST_CASE("NTTP find supports structured binding via find_unchecked", "[nttp_find]") {
  // C7: find_unchecked は throw 版
  auto [matched, whole, key, value] = pcrepp::find_unchecked<R"((\w+):(\d+))">("age:30");
  CHECK(matched);
  CHECK(whole == "age:30");
  CHECK(key == "age");
  CHECK(value == "30");
}

TEST_CASE("NTTP find returns expected", "[nttp_find]") {
  // C7: find は expected を返す
  auto const res = pcrepp::find<R"((\w+):(\d+))">("age:30");
  REQUIRE(res);
  CHECK(pcrepp::get<0>(*res));
  CHECK(pcrepp::get<1>(*res) == "age:30");
  CHECK(pcrepp::get<2>(*res) == "age");
  CHECK(pcrepp::get<3>(*res) == "30");
}

TEST_CASE("NTTP find supports get by named capture", "[nttp_find]") {
  auto const res = pcrepp::find<R"((?<key>\w+):(?<value>\d+))">("age:30");
  REQUIRE(res);
  CHECK(res->get<"key">() == "age");
  CHECK(res->get<"value">() == "30");
  CHECK(res->get<"missing">().empty());
}

TEST_CASE("NTTP find returns false result when no match", "[nttp_find]") {
  auto const res = pcrepp::find<R"((\w+):(\d+))">("nomatch");
  REQUIRE(res);  // expected にエラーなし (マッチなしは !matched)
  CHECK_FALSE(static_cast<bool>(*res));
  CHECK_FALSE(pcrepp::get<0>(*res));
  CHECK(pcrepp::get<1>(*res).empty());
  CHECK(pcrepp::get<2>(*res).empty());
  CHECK(pcrepp::get<3>(*res).empty());
}

TEST_CASE("NTTP find_all returns result vector", "[nttp_find]") {
  auto const all = pcrepp::find_all<R"((?<key>\w+):(?<value>\d+))">("age:30 height:180");
  REQUIRE(std::ranges::distance(all) == 2);

  // 構造化束縛を用いたループ
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

TEST_CASE("NTTP find returns unexpected on invalid pattern", "[nttp_find]") {
  // C7: 無効パターンは unexpected を返す
  auto const res = pcrepp::find<"(">("x");
  CHECK_FALSE(res.has_value());
}

TEST_CASE("NTTP find_unchecked throws on invalid pattern", "[nttp_find]") {
  // C7: find_unchecked は throw 版
  CHECK_THROWS_AS((void)pcrepp::find_unchecked<"(">("x"), std::runtime_error);
}

TEST_CASE("NTTP find accepts string literal template argument", "[nttp_find]") {
  static_assert(requires { pcrepp::find<"a+">("aaaa"); });
  auto const res = pcrepp::find<"a+">("aaaa");
  REQUIRE(res);
  CHECK(res->get<1>() == "aaaa");
}

TEST_CASE("NTTP compile and _re literal", "[nttp_find]") {
  using namespace pcrepp;

  SECTION("compile API") {
    static constexpr auto re = compile<R"((\w+):(\d+))">();
    // nttp_regex::find は expected を返す
    auto res = re.find("age:30");
    REQUIRE(res);
    auto [m, whole, key, value] = *res;
    CHECK(m);
    CHECK(key == "age");
    CHECK(value == "30");

    auto all = re.find_all("age:30 height:180");
    CHECK(std::ranges::distance(all) == 2);
  }

  SECTION("_re literal") {
    auto re = R"((\w+):(\d+))"_re;
    auto res = re.find("age:30");
    REQUIRE(res);
    auto [m, whole, key, value] = *res;
    CHECK(m);
    CHECK(key == "age");
    CHECK(value == "30");
  }

  SECTION("_re literal find_all with complex pattern") {
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

TEST_CASE("nttp_regex replace with string replacement", "[nttp][f14]") {
  auto const re = pcrepp::compile<R"(\d+)">();
  auto const res = re.replace("abc 42 def", "X");
  REQUIRE(res);
  CHECK(*res == "abc X def");
}

TEST_CASE("nttp_regex replace with callback", "[nttp][f14]") {
  auto const re = pcrepp::compile<R"((\w+))">();
  auto const res = re.replace("hello world", [](auto const& m) -> std::string {
    return "[" + std::string{m.get(1)} + "]";
  });
  REQUIRE(res);
  CHECK(*res == "[hello] [world]");
}

TEST_CASE("nttp_regex split", "[nttp][f14]") {
  auto const re = pcrepp::compile<R"(,\s*)">();
  auto const parts = re.split("a, b, c");
  REQUIRE(parts.size() == 3uz);
  CHECK(parts[0] == "a");
  CHECK(parts[1] == "b");
  CHECK(parts[2] == "c");
}

TEST_CASE("nttp_match_result formatter", "[nttp][f8]") {
  auto const res = pcrepp::find_unchecked<R"((\w+):(\d+))">("age:30");
  auto const s = std::format("{}", res);
  CHECK(s == "[matched, age:30, age, 30]");
}
