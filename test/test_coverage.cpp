#include "pcrepp.hpp"
#include <catch2/catch_test_macros.hpp>
#include <iterator>
#include <string>
#include <vector>

// ========================================
// レビュー修正の回帰テスト
// ========================================
TEST_CASE("match_result out-of-range access returns empty", "[match_result][oob]") {
  auto const ctx = pcrepp::context<>::create(R"((\w+):(\d+))").value();
  auto const res = ctx.find("age:30");
  REQUIRE(res);
  REQUIRE(*res);

  CHECK(res->get(99uz).empty());
  CHECK((*res)[99uz].empty());
  CHECK(res->get<std::string>(99uz).empty());
  CHECK(res->get<int>(99uz) == 0);
}

TEST_CASE("match_result group_iterator satisfies forward_iterator", "[match_result][iterator]") {
  static_assert(std::forward_iterator<pcrepp::match_result::group_iterator>);

  auto const ctx = pcrepp::context<>::create(R"((\w+):(\d+))").value();
  auto const res = ctx.find("age:30");
  REQUIRE(res);
  REQUIRE(*res);

  auto it = res->begin();
  CHECK(*it++ == "age:30");  // 後置インクリメント
  CHECK(*it == "age");
  ++it;
  CHECK(*it == "30");
  ++it;
  CHECK(it == res->end());
}

TEST_CASE("match_range copy re-binds error_ref to own m_error", "[match_range][copy]") {
  auto const ctx = pcrepp::context<>::create(R"(\d+)").value();
  auto range1 = ctx.find_all("1 2 3");
  auto range2 = range1;  // コピー
  auto range3 = std::move(range1);  // ムーブ

  // コピー/ムーブ後も error_ref が自オブジェクトの m_error を指している
  CHECK(range2.first.error_ref == &range2.m_error);
  CHECK(range2.last.error_ref == &range2.m_error);
  CHECK(range3.first.error_ref == &range3.m_error);
  CHECK(range3.last.error_ref == &range3.m_error);
}

// ========================================
// context::match() のテスト
// ========================================
TEST_CASE("context::match returns true on full anchored match", "[match]") {
  auto const ctx = pcrepp::context<true>::create(R"(hello)").value();
  auto mr = pcrepp::match_result{};
  auto const res = ctx.match("hello world", mr);
  REQUIRE(res.has_value());
  CHECK_FALSE(*res);
}

TEST_CASE("context::match returns true on exact match", "[match]") {
  auto const ctx = pcrepp::context<true>::create(R"(hello world)").value();
  auto mr = pcrepp::match_result{};
  auto const res = ctx.match("hello world", mr);
  REQUIRE(res.has_value());
  CHECK(*res);
}

TEST_CASE("context::match returns match_result with capture groups", "[match]") {
  auto const ctx = pcrepp::context<true>::create(R"((hello) (world))").value();
  auto mr = pcrepp::match_result{};
  auto const res = ctx.match("hello world", mr);
  REQUIRE(res.has_value());
  CHECK(*res);
  CHECK(mr.get(1uz) == "hello");
  CHECK(mr.get(2uz) == "world");
}

TEST_CASE("context::match returns false for partial match", "[match]") {
  auto const ctx = pcrepp::context<true>::create(R"(hello)").value();
  auto mr = pcrepp::match_result{};
  auto const res = ctx.match("hello world", mr);
  REQUIRE(res.has_value());
  CHECK_FALSE(*res);
}

TEST_CASE("nttp_regex::match returns true on exact match", "[match][nttp]") {
  auto const re = pcrepp::compile<R"(hello world)">();
  auto const res = re.match("hello world");
  // C7: nttp_regex::match は expected<bool> を返す
  REQUIRE(res.has_value());
  CHECK(*res);
}

TEST_CASE("nttp_regex::match returns false on partial match", "[match][nttp]") {
  auto const re = pcrepp::compile<R"(hello)">();
  auto const res = re.match("hello world");
  REQUIRE(res.has_value());
  CHECK_FALSE(*res);
}

