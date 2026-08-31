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

TEST_CASE("NTTP API with UseJIT=false", "[nttp_find][h6]") {
  auto const res = pcrepp::find<R"((\d+))", false>("abc 42");
  REQUIRE(res);
  CHECK(res->get<1>() == "42");

  auto count = 0uz;
  for ([[maybe_unused]] auto const& t : pcrepp::find_all<R"(\d+)", false>("1 2 3")) {
    ++count;
  }
  CHECK(count == 3uz);

  auto const re = pcrepp::compile<R"((\d+))", false>();
  auto const match_res = re.match("123");
  REQUIRE(match_res);
  CHECK(*match_res);
}

TEST_CASE("NTTP find with start offset", "[nttp_find][h19]") {
  auto const res = pcrepp::find<R"((\d+))">("abc 123 456", 8uz);
  REQUIRE(res);
  CHECK(res->get<1>() == "456");
}

TEST_CASE("NTTP free replace with string replacement", "[nttp_replace]") {
  auto const res = pcrepp::replace<R"((?<key>\w+):(?<value>\d+))">("age:30 height:180", "$1=$2");
  REQUIRE(res);
  CHECK(*res == "age=30 height=180");
}

TEST_CASE("NTTP free replace non-global", "[nttp_replace]") {
  // PCRE2_SUBSTITUTE_GLOBAL を外すと最初の 1 箇所のみ置換
  auto const res = pcrepp::replace<R"(\d+)">("a1b2c3", "#", 0);
  REQUIRE(res);
  CHECK(*res == "a#b2c3");
}

TEST_CASE("NTTP free replace with callback", "[nttp_replace]") {
  auto const res = pcrepp::replace<R"((\w+):(\d+))">("age:30", [](auto const& m) -> std::string {
    return "[" + std::string{m.get(1)} + "=" + std::string{m.get(2)} + "]";
  });
  REQUIRE(res);
  CHECK(*res == "[age=30]");
}

TEST_CASE("NTTP replace_unchecked throws on invalid pattern", "[nttp_replace]") {
  CHECK_THROWS_AS(pcrepp::replace_unchecked<R"([)">("abc", "x"), std::runtime_error);

  auto const s = pcrepp::replace_unchecked<R"(\d+)">("v1 v22", "?");
  CHECK(s == "v? v?");
}

TEST_CASE("NTTP free replace returns unexpected on invalid pattern", "[nttp_replace]") {
  auto const res = pcrepp::replace<R"([)">("abc", "x");
  CHECK_FALSE(res.has_value());
}

TEST_CASE("NTTP two-template-arg replace basic capture substitution", "[nttp_replace][fast]") {
  auto const res = pcrepp::replace<R"((\w+):(\d+))", "$1=$2">("age:30 height:180");
  REQUIRE(res);
  CHECK(*res == "age=30 height=180");
}

TEST_CASE("NTTP fast replace $$ and braced forms", "[nttp_replace][fast]") {
  auto const res = pcrepp::replace<R"((a)(b))", "$$${1}_${2}">("ab ab");
  REQUIRE(res);
  CHECK(*res == "$a_b $a_b");
}

TEST_CASE("NTTP fast replace with named captures", "[nttp_replace][fast]") {
  auto const res = pcrepp::replace<R"((?<key>\w+)=(?<value>\d+))", "${value}:${key}">("age=30");
  REQUIRE(res);
  CHECK(*res == "30:age");

  // 波括弧なしの名前参照
  auto const res2 = pcrepp::replace<R"((?<key>\w+)=(?<value>\d+))", "$key">("age=30");
  REQUIRE(res2);
  // ランタイム経路 (pcre2_substitute) と同一結果になることを確認
  auto const legacy2 = pcrepp::replace<R"((?<key>\w+)=(?<value>\d+))">("age=30", "$key");
  REQUIRE(legacy2);
  CHECK(*res2 == "age");
  CHECK(*res2 == *legacy2);
}

TEST_CASE("NTTP fast replace adjacent references", "[nttp_replace][fast]") {
  auto const res = pcrepp::replace<R"((\d)(\d))", "$1x$2y">("1234");
  REQUIRE(res);
  CHECK(*res == "1x2y3x4y");
}

