#include "pcrepp.hpp"
#include <catch2/catch_test_macros.hpp>
#include <pcre2.h>
#include <string_view>
#include <string>

using namespace std::string_view_literals;

// ============================================================
// Runtime context: ASCII patterns matching in Japanese text
// ============================================================

TEST_CASE("Runtime context matches ASCII digits in Japanese text", "[japanese]") {
  pcrepp::context ctx{R"((\d+)円)"};
  pcrepp::match_result mr{ctx};

  auto const target = "価格: 100円"sv;
  auto rc = ctx.find(target, mr);
  REQUIRE(rc);
  CHECK(mr[0] == "100円");
  CHECK(mr[1] == "100");
}

TEST_CASE("Runtime context matches literal Japanese substring", "[japanese]") {
  pcrepp::context ctx{R"re(日本)re"};
  pcrepp::match_result mr{ctx};

  auto const target = "日本語のテスト"sv;
  auto rc = ctx.find(target, mr);
  REQUIRE(rc);
  CHECK(mr[0] == "日本");
}

TEST_CASE("Runtime context find_all with Japanese text", "[japanese]") {
  pcrepp::context ctx{R"((\d+)個)"};

  auto const target = "りんご: 3個, みかん: 5個"sv;
  int count = 0;
  for (auto const& m : ctx.find_all(target)) {
    if (count == 0) CHECK(m[1] == "3");
    else            CHECK(m[1] == "5");
    ++count;
  }
  CHECK(count == 2);
}

TEST_CASE("Runtime context replace with Japanese text", "[japanese]") {
  pcrepp::context ctx{R"((\d+)円)"};

  auto const target = "100円の商品"sv;
  auto res = ctx.replace(target, "$1 USD");
  REQUIRE(res.has_value());
  CHECK(*res == "100 USDの商品");
}

TEST_CASE("Runtime context split with Japanese delimiter", "[japanese]") {
  pcrepp::context ctx{R"re(、)re"};

  auto const target = "東京、大阪、名古屋"sv;
  auto parts = ctx.split(target);
  REQUIRE(parts.size() == 3);
  CHECK(parts[0] == "東京");
  CHECK(parts[1] == "大阪");
  CHECK(parts[2] == "名古屋");
}

TEST_CASE("Runtime context named capture in Japanese text", "[japanese]") {
  pcrepp::context ctx{R"re((?<city>[\w\W]+)の(?<item>[\w\W]+))re"};
  pcrepp::match_result mr{ctx};

  auto const target = "東京のラーメン"sv;
  auto rc = ctx.find(target, mr);
  REQUIRE(rc);
  CHECK(mr.get("city") == "東京");
  CHECK(mr.get("item") == "ラーメン");
}

TEST_CASE("Runtime context match with PCRE2_CASELESS on ASCII in Japanese text", "[japanese]") {
  pcrepp::context ctx{"(hello)", PCRE2_CASELESS};
  pcrepp::match_result mr{ctx};

  auto const target = "日本語HELLO世界"sv;
  auto rc = ctx.find(target, mr);
  REQUIRE(rc);
  CHECK(mr[1] == "HELLO");
}

// ============================================================
// PCRE2_UTF / PCRE2_UCP explicit tests (runtime context)
// ============================================================

TEST_CASE("Runtime context with PCRE2_UTF matches \\w in Japanese", "[japanese][utf]") {
  pcrepp::context ctx{R"re((\w+)の(\w+))re", PCRE2_UTF | PCRE2_UCP};
  pcrepp::match_result mr{ctx};

  auto const target = "東京のラーメン"sv;
  auto rc = ctx.find(target, mr);
  REQUIRE(rc);
  CHECK(mr[1] == "東京");
  CHECK(mr[2] == "ラーメン");
}

TEST_CASE("Runtime context with PCRE2_UTF find_all in Japanese", "[japanese][utf]") {
  pcrepp::context ctx{R"re((\w+))re", PCRE2_UTF | PCRE2_UCP};

  auto const target = "東京 大阪 名古屋"sv;
  int count = 0;
  for (auto const& m : ctx.find_all(target)) {
    if (count == 0)       CHECK(m[1] == "東京");
    else if (count == 1)  CHECK(m[1] == "大阪");
    else                  CHECK(m[1] == "名古屋");
    ++count;
  }
  CHECK(count == 3);
}

TEST_CASE("Runtime context with PCRE2_UTF handles find_all across newlines", "[japanese][utf]") {
  pcrepp::context ctx{R"re(([\s\S]+?)[。\n])re", PCRE2_UTF};

  auto const target = "今日はいい天気です。\n明日も晴れるでしょう。"sv;
  int count = 0;
  for (auto const& m : ctx.find_all(target)) {
    if (count == 0) CHECK(m[1] == "今日はいい天気です");
    else            CHECK(m[1] == "\n明日も晴れるでしょう");
    ++count;
  }
  CHECK(count == 2);
}