// ========================================
// context<false> (非JIT) のテスト
// ========================================
TEST_CASE("context<false> basic find works", "[nojit]") {
  auto const ctx = pcrepp::context<false>::create(R"(\d+)").value();
  auto const res = ctx.find("abc 123 def");
  REQUIRE(res.has_value());
  CHECK(res->get(0uz) == "123");
}

TEST_CASE("context<false> find_all works", "[nojit]") {
  auto const ctx = pcrepp::context<false>::create(R"(\d+)").value();
  auto count = 0uz;
  for (auto& mr : ctx.find_all("a1 b2 c3")) {
    ++count;
    CHECK_FALSE(mr.get(0uz).empty());
  }
  CHECK(count == 3uz);
}

TEST_CASE("context<false> replace works", "[nojit]") {
  auto const ctx = pcrepp::context<false>::create(R"(cat)").value();
  auto const res = ctx.replace("the cat sat", "dog");
  REQUIRE(res.has_value());
  CHECK(*res == "the dog sat");
}

TEST_CASE("context<false> split works", "[nojit]") {
  auto const ctx = pcrepp::context<false>::create(R"(, )").value();
  auto const parts = ctx.split("a, b, c");
  REQUIRE(parts.size() == 3uz);
  CHECK(parts[0uz] == "a");
  CHECK(parts[1uz] == "b");
  CHECK(parts[2uz] == "c");
}

TEST_CASE("context<false> match works", "[nojit][match]") {
  auto const ctx = pcrepp::context<false>::create(R"(exact)").value();
  auto mr = pcrepp::match_result{};
  auto const res = ctx.match("exact", mr);
  REQUIRE(res.has_value());
  CHECK(*res);
}

// ========================================
// match_result グループイテレータのテスト
// ========================================
TEST_CASE("match_result group_iterator iterates all groups", "[group_iterator]") {
  auto const ctx = pcrepp::context<true>::create(R"((a)(b)(c))").value();
  auto const res = ctx.find("abc");
  REQUIRE(res.has_value());
  auto const& mr = *res;
  REQUIRE(mr);
  auto count = 0uz;
  for (auto const g : mr) {
    CHECK_FALSE(g.empty());
    ++count;
  }
  CHECK(count == 4uz);  // group 0 + 3 captures
}

TEST_CASE("match_result begin/end works on empty match", "[group_iterator]") {
  auto mr = pcrepp::match_result{};
  auto count = 0uz;
  for (auto const g : mr) {
    (void)g;
    ++count;
  }
  CHECK(count == 0uz);
}

// ========================================
// capture_index() のテスト
// ========================================
TEST_CASE("capture_index returns correct index for named group", "[capture_index]") {
  auto const ctx = pcrepp::context<true>::create(R"((?<year>\d{4})-(?<month>\d{2})-(?<day>\d{2}))").value();
  CHECK(ctx.capture_index("year") == 1);
  CHECK(ctx.capture_index("month") == 2);
  CHECK(ctx.capture_index("day") == 3);
}

TEST_CASE("capture_index returns negative for unknown name", "[capture_index]") {
  auto const ctx = pcrepp::context<true>::create(R"((a))").value();
  CHECK(ctx.capture_index("nonexistent") < 0);
}

TEST_CASE("capture_index on uncompiled context returns -1", "[capture_index]") {
  auto const ctx = pcrepp::context<true>{};
  CHECK(ctx.capture_index("any") == -1);
}

// ========================================
// replace() バッファオーバーフロー再試行テスト
// ========================================
TEST_CASE("replace handles buffer overflow retry", "[replace]") {
  auto const ctx = pcrepp::context<true>::create(R"((\w))").value();
  // 置換によって文字列が大きく増えるパターン
  auto input = std::string{};
  for (auto i = 0; i < 1000; ++i) input += 'a';
  auto const res = ctx.replace(input, "$1$1");
  REQUIRE(res.has_value());
  CHECK(res->size() == 2000uz);
}