TEST_CASE("NTTP fast replace zero-width matches match legacy path", "[nttp_replace][fast]") {
  auto const target = std::string{"abc"};
  auto const fast   = pcrepp::replace<R"(x*)", "-">(target);
  auto const legacy = pcrepp::replace<R"(x*)">(target, "-");
  REQUIRE(fast);
  REQUIRE(legacy);
  CHECK(*fast == *legacy);
  CHECK(*fast == "-a-b-c-");
}

TEST_CASE("NTTP fast replace unset group is an error like PCRE2 default", "[nttp_replace][fast]") {
  auto const res = pcrepp::replace<R"((x)?y)", "<$1>">("y");
  CHECK_FALSE(res.has_value());
}

TEST_CASE("NTTP fast replace falls back to runtime path on out-of-range ref", "[nttp_replace][fast]") {
  // 存在しないグループ参照 → フォールバックして pcre2_substitute のエラーを再現
  auto const res = pcrepp::replace<R"((a)(b)(c)(d)(e)(f)(g)(h)(i)(j)(k)(l))", "$123">("abcdefghijklm");
  CHECK_FALSE(res.has_value());
}

TEST_CASE("NTTP fast replace falls back on invalid replacement syntax", "[nttp_replace][fast]") {
  auto const res = pcrepp::replace<R"(a)", "X$">("abc");
  CHECK_FALSE(res.has_value());
}

TEST_CASE("NTTP fast replace UseJIT=false", "[nttp_replace][fast]") {
  auto const res = pcrepp::replace<R"((\d+))", "[\\1]", false>("a1b22");
  REQUIRE(res);
  // \1 は EXTENDED 非対応のためリテラル扱い (フォールバックしない、そのまま出力)
  CHECK(*res == "a[\\1]b[\\1]");
}

TEST_CASE("NTTP replace_unchecked two-template-arg version", "[nttp_replace][fast]") {
  auto const s = pcrepp::replace_unchecked<R"(\d+)", "?">("v1 v22");
  CHECK(s == "v? v?");

  // テンプレート引数のカンマは Catch2 マクロの引数分割と干渉するため
  // マクロの外で実行して例外を確認する
  auto threw = false;
  try {
    static_cast<void>(pcrepp::replace_unchecked<"[)", "?">("abc"));
  } catch (std::runtime_error const&) {
    threw = true;
  }
  CHECK(threw);
}

TEST_CASE("NTTP free match full-string check", "[nttp_match]") {
  CHECK(pcrepp::match<R"(\d+)">("12345").value_or(false));
  CHECK_FALSE(pcrepp::match<R"(\d+)">("123abc").value_or(false));
  // 無効パターンは unexpected
  CHECK_FALSE(pcrepp::match<"[)">("x").has_value());
}

TEST_CASE("NTTP free split", "[nttp_split]") {
  auto const parts = pcrepp::split<R"(,\s*)">("a, b, c");
  REQUIRE(parts.size() == 3uz);
  CHECK(parts[0] == "a");
  CHECK(parts[1] == "b");
  CHECK(parts[2] == "c");
}

TEST_CASE("NTTP split_view lazy iteration", "[nttp_split]") {
  auto        count = 0uz;
  std::string_view last;
  for (auto part : pcrepp::split_view<R"(\s+)">("x y z")) {
    last = part;
    ++count;
  }
  CHECK(count == 3uz);
  CHECK(last == "z");

  auto const re = pcrepp::compile<R"(-)">();
  count         = 0uz;
  for ([[maybe_unused]] auto part : re.split_view("1-2-3")) { ++count; }
  CHECK(count == 3uz);
}

TEST_CASE("NTTP find_all with start offset", "[nttp_find][h19]") {
  auto const all = pcrepp::find_all<R"(\d+)">("1 2 3 4", 0, 4uz);
  auto       n   = 0uz;
  for (auto const& [whole] : all) {
    CHECK((whole == "3" || whole == "4"));
    ++n;
  }
  CHECK(n == 2uz);

  auto const re  = pcrepp::compile<R"([a-z]+)">();
  auto const all2 = re.find_all("aa bb cc", 0, 3uz);
  CHECK(std::ranges::distance(all2) == 2uz);
}