// ============================================================
// NTTP: ASCII patterns + PCRE2 match-time options on Japanese text
// ============================================================

TEST_CASE("NTTP find extracts ASCII digits from Japanese text", "[japanese][nttp_find]") {
  auto res = pcrepp::find<R"((\d+)円)">("価格: 500円");
  REQUIRE(res);
  auto [matched, whole, price] = *res;
  CHECK(matched);
  CHECK(whole == "500円");
  CHECK(price == "500");
}

TEST_CASE("NTTP find matches literal Japanese substring", "[japanese][nttp_find]") {
  auto res = pcrepp::find<R"re(日本)re">("日本語");
  REQUIRE(res);
  auto [matched, whole] = *res;
  CHECK(matched);
  CHECK(whole == "日本");
}

TEST_CASE("NTTP find_all extracts ASCII digits from Japanese text", "[japanese][nttp_find]") {
  auto all_res = pcrepp::find_all<R"((\d+)個)">("りんご 3個, みかん 5個");
  REQUIRE(all_res.has_value());
  auto const& all = *all_res;
  CHECK(std::ranges::distance(all) == 2);

  int count = 0;
  for (auto const& mr : all) {
    if (count == 0) {
      CHECK(mr.get(0) == "3個");
      CHECK(mr.get(1) == "3");
    } else {
      CHECK(mr.get(0) == "5個");
      CHECK(mr.get(1) == "5");
    }
    ++count;
  }
  CHECK(count == 2);
}

TEST_CASE("NTTP compile API matches Japanese text", "[japanese][nttp_find]") {
  static constexpr auto re = pcrepp::compile<R"re(([\w\W]+)の([\w\W]+))re">();

  auto res = re.find("京都の紅葉");
  REQUIRE(res);
  auto [matched, whole, item, desc] = *res;
  CHECK(matched);
  CHECK(item == "京都");
  CHECK(desc == "紅葉");
}

TEST_CASE("NTTP named capture with Japanese text", "[japanese][nttp_find]") {
  auto const res = pcrepp::find<R"re((?<pref>[\w\W]+)県(?<city>[\w\W]+))re">("京都県京都");
  REQUIRE(res);
  CHECK(static_cast<bool>(*res));
  CHECK(res->get<"pref">() == "京都");
  CHECK(res->get<"city">() == "京都");
}

// ============================================================
// Mixed ASCII/Japanese patterns
// ============================================================

TEST_CASE("Mixed ASCII/Japanese email-like extraction", "[japanese]") {
  pcrepp::context ctx{R"re(([\w.@]+)からメール)re"};
  pcrepp::match_result mr{ctx};

  auto const target = "user@example.jpからメールが届きました"sv;
  auto rc = ctx.find(target, mr);
  REQUIRE(rc);
  CHECK(mr[1] == "user@example.jp");
}

TEST_CASE("Dynamic replace callback with Japanese text", "[japanese]") {
  pcrepp::context ctx{R"((\d+)円)"};

  auto const target = "価格は500円です"sv;
  auto const result = ctx.replace(target, [](auto const& m) {
    auto const price = m.template get<int>(1);
    return std::to_string(price * 110 / 100) + "円(税込)";
  });
  REQUIRE(result.has_value());
  CHECK(*result == "価格は550円(税込)です");
}

TEST_CASE("NTTP compile API with ASCII pattern on Japanese text", "[japanese][nttp_find]") {
  static constexpr auto re = pcrepp::compile<R"((\d+)円)">();
  auto res = re.find("価格: 300円");
  REQUIRE(res);
  auto [matched, whole, price] = *res;
  CHECK(matched);
  CHECK(whole == "300円");
  CHECK(price == "300");
}

TEST_CASE("Unicode property classes with PCRE2_UCP", "[japanese][unicode][h11]") {
  auto const letter_ctx = pcrepp::context<>::create(R"(\p{L}+)", PCRE2_UTF | PCRE2_UCP).value();
  auto const letter_res = letter_ctx.find("123日本abc");
  REQUIRE(letter_res);
  CHECK(!letter_res->get(0uz).empty());

  auto const han_ctx = pcrepp::context<>::create(R"(\p{Han}+)", PCRE2_UTF | PCRE2_UCP).value();
  auto han_count = 0uz;
  for ([[maybe_unused]] auto const& mr : han_ctx.find_all("Hello 日本語 World 漢字")) {
    ++han_count;
  }
  CHECK(han_count >= 1uz);
}