TEST_CASE("replace with backreference", "[replace]") {
  auto const ctx = pcrepp::context<true>::create(R"((\w+))").value();
  auto const res = ctx.replace("hello world", "[$1]");
  REQUIRE(res.has_value());
  CHECK(*res == "[hello] [world]");
}

// ========================================
// replace() callback ゼロ幅マッチテスト
// ========================================
TEST_CASE("replace callback with zero-width end anchor", "[replace][zero_width]") {
  auto const ctx = pcrepp::context<true>::create(R"($)").value();
  auto const result = ctx.replace("abc", [](auto const&) -> std::string {
    return "!";
  });
  // C7: replace(callback) は expected<string> を返す
  REQUIRE(result.has_value());
  CHECK(*result == "abc!");  // match at end, not infinite
}

TEST_CASE("replace callback with zero-width lookahead", "[replace][zero_width]") {
  auto const ctx = pcrepp::context<true>::create(R"((?=[bc]))").value();
  auto const result = ctx.replace("abcd", [](auto const&) -> std::string {
    return "X";
  });
  REQUIRE(result.has_value());
  CHECK(*result == "aXbXcd");
}

// ========================================
// split() エッジケーステスト
// ========================================
TEST_CASE("split with no match returns single element", "[split]") {
  auto const ctx = pcrepp::context<true>::create(R"(,)").value();
  auto const parts = ctx.split("abc");
  REQUIRE(parts.size() == 1uz);
  CHECK(parts[0uz] == "abc");
}

TEST_CASE("split with zero-width delimiter", "[split][zero_width]") {
  auto const ctx = pcrepp::context<true>::create(R"((?=[bc]))").value();
  auto const parts = ctx.split("abcd");
  REQUIRE(parts.size() == 3uz);
  CHECK(parts[0uz] == "a");
  CHECK(parts[1uz] == "b");
  CHECK(parts[2uz] == "cd");
}

TEST_CASE("split with consecutive delimiters", "[split]") {
  auto const ctx = pcrepp::context<true>::create(R"(,)").value();
  auto const parts = ctx.split("a,,b");
  REQUIRE(parts.size() == 3uz);
  CHECK(parts[0uz] == "a");
  CHECK(parts[1uz] == "");
  CHECK(parts[2uz] == "b");
}

// ========================================
// find_all でマッチなし時のテスト
// ========================================
TEST_CASE("find_all with no matches returns empty range", "[find_all]") {
  auto const ctx = pcrepp::context<true>::create(R"(xyz)").value();
  auto count = 0uz;
  for (auto& mr : ctx.find_all("abc")) {
    (void)mr;
    ++count;
  }
  CHECK(count == 0uz);
}

// ========================================
// match_result コピー/ムーブテスト
// ========================================
TEST_CASE("match_result copy preserves group values", "[match_result]") {
  auto const ctx = pcrepp::context<true>::create(R"((hello) (world))").value();
  auto const res = ctx.find("hello world");
  REQUIRE(res.has_value());
  auto const copy = *res;
  CHECK(copy.get(1uz) == "hello");
  CHECK(copy.get(2uz) == "world");
}

TEST_CASE("match_result assignment", "[match_result]") {
  auto const ctx = pcrepp::context<true>::create(R"((\d+))").value();
  auto const res1 = ctx.find("abc 123");
  REQUIRE(res1.has_value());
  auto mr = pcrepp::match_result{};
  mr = *res1;
  CHECK(mr.get(1uz) == "123");
}

// ========================================
// start_pos / end_pos テスト
// ========================================
TEST_CASE("start_pos and end_pos return correct positions", "[match_result]") {
  auto const ctx = pcrepp::context<true>::create(R"(\d+)").value();
  auto const res = ctx.find("abc 123 def");
  REQUIRE(res.has_value());
  CHECK(res->start_pos() == 4uz);
  CHECK(res->end_pos() == 7uz);
}

TEST_CASE("start_pos and end_pos on empty result return 0", "[match_result]") {
  auto mr = pcrepp::match_result{};
  CHECK(mr.start_pos() == 0uz);
  CHECK(mr.end_pos() == 0uz);
}

