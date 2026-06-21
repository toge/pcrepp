#include "pcrepp.hpp"
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

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
