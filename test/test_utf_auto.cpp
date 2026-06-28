#include "catch2/catch_all.hpp"
#include "pcrepp.hpp"

TEST_CASE("auto_utf_options detects non-ASCII patterns", "[utf][e9]") {
  // ASCII only → フラグなし
  CHECK(pcrepp::detail::auto_utf_options("hello") == 0u);
  CHECK(pcrepp::detail::auto_utf_options(R"(\d+)") == 0u);
  // 非 ASCII → PCRE2_UTF
  CHECK(pcrepp::detail::auto_utf_options(R"(東京)") == PCRE2_UTF);
  CHECK(pcrepp::detail::auto_utf_options(R"((\d+)円)") == PCRE2_UTF);
  // 空パターン → フラグなし
  CHECK(pcrepp::detail::auto_utf_options("") == 0u);
}

TEST_CASE("context_create_utf auto-encodes UTF for non-ASCII patterns", "[utf][e9]") {
  using namespace std::string_view_literals;
  // ASCII パターン → UTF 不要 (明示指定があれば OR)
  auto const ascii_res = pcrepp::context_create_utf(R"((\w+):(\d+))");
  REQUIRE(ascii_res);
  CHECK((ascii_res->options() & PCRE2_UTF) == 0u);

  // 非 ASCII パターン → PCRE2_UTF 自動付与
  auto const jp_res = pcrepp::context_create_utf(R"((\d+)円)");
  REQUIRE(jp_res);
  CHECK((jp_res->options() & PCRE2_UTF) != 0u);

  // extra_option で CASELESS を追加しても UTF 自動付与は維持
  auto const jp_caseless = pcrepp::context_create_utf(R"(東京)", PCRE2_CASELESS);
  REQUIRE(jp_caseless);
  CHECK((jp_caseless->options() & PCRE2_UTF) != 0u);
  CHECK((jp_caseless->options() & PCRE2_CASELESS) != 0u);
}

TEST_CASE("compile_utf returns context with PCRE2_UTF for non-ASCII patterns", "[utf][e9]") {
  // ASCII パターン → UTF 不要
  auto const& ascii_ctx = pcrepp::compile_utf<R"((\w+):(\d+))">();
  CHECK((ascii_ctx.options() & PCRE2_UTF) == 0u);

  // 非 ASCII パターン → PCRE2_UTF 自動付与
  auto const& jp_ctx = pcrepp::compile_utf<R"((\d+)円)">();
  CHECK((jp_ctx.options() & PCRE2_UTF) != 0u);

  // 動作確認: 日本語ターゲットで (\d+)円 がマッチ
  using namespace std::string_view_literals;
  auto mr = pcrepp::match_result{jp_ctx};
  auto const rc = jp_ctx.find("価格は500円です"sv, mr);
  REQUIRE(rc.has_value());
  REQUIRE(*rc > 0);
  CHECK(mr.get(0uz) == "500円");
  CHECK(mr.get(1uz) == "500");
}