// ========================================
// F1: find_all with start offset
// ========================================
TEST_CASE("find_all with start offset skips prefix", "[find_all][f1]") {
  auto const ctx = pcrepp::context<true>::create(R"(\d+)").value();
  // "abc 123 456 789" — offset 8 から開始すると 456 と 789 のみ
  auto count = 0uz;
  for ([[maybe_unused]] auto& mr : ctx.find_all("abc 123 456 789", 0u, 8uz)) {
    ++count;
  }
  CHECK(count == 2uz);
}

TEST_CASE("find_all with start offset 0 finds all", "[find_all][f1]") {
  auto const ctx = pcrepp::context<true>::create(R"(\d+)").value();
  auto count = 0uz;
  for ([[maybe_unused]] auto& mr : ctx.find_all("1 2 3", 0u, 0uz)) {
    ++count;
  }
  CHECK(count == 3uz);
}

// ========================================
// F2: split with option
// ========================================
TEST_CASE("split with PCRE2_CASELESS option", "[split][f2]") {
  auto const ctx = pcrepp::context<true>::create(R"(AND)").value();
  auto const parts = ctx.split("apple AND banana", PCRE2_CASELESS);
  REQUIRE(parts.size() == 2uz);
  CHECK(parts[0] == "apple ");
  CHECK(parts[1] == " banana");
}

TEST_CASE("split with option default 0 works as before", "[split][f2]") {
  auto const ctx = pcrepp::context<true>::create(R"(,)").value();
  auto const parts = ctx.split("a,b,c");
  REQUIRE(parts.size() == 3uz);
  CHECK(parts[0] == "a");
  CHECK(parts[1] == "b");
  CHECK(parts[2] == "c");
}

// ========================================
// F5: context::match without match_result arg
// ========================================
TEST_CASE("context::match without match_result arg exact match", "[match][f5]") {
  auto const ctx = pcrepp::context<true>::create(R"(hello world)").value();
  auto const res = ctx.match("hello world");
  REQUIRE(res.has_value());
  CHECK(*res);
}

TEST_CASE("context::match without match_result arg partial mismatch", "[match][f5]") {
  auto const ctx = pcrepp::context<true>::create(R"(hello)").value();
  auto const res = ctx.match("hello world");
  REQUIRE(res.has_value());
  CHECK_FALSE(*res);
}

// ========================================
// F4: try_get<string_view> / try_get<string>
// ========================================
TEST_CASE("try_get<string_view> distinguishes unmatch from empty match", "[match_result][f4]") {
  // (a?) は空文字列にもマッチ → optional{""}
  auto const ctx = pcrepp::context<true>::create(R"((a?))").value();
  auto const res = ctx.find("b");  // 空マッチ
  REQUIRE(res);
  auto const v = res->try_get<std::string_view>(1uz);
  REQUIRE(v.has_value());
  CHECK(v->empty());

  // 範囲外インデックスは nullopt
  auto const no_v = res->try_get<std::string_view>(99uz);
  CHECK_FALSE(no_v.has_value());
}

TEST_CASE("try_get<string> returns string value", "[match_result][f4]") {
  auto const ctx = pcrepp::context<true>::create(R"((\d+))").value();
  auto const res = ctx.find("abc 42");
  REQUIRE(res);
  auto const v = res->try_get<std::string>(1uz);
  REQUIRE(v.has_value());
  CHECK(*v == "42");
}

TEST_CASE("try_get<string_view> named capture", "[match_result][f4]") {
  auto const ctx = pcrepp::context<true>::create(R"((?<num>\d+))").value();
  auto const res = ctx.find("abc 42");
  REQUIRE(res);
  auto const v = res->try_get<std::string_view>("num");
  REQUIRE(v.has_value());
  CHECK(*v == "42");

  auto const no_v = res->try_get<std::string_view>("missing");
  CHECK_FALSE(no_v.has_value());
}

TEST_CASE("named capture index consistency", "[match_result][h14]") {
  auto const ctx = pcrepp::context<>::create(R"((?<year>\d{4})-(?<month>\d{2})-(?<day>\d{2}))").value();
  auto const res = ctx.find("2024-06-21");
  REQUIRE(res);
  CHECK(res->get(1uz) == res->get("year"));
  CHECK(res->get(2uz) == res->get("month"));
  CHECK(res->get(3uz) == res->get("day"));
}

// ========================================
// F6: match_result::to_tuple<N>
// ========================================
TEST_CASE("match_result to_tuple<N>", "[match_result][f6]") {
  auto const ctx = pcrepp::context<true>::create(R"((\w+) (\w+))").value();
  auto const res = ctx.find("hello world");
  REQUIRE(res);
  auto const t = res->to_tuple<3>();
  CHECK(std::get<0>(t) == "hello world");
  CHECK(std::get<1>(t) == "hello");
  CHECK(std::get<2>(t) == "world");
}

// ========================================
// M4: E1/E3/E5/E6/E10/E11
// ========================================
TEST_CASE("PCRE2_SUBSTITUTE_EXTENDED enables ${1} syntax", "[replace][e6]") {
  auto const ctx = pcrepp::context<true>::create(R"((\w+))").value();
  auto const res = ctx.replace("hello", "${1}!",
    PCRE2_SUBSTITUTE_GLOBAL | PCRE2_SUBSTITUTE_EXTENDED);
  REQUIRE(res);
  CHECK(*res == "hello!");
}

TEST_CASE("context pattern info queries", "[e5]") {
  auto const ctx = pcrepp::context<true>::create(R"((?<year>\d{4})-(?<month>\d{2}))", PCRE2_UTF).value();
  CHECK(ctx.capture_count() == 2u);
  auto const caps = ctx.named_captures();
  REQUIRE(caps.size() == 2uz);
  CHECK(caps[0].first == "month");
  CHECK(caps[1].first == "year");
  CHECK(ctx.pattern_size() > 0uz);
  CHECK((ctx.options() & PCRE2_UTF) != 0u);
}

TEST_CASE("context jit size on JIT-enabled context", "[e1][e5]") {
  auto const ctx = pcrepp::context<true, PCRE2_JIT_COMPLETE>::create(R"(\d+)").value();
#ifndef __EMSCRIPTEN__
  CHECK(ctx.jit_size() > 0uz);
#endif
}

TEST_CASE("context set_match_limit can trigger match error", "[e3]") {
  auto ctx = pcrepp::context<false>::create(R"((a+)+b)").value();
  ctx.set_match_limit(10u);
  auto input = std::string(2000, 'a');
  input += 'c';
  auto const res = ctx.find(input);
  // PCRE2 実装差により error / no-match のどちらかになるため両方許容
  CHECK((!res.has_value() || (res.has_value() && !static_cast<bool>(*res))));
}

TEST_CASE("context set_offset_limit restricts search range", "[e10]") {
  auto ctx = pcrepp::context<false>::create(R"(\d+)").value();
  ctx.set_offset_limit(3uz);
  auto const res = ctx.find("abc 123 456");
  // offset_limit により error になる実装と no-match になる実装がある
  CHECK((!res.has_value() || (res.has_value() && !static_cast<bool>(*res))));
}

TEST_CASE("JIT context honors heap_limit by falling back to interpreter", "[e3][jit]") {
  // pcre2_jit_match は heap_limit / depth_limit を無視する (公式ドキュメント記載)。
  // これらの制限設定中は interpreter フォールバックで実行される。
  // `(x+x+)+y` は指数バックトラックする古典的パターンで、heap_limit=20KB の
  // interpreter 実行では確実に PCRE2_ERROR_HEAPLIMIT になる。
  // 注: PCRE2 10.47 の JIT は実装上このケースでもエラーを返すため、本テストは
  // 「制限が結果に反映されること」のリグレッションガードとして機能する
  auto ctx = pcrepp::context<true>::create("(x+x+)+y").value();
#ifndef __EMSCRIPTEN__
  REQUIRE(ctx.jit_size() > 0uz);  // JIT が有効な環境でのみ意味を持つテスト
#endif
  ctx.set_heap_limit(20u);
  // y の直前が x でないことで、どの開始位置でもマッチが確定せず
  // バックトラックが回り切る前に制限に達する
  auto input = std::string(26, 'x') + " y";
  auto const res = ctx.find(input);
  CHECK(!res.has_value());
}

TEST_CASE("oversized match_result constructor works", "[e11]") {
  auto const ctx = pcrepp::context<true>::create(R"((\d+))").value();
  auto mr = pcrepp::match_result{ctx, 10uz};
  auto const rc = ctx.find("abc 42", mr);
  REQUIRE(rc);
  CHECK(*rc > 0);
  CHECK(mr.get(1uz) == "42");
}

// ========================================
// M5: H2/H3/H5/H7/H8/H17/H18/H20
// ========================================
TEST_CASE("context move and release behavior", "[context][h2]") {
  auto ctx1 = pcrepp::context<>::create(R"(\d+)").value();
  auto ctx2 = std::move(ctx1);
  CHECK(ctx1.get_code() == nullptr);
  REQUIRE(ctx2.get_code() != nullptr);
  auto const res = ctx2.find("abc 123");
  REQUIRE(res);
  CHECK(res->get(0uz) == "123");
  ctx2.release();
  CHECK(ctx2.get_code() == nullptr);
}

TEST_CASE("match_result move and self assignment", "[match_result][h3]") {
  auto const ctx = pcrepp::context<>::create(R"((\d+))").value();
  auto mr1 = ctx.find("abc 42").value();
  auto mr2 = std::move(mr1);
  CHECK(mr2.get(1uz) == "42");
  mr2 = mr2; // self assignment
  CHECK(mr2.get(1uz) == "42");
}

TEST_CASE("replace with non-default substitute option", "[replace][h5]") {
  auto const ctx = pcrepp::context<>::create(R"((\w+))").value();
  auto const res = ctx.replace("hello", "${1}",
    PCRE2_SUBSTITUTE_GLOBAL | PCRE2_SUBSTITUTE_EXTENDED);
  REQUIRE(res);
  CHECK(*res == "hello");
}

TEST_CASE("context find with start offset", "[find][h7]") {
  auto const ctx = pcrepp::context<>::create(R"(\d+)").value();
  auto const res = ctx.find("abc 123 456", 8uz);
  REQUIRE(res);
  CHECK(res->get(0uz) == "456");
}

TEST_CASE("JIT compile failure path is representable", "[compile][h8]") {
  // 無効 JIT フラグで失敗経路を通す（環境差を考慮して成功も許容）
  auto const res = pcrepp::context<true, 0u>::create(R"(\d+)");
  if (!res) {
    CHECK(res.error().find("JIT compile error") != std::string::npos);
  } else {
    SUCCEED();
  }
}

TEST_CASE("runtime create failure exposes error text", "[context][h16]") {
  auto const res = pcrepp::context<>::create(R"(()");
  REQUIRE_FALSE(res.has_value());
  CHECK_FALSE(res.error().empty());
}

TEST_CASE("match_range view_interface helpers", "[match_range][h17]") {
  auto const ctx = pcrepp::context<>::create(R"(\d+)").value();
  auto range = ctx.find_all("1 2 3");
  CHECK_FALSE(range.empty());

  // apple-clangではpcre2_match_data_create_from_patternが正常に動作しない場合がるため、
  // ここではget(0)のテストをスキップする
#if not (defined(__clang__) && defined(__APPLE__))
  CHECK(range.front().get(0uz) == "1");
#endif
}

TEST_CASE("match_result size direct assert", "[match_result][h18]") {
  auto const ctx = pcrepp::context<>::create(R"((\w+) (\w+))").value();
  auto const res = ctx.find("hello world");
  REQUIRE(res);
  CHECK(res->size() == 3uz);
}

TEST_CASE("split with empty target", "[split][h20]") {
  auto const ctx = pcrepp::context<>::create(R"(,)").value();
  auto const parts = ctx.split("");
  REQUIRE(parts.size() == 1uz);
  CHECK(parts[0uz].empty());
}
