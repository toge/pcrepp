#include "pcrepp.hpp"
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
void check_empty_match_result(pcrepp::match_result const& result) {
  CHECK_FALSE(static_cast<bool>(result));
  CHECK(result.size() == 0uz);
  CHECK(result[0uz].empty());
  CHECK(result.get("v").empty());
  CHECK_FALSE(result.try_get<std::string_view>(0uz).has_value());
  CHECK_FALSE(result.try_get<std::string_view>("v").has_value());
  CHECK(result.start_pos() == 0uz);
  CHECK(result.end_pos() == 0uz);
  CHECK(result.begin() == result.end());
}
}  // namespace

TEST_CASE("review: match_result state is not allocation state", "[review][match_result]") {
  auto                 ctx = pcrepp::context<>::create(R"((?<v>a))").value();
  pcrepp::match_result result{ctx};
  check_empty_match_result(result);

  auto found = ctx.find("a", result);
  REQUIRE(found);
  CHECK(*found == 2);
  CHECK(static_cast<bool>(result));
  CHECK(result.size() == 2uz);
  CHECK(result[0uz] == "a");
  CHECK(result["v"] == "a");

  found = ctx.find("z", result);
  REQUIRE(found);
  CHECK(*found == 0);
  check_empty_match_result(result);

  found = ctx.find("a", result);
  REQUIRE(found);
  CHECK(*found == 2);
  found = ctx.find("a", result, 2uz);
  CHECK_FALSE(found);
  check_empty_match_result(result);
}

TEST_CASE("review: match_result keeps compiled code alive", "[review][match_result][lifetime]") {
  auto const           subject = std::string_view{"a"};
  auto                 copied  = std::optional<pcrepp::match_result>{};
  pcrepp::match_result assigned;
  pcrepp::match_result moved;
  pcrepp::match_result tls;

  {
    auto ctx   = pcrepp::context<>::create(R"((?<v>a))").value();
    auto found = ctx.find(subject);
    REQUIRE(found);
    REQUIRE(*found);
    copied.emplace(*found);
    assigned = *found;
    moved    = ctx.find(subject).value();

    auto tls_found = ctx.find(subject, pcrepp::use_tls);
    REQUIRE(tls_found);
    REQUIRE(*tls_found);
    tls = *tls_found;
  }

  REQUIRE(copied);
  CHECK(static_cast<bool>(*copied));
  CHECK(copied->get(1uz) == "a");
  CHECK(copied->get("v") == "a");

  CHECK(static_cast<bool>(assigned));
  CHECK(assigned.get(1uz) == "a");
  CHECK(assigned.get("v") == "a");

  CHECK(static_cast<bool>(moved));
  CHECK(moved.get(1uz) == "a");
  CHECK(moved.get("v") == "a");

  CHECK(static_cast<bool>(tls));
  CHECK(tls.get(1uz) == "a");
  CHECK(tls.get("v") == "a");
}

TEST_CASE("review: match_result tuple conversion bounds-checks oversized tuples", "[review][match_result][oob]") {
  auto const result = pcrepp::context<>::create(R"((a))").value().find("a");
  REQUIRE(result);
  REQUIRE(*result);

  auto const tuple = result->to_tuple<3uz>();
  CHECK(std::get<0uz>(tuple) == "a");
  CHECK(std::get<1uz>(tuple) == "a");
  CHECK(std::get<2uz>(tuple).empty());
}

TEST_CASE("review: existing result cannot be reused across contexts", "[review][match_result]") {
  auto                 a = pcrepp::context<>::create(R"((?<v>a))").value();
  auto                 b = pcrepp::context<>::create(R"((?<v>b))").value();
  pcrepp::match_result result{a};

  REQUIRE(a.find("a", result));
  REQUIRE(result);

  auto const reused = b.find("a", result);
  REQUIRE_FALSE(reused);
  CHECK(reused.error().find("different") != std::string::npos);
  check_empty_match_result(result);
}

TEST_CASE("review: JIT dispatch preserves anchoring options", "[review][jit][options]") {
  auto const jit   = pcrepp::context<true>::create("b").value();
  auto const nojit = pcrepp::context<false>::create("b").value();

  auto const jit_result   = jit.find("ab", 0uz, PCRE2_ANCHORED);
  auto const nojit_result = nojit.find("ab", 0uz, PCRE2_ANCHORED);
  REQUIRE(jit_result);
  REQUIRE(nojit_result);
  CHECK_FALSE(*jit_result);
  CHECK(static_cast<bool>(*jit_result) == static_cast<bool>(*nojit_result));

  auto const jit_no_jit   = jit.find("ab", 0uz, PCRE2_ANCHORED | PCRE2_NO_JIT);
  auto const nojit_no_jit = nojit.find("ab", 0uz, PCRE2_ANCHORED | PCRE2_NO_JIT);
  REQUIRE(jit_no_jit);
  REQUIRE(nojit_no_jit);
  CHECK_FALSE(*jit_no_jit);
  CHECK(static_cast<bool>(*jit_no_jit) == static_cast<bool>(*nojit_no_jit));
}

TEST_CASE("review: JIT dispatch validates invalid UTF", "[review][jit][utf]") {
  auto const jit   = pcrepp::context<true>::create(".", PCRE2_UTF).value();
  auto const nojit = pcrepp::context<false>::create(".", PCRE2_UTF).value();
  auto const input = std::string_view{"\xff", 1uz};

  auto const jit_result   = jit.find(input);
  auto const nojit_result = nojit.find(input);
  REQUIRE_FALSE(nojit_result);
  REQUIRE_FALSE(jit_result);
  CHECK(jit_result.error() == nojit_result.error());
}

TEST_CASE("review: full match uses both anchoring options", "[review][jit][match]") {
  auto const verify = [](std::string_view const pattern) {
    auto const jit   = pcrepp::context<true>::create(pattern).value();
    auto const nojit = pcrepp::context<false>::create(pattern).value();

    auto const jit_result   = jit.match("ab");
    auto const nojit_result = nojit.match("ab");
    REQUIRE(jit_result);
    REQUIRE(nojit_result);
    CHECK(*jit_result);
    CHECK(*nojit_result);
  };

  verify("a|ab");
  verify(R"(a.*?|a.*)");
}

TEST_CASE("review: full match accepts partial modes for complete subjects", "[review][jit][match][options]") {
  using namespace pcrepp;
  auto const verify = [](unsigned int const option) {
    auto const jit   = pcrepp::context<true>::create("abc").value();
    auto const nojit = pcrepp::context<false>::create("abc").value();

    auto const jit_result   = jit.match("abc", option);
    auto const nojit_result = nojit.match("abc", option);
    REQUIRE(jit_result);
    REQUIRE(nojit_result);
    CHECK(*jit_result);
    CHECK(*nojit_result);

    auto const nttp_result  = pcrepp::match<"abc">("abc", option);
    auto const nojit_nttp   = pcrepp::match<"abc", false>("abc", option);
    auto const regex_result = R"(abc)"_re.match("abc", option);
    REQUIRE(nttp_result);
    REQUIRE(nojit_nttp);
    REQUIRE(regex_result);
    CHECK(*nttp_result);
    CHECK(*nojit_nttp);
    CHECK(*regex_result);
  };

  verify(PCRE2_PARTIAL_HARD);
  verify(PCRE2_PARTIAL_SOFT);
}

TEST_CASE("review: partial modes still backtrack to a full ordered alternative", "[review][jit][match][options]") {
  using namespace pcrepp;
  auto const verify = [](unsigned int const option) {
    auto const jit   = pcrepp::context<true>::create("a|ab").value();
    auto const nojit = pcrepp::context<false>::create("a|ab").value();

    auto const jit_result   = jit.match("ab", option);
    auto const nojit_result = nojit.match("ab", option);
    REQUIRE(jit_result);
    REQUIRE(nojit_result);
    CHECK(*jit_result);
    CHECK(*nojit_result);

    auto const nttp_result  = pcrepp::match<"a|ab">("ab", option);
    auto const nojit_nttp   = pcrepp::match<"a|ab", false>("ab", option);
    auto const regex_result = R"(a|ab)"_re.match("ab", option);
    REQUIRE(nttp_result);
    REQUIRE(nojit_nttp);
    REQUIRE(regex_result);
    CHECK(*nttp_result);
    CHECK(*nojit_nttp);
    CHECK(*regex_result);
  };

  verify(PCRE2_PARTIAL_HARD);
  verify(PCRE2_PARTIAL_SOFT);
}

TEST_CASE("review: incomplete full match preserves partial-mode errors", "[review][jit][match][options]") {
  using namespace pcrepp;
  auto const verify = [](unsigned int const option) {
    auto const jit   = pcrepp::context<true>::create("abc").value();
    auto const nojit = pcrepp::context<false>::create("abc").value();

    auto const jit_result      = jit.match("ab", option);
    auto const nojit_result    = nojit.match("ab", option);
    auto const jit_reference   = jit.find("ab", 0uz, option | PCRE2_ANCHORED);
    auto const nojit_reference = nojit.find("ab", 0uz, option | PCRE2_ANCHORED);
    REQUIRE_FALSE(jit_reference);
    REQUIRE_FALSE(nojit_reference);
    REQUIRE_FALSE(jit_result);
    REQUIRE_FALSE(nojit_result);
    CHECK(jit_result.error() == jit_reference.error());
    CHECK(nojit_result.error() == nojit_reference.error());
    CHECK(jit_result.error().find("code " + std::to_string(PCRE2_ERROR_PARTIAL)) != std::string::npos);

    auto const nttp_result     = pcrepp::match<"abc">("ab", option);
    auto const nojit_nttp      = pcrepp::match<"abc", false>("ab", option);
    auto const regex_result    = R"(abc)"_re.match("ab", option);
    auto const nttp_reference  = pcrepp::find<"abc">("ab", 0uz, option | PCRE2_ANCHORED);
    auto const nojit_nttp_ref  = pcrepp::find<"abc", false>("ab", 0uz, option | PCRE2_ANCHORED);
    auto const regex_reference = R"(abc)"_re.find("ab", 0uz, option | PCRE2_ANCHORED);
    REQUIRE_FALSE(nttp_reference);
    REQUIRE_FALSE(nojit_nttp_ref);
    REQUIRE_FALSE(regex_reference);
    REQUIRE_FALSE(nttp_result);
    REQUIRE_FALSE(nojit_nttp);
    REQUIRE_FALSE(regex_result);
    CHECK(nttp_result.error() == nttp_reference.error());
    CHECK(nojit_nttp.error() == nojit_nttp_ref.error());
    CHECK(regex_result.error() == regex_reference.error());
  };

  verify(PCRE2_PARTIAL_HARD);
  verify(PCRE2_PARTIAL_SOFT);
}

TEST_CASE("review: JIT contexts honor setter depth and heap limits", "[review][jit][e3]") {
  // 必須末尾 b の開始位置最適化だけで検索が脱落しないようにし、深さ制限まで到達させる。
  auto const depth_input = std::string(20uz, 'a') + 'c';
  auto       jit_depth   = pcrepp::context<true>::create("a").value();
  auto       nojit_depth = pcrepp::context<false>::create("a").value();
  jit_depth.set_depth_limit(10u);
  nojit_depth.set_depth_limit(10u);
  REQUIRE(jit_depth.compile("(*NO_START_OPT)(a+)+b"));
  REQUIRE(nojit_depth.compile("(*NO_START_OPT)(a+)+b"));

  auto const jit_depth_result   = jit_depth.find(depth_input);
  auto const nojit_depth_result = nojit_depth.find(depth_input);
  REQUIRE_FALSE(nojit_depth_result);
  REQUIRE_FALSE(jit_depth_result);
  CHECK(jit_depth_result.error() == nojit_depth_result.error());

  auto const heap_input = std::string(26uz, 'x') + " y";
  auto       jit_heap   = pcrepp::context<true>::create("(x+x+)+y").value();
  auto       nojit_heap = pcrepp::context<false>::create("(x+x+)+y").value();
  jit_heap.set_heap_limit(20u);
  nojit_heap.set_heap_limit(20u);

  auto const jit_heap_result   = jit_heap.find(heap_input);
  auto const nojit_heap_result = nojit_heap.find(heap_input);
  REQUIRE_FALSE(nojit_heap_result);
  REQUIRE_FALSE(jit_heap_result);
  CHECK(jit_heap_result.error() == nojit_heap_result.error());
}

TEST_CASE("review: JIT contexts honor inline pattern limits after move", "[review][jit][e3]") {
  auto const depth_input = std::string(20uz, 'a') + 'c';
  auto       original    = pcrepp::context<true>::create("(*NO_START_OPT)(*LIMIT_DEPTH=10)(a+)+b").value();
  auto       jit_depth   = std::move(original);
  auto const nojit_depth = pcrepp::context<false>::create("(*NO_START_OPT)(*LIMIT_DEPTH=10)(a+)+b").value();

  auto const jit_depth_result   = jit_depth.find(depth_input);
  auto const nojit_depth_result = nojit_depth.find(depth_input);
  REQUIRE_FALSE(nojit_depth_result);
  REQUIRE_FALSE(jit_depth_result);
  CHECK(jit_depth_result.error() == nojit_depth_result.error());
  CHECK_FALSE(original.find(depth_input));

  auto const heap_input = std::string(26uz, 'x') + " y";
  auto const jit_heap   = pcrepp::context<true>::create("(*LIMIT_HEAP=20)(x+x+)+y").value();
  auto const nojit_heap = pcrepp::context<false>::create("(*LIMIT_HEAP=20)(x+x+)+y").value();

  auto const jit_heap_result   = jit_heap.find(heap_input);
  auto const nojit_heap_result = nojit_heap.find(heap_input);
  REQUIRE_FALSE(nojit_heap_result);
  REQUIRE_FALSE(jit_heap_result);
  CHECK(jit_heap_result.error() == nojit_heap_result.error());
}

TEST_CASE("review: replacement receives active matching limits", "[review][jit][replace][e3]") {
  auto const verify_error_class = []<bool HasInlineLimit>(std::string_view const pattern, std::string_view const input) {
    auto jit   = pcrepp::context<true>::create(pattern).value();
    auto nojit = pcrepp::context<false>::create(pattern).value();
    if constexpr (not HasInlineLimit) {
      jit.set_depth_limit(10u);
      nojit.set_depth_limit(10u);
    }

    auto const jit_result   = jit.replace(input, "replacement");
    auto const nojit_result = nojit.replace(input, "replacement");
    REQUIRE_FALSE(nojit_result);
    REQUIRE_FALSE(jit_result);
    CHECK(jit_result.has_value() == nojit_result.has_value());
  };

  auto const                  depth_input = std::string(20uz, 'a') + 'c';
  verify_error_class.template operator()<false>("(*NO_START_OPT)(a+)+b", depth_input);
  verify_error_class.template operator()<true>("(*NO_START_OPT)(*LIMIT_DEPTH=10)(a+)+b", depth_input);

  auto const verify_heap_error_class = []<bool HasInlineLimit>(std::string_view const pattern, std::string_view const input) {
    auto jit   = pcrepp::context<true>::create(pattern).value();
    auto nojit = pcrepp::context<false>::create(pattern).value();
    if constexpr (not HasInlineLimit) {
      jit.set_heap_limit(20u);
      nojit.set_heap_limit(20u);
    }

    auto const jit_result   = jit.replace(input, "replacement");
    auto const nojit_result = nojit.replace(input, "replacement");
    REQUIRE_FALSE(nojit_result);
    REQUIRE_FALSE(jit_result);
    CHECK(jit_result.has_value() == nojit_result.has_value());
  };

  auto const                       heap_input = std::string(26uz, 'x') + " y";
  verify_heap_error_class.template operator()<false>("(x+x+)+y", heap_input);
  verify_heap_error_class.template operator()<true>("(*LIMIT_HEAP=20)(x+x+)+y", heap_input);
}

TEST_CASE("review: caller match options survive JIT dispatch", "[review][jit][options]") {
  auto const jit   = pcrepp::context<true>::create("^abc").value();
  auto const nojit = pcrepp::context<false>::create("^abc").value();

  auto const jit_result   = jit.find("abc", 0uz, PCRE2_NOTBOL);
  auto const nojit_result = nojit.find("abc", 0uz, PCRE2_NOTBOL);
  REQUIRE(jit_result);
  REQUIRE(nojit_result);
  CHECK_FALSE(*jit_result);
  CHECK(static_cast<bool>(*jit_result) == static_cast<bool>(*nojit_result));

  auto const jit_replacement   = jit.replace("abc", "x", PCRE2_NOTBOL);
  auto const nojit_replacement = nojit.replace("abc", "x", PCRE2_NOTBOL);
  REQUIRE(jit_replacement);
  REQUIRE(nojit_replacement);
  CHECK(*jit_replacement == "abc");
  CHECK(*jit_replacement == *nojit_replacement);
}

TEST_CASE("review: substitute overflow retry preserves match options", "[review][jit][replace][options]") {
  auto const jit         = pcrepp::context<true>::create("^a", PCRE2_MULTILINE).value();
  auto const nojit       = pcrepp::context<false>::create("^a", PCRE2_MULTILINE).value();
  auto const replacement = std::string(64uz, 'x');
  auto       input       = std::string{};
  auto       expected    = std::string{"a\n"};

  for (auto const line : std::views::iota(1, 80)) {
    input += "a\n";
    expected += replacement + '\n';
    std::ignore = line;
  }
  input.insert(0, "a\n");

  auto const options           = PCRE2_NOTBOL | PCRE2_SUBSTITUTE_GLOBAL;
  auto const jit_replacement   = jit.replace(input, replacement, options);
  auto const nojit_replacement = nojit.replace(input, replacement, options);
  REQUIRE(jit_replacement);
  REQUIRE(nojit_replacement);
  CHECK(*jit_replacement == expected);
  CHECK(*nojit_replacement == expected);
}

TEST_CASE("review: global replacement uses PCRE2 continuation semantics", "[review][iteration]") {
  auto const ctx      = pcrepp::context<>::create(R"(|a)").value();
  auto       seen     = std::vector<std::pair<size_t, size_t>>{};
  auto const callback = ctx.replace("a", [&](pcrepp::match_result const& result) {
    seen.emplace_back(result.start_pos(), result.end_pos());
    return "X";
  });
  auto const native   = ctx.replace("a", "X");
  auto const fast     = pcrepp::replace<R"(|a)", "X">("a");
  auto const legacy   = pcrepp::replace<R"(|a)">("a", "X");

  REQUIRE(callback);
  REQUIRE(native);
  REQUIRE(fast);
  REQUIRE(legacy);
  CHECK(*callback == "XXX");
  CHECK(*native == "XXX");
  CHECK(*fast == "XXX");
  CHECK(*legacy == "XXX");
  auto const expected = std::vector{std::pair{0uz, 0uz}, std::pair{0uz, 1uz}, std::pair{1uz, 1uz}};
  CHECK(seen == expected);
}

TEST_CASE("review: range iteration keeps application and continuation options", "[review][iteration]") {
  auto const zero_width = pcrepp::context<>::create(R"(|a)").value();
  auto       range      = zero_width.find_all("a");
  auto       positions  = std::vector<std::pair<size_t, size_t>>{};
  for (auto const& result : range) {
    positions.emplace_back(result.start_pos(), result.end_pos());
  }
  auto const expected = std::vector{std::pair{0uz, 0uz}, std::pair{0uz, 1uz}, std::pair{1uz, 1uz}};
  CHECK(positions == expected);
  CHECK(range.error().empty());

  auto same_offset_before = range.begin();
  auto same_offset_after  = same_offset_before;
  ++same_offset_after;
  CHECK(same_offset_before != same_offset_after);

  auto const noteol       = pcrepp::context<>::create(R"(a|$)").value();
  auto       noteol_range = noteol.find_all("ab", PCRE2_NOTEOL);
  auto       matches      = std::vector<std::pair<size_t, size_t>>{};
  for (auto const& result : noteol_range) {
    matches.emplace_back(result.start_pos(), result.end_pos());
  }
  CHECK(matches == std::vector{std::pair{0uz, 1uz}});
  CHECK(noteol_range.error().empty());
}

TEST_CASE("review: copied iterators retain cached continuation state", "[review][iteration]") {
  auto const ctx   = pcrepp::context<>::create(R"(\w+)").value();
  auto       range = ctx.find_all("aa bb");
  auto       first = range.begin();
  auto       copy  = first;
  auto const end   = range.end();

  auto previous = first++;
  REQUIRE(previous != end);
  REQUIRE(first != end);
  REQUIRE(copy != end);
  CHECK((*previous)[0] == "aa");
  CHECK((*first)[0] == "bb");
  CHECK((*copy)[0] == "aa");

  ++previous;
  REQUIRE(previous != end);
  CHECK((*previous)[0] == "bb");

  ++copy;
  REQUIRE(copy != end);
  CHECK((*copy)[0] == "bb");
  ++first;
  ++previous;
  ++copy;
  CHECK(first == end);
  CHECK(previous == end);
  CHECK(copy == end);
  CHECK(range.error().empty());
}

TEST_CASE("review: UTF global iteration advances by codepoints", "[iteration][utf]") {
  auto const ctx     = pcrepp::context<>::create("", PCRE2_UTF).value();
  auto       range   = ctx.find_all("あ");
  auto       offsets = std::vector<size_t>{};
  for (auto const& result : range) {
    offsets.push_back(result.start_pos());
  }

  CHECK(static_cast<bool>(range));
  CHECK(range.error().empty());
  CHECK(offsets == std::vector{0uz, 3uz});
}

TEST_CASE("review: CRLF global iteration follows PCRE2 transitions", "[iteration][crlf]") {
  auto const ctx     = pcrepp::context<>::create(R"((?=\r?$))", PCRE2_MULTILINE).value();
  auto       range   = ctx.find_all("a\r\nb");
  auto       offsets = std::vector<size_t>{};
  for (auto const& result : range) {
    offsets.push_back(result.start_pos());
  }

  CHECK(static_cast<bool>(range));
  CHECK(range.error().empty());
  CHECK(offsets == std::vector{1uz, 2uz, 4uz});
}

TEST_CASE("review: match_range publishes initial matching errors", "[iteration][error]") {
  SECTION("invalid start") {
    auto const ctx   = pcrepp::context<>::create(R"(.)").value();
    auto       range = ctx.find_all("a", 0u, 2uz);
    CHECK_FALSE(static_cast<bool>(range));
    CHECK_FALSE(range.error().empty());
    CHECK(range.error().find("start offset") != std::string_view::npos);
    CHECK(range.begin() == range.end());
  }

  SECTION("invalid UTF") {
    auto const ctx   = pcrepp::context<>::create(R"(.)", PCRE2_UTF).value();
    auto const input = std::string_view{"\xff", 1uz};
    auto       range = ctx.find_all(input);
    CHECK_FALSE(static_cast<bool>(range));
    CHECK_FALSE(range.error().empty());
    CHECK(range.begin() == range.end());
  }

  SECTION("initial match limit") {
    auto ctx = pcrepp::context<>::create(R"(a|((b+)+c))").value();
    ctx.set_depth_limit(10u);
    auto const input = std::string(20uz, 'b') + 'x';
    auto       range = ctx.find_all(input);
    CHECK_FALSE(static_cast<bool>(range));
    CHECK(range.error().find("code " + std::to_string(PCRE2_ERROR_DEPTHLIMIT)) != std::string_view::npos);
    CHECK(range.begin() == range.end());
  }

  SECTION("no match is not an error") {
    auto const ctx   = pcrepp::context<>::create(R"(z)").value();
    auto       range = ctx.find_all("abc");
    CHECK(static_cast<bool>(range));
    CHECK(range.error().empty());
    CHECK(range.begin() == range.end());
  }
}

TEST_CASE("review: match_range copy and move rebind matching errors", "[iteration][error]") {
  auto const ctx = pcrepp::context<>::create(R"(.)").value();

  auto invalid_source = ctx.find_all("a", 0u, 2uz);
  REQUIRE_FALSE(static_cast<bool>(invalid_source));
  auto const initial_error = std::string{invalid_source.error()};

  auto copied = invalid_source;
  CHECK(copied.first.error_ref == &copied.m_error);
  CHECK(copied.last.error_ref == &copied.m_error);
  CHECK(copied.error() == initial_error);

  auto copy_assigned = decltype(invalid_source){};
  copy_assigned      = invalid_source;
  CHECK(copy_assigned.first.error_ref == &copy_assigned.m_error);
  CHECK(copy_assigned.last.error_ref == &copy_assigned.m_error);
  CHECK(copy_assigned.error() == initial_error);

  auto move_source = ctx.find_all("a", 0u, 2uz);
  auto moved       = std::move(move_source);
  CHECK(moved.first.error_ref == &moved.m_error);
  CHECK(moved.last.error_ref == &moved.m_error);
  CHECK(moved.error() == initial_error);
  CHECK(move_source.first.error_ref == &move_source.m_error);
  CHECK(move_source.last.error_ref == &move_source.m_error);

  auto move_assign_source = ctx.find_all("a", 0u, 2uz);
  auto move_assigned      = decltype(move_assign_source){};
  move_assigned           = std::move(move_assign_source);
  CHECK(move_assigned.first.error_ref == &move_assigned.m_error);
  CHECK(move_assigned.last.error_ref == &move_assigned.m_error);
  CHECK(move_assigned.error() == initial_error);
  CHECK(move_assign_source.first.error_ref == &move_assign_source.m_error);
  CHECK(move_assign_source.last.error_ref == &move_assign_source.m_error);
}

TEST_CASE("review: copied and moved ranges receive later iteration errors", "[iteration][error]") {
  auto ctx = pcrepp::context<>::create(R"(a|((b+)+c))").value();
  ctx.set_depth_limit(10u);
  auto const input = "a" + std::string(20uz, 'b') + 'x';
  auto       base  = ctx.find_all(input);
  REQUIRE(static_cast<bool>(base));

  auto copied = base;

  auto move_ctor_source = base;
  auto moved            = std::move(move_ctor_source);

  auto copy_assigned = decltype(base){};
  copy_assigned      = base;

  auto move_assign_source = base;
  auto move_assigned      = decltype(base){};
  move_assigned           = std::move(move_assign_source);

  auto  self_moved = base;
  auto* self_alias = &self_moved;
  self_moved       = std::move(*self_alias);

  auto const verify_later_error = [](auto& range) {
    CHECK(range.first.error_ref == &range.m_error);
    CHECK(range.last.error_ref == &range.m_error);
    auto it = range.begin();
    REQUIRE(it != range.end());
    CHECK((*it)[0] == "a");
    ++it;
    CHECK(it == range.end());
    CHECK_FALSE(static_cast<bool>(range));
    CHECK(range.error().find("code " + std::to_string(PCRE2_ERROR_DEPTHLIMIT)) != std::string_view::npos);
  };

  verify_later_error(copied);
  verify_later_error(moved);
  verify_later_error(copy_assigned);
  verify_later_error(move_assigned);
  verify_later_error(self_moved);
}

TEST_CASE("review: custom JIT flags propagate through find_all range", "[review][jitflags]") {
  auto constexpr jit_flags = PCRE2_JIT_COMPLETE | PCRE2_JIT_PARTIAL_SOFT;
  using context_type       = pcrepp::context<true, jit_flags>;
  auto const ctx           = context_type::create(R"(\w+)").value();
  auto       range         = ctx.find_all("aa bb");

  static_assert(std::same_as<decltype(range), pcrepp::match_range<true, jit_flags>>);
  static_assert(std::same_as<decltype(range.begin()), pcrepp::iterator<true, jit_flags>>);

  auto matches = std::vector<std::string_view>{};
  for (auto const& result : range) {
    matches.push_back(result[0]);
  }
  CHECK(matches == std::vector<std::string_view>{"aa", "bb"});
}

TEST_CASE("review: NTTP capture parser handles extended comments and POSIX classes", "[review][capture_count][nttp_replace]") {
  using pcrepp::detail::count_capture_groups;

  CHECK(count_capture_groups("(?x)# (ignored)\n(a)") == 1uz);
  CHECK(count_capture_groups("(?x:# (ignored)\n(a))") == 1uz);
  CHECK(count_capture_groups(R"([[:alpha:](a)])") == 0uz);
  CHECK(count_capture_groups(R"((?n)(a)(b))") == 0uz);
  CHECK(count_capture_groups(R"((?n)(?<name>a)(b))") == 1uz);
  CHECK(count_capture_groups(R"((?n:(a)(b))") == 0uz);
  CHECK(count_capture_groups(R"((?n:(?<name>a)))") == 1uz);

  auto deep = std::string{};
  deep.reserve(2048uz);
  for (auto const i : std::views::iota(0, 50)) {
    (void)i;
    deep += "(?:";
  }
  deep += "(?n:(a))";
  for (auto const i : std::views::iota(0, 50)) {
    (void)i;
    deep += ')';
  }
  auto deep_context = pcrepp::context<false>::create(deep);
  REQUIRE(deep_context);
  CHECK(count_capture_groups(deep) == deep_context->capture_count());

  auto const inline_fast   = pcrepp::replace<R"((?x)# (ignored)
(a))",
                                             "$1">("a");
  auto const inline_normal = pcrepp::replace<R"((?x)# (ignored)
(a))">("a", "$1");
  REQUIRE(inline_fast);
  REQUIRE(inline_normal);
  CHECK(*inline_fast == *inline_normal);
  CHECK(*inline_fast == "a");

  auto const scoped_fast   = pcrepp::replace<R"((?x:# (ignored)
(a)))",
                                             "$1">("a");
  auto const scoped_normal = pcrepp::replace<R"((?x:# (ignored)
(a)))">("a", "$1");
  REQUIRE(scoped_fast);
  REQUIRE(scoped_normal);
  CHECK(*scoped_fast == *scoped_normal);
  CHECK(*scoped_fast == "a");
}

TEST_CASE("review: NTTP fast replacement matches PCRE2 for POSIX class parentheses", "[review][nttp_replace][fast]") {
  auto const normal = pcrepp::replace<R"([[:alpha:](a)])">("x", "$1");
  auto const fast   = pcrepp::replace<R"([[:alpha:](a)])", "$1">("x");

  CHECK(fast.has_value() == normal.has_value());
  if (fast.has_value() && normal.has_value()) {
    CHECK(*fast == *normal);
  }
  CHECK_FALSE(normal);
  if (not fast.has_value() && not normal.has_value()) {
    CHECK(fast.error() == normal.error());
  }
}

TEST_CASE("review: NTTP replacement reference overflow falls back to PCRE2", "[review][nttp_replace][overflow]") {
  auto const normal_overflow = pcrepp::replace<R"((a))">("a", "$18446744073709551616");
  auto const fast_overflow   = pcrepp::replace<R"((a))", "$18446744073709551616">("a");

  CHECK(fast_overflow.has_value() == normal_overflow.has_value());
  if (fast_overflow.has_value() && normal_overflow.has_value()) {
    CHECK(*fast_overflow == *normal_overflow);
  }
  CHECK_FALSE(normal_overflow);
  if (not fast_overflow.has_value() && not normal_overflow.has_value()) {
    CHECK(fast_overflow.error() == normal_overflow.error());
  }

  auto const verify_size_max = [] {
    if constexpr (std::numeric_limits<size_t>::digits == 64) {
      auto const normal = pcrepp::replace<R"((a))">("a", "$18446744073709551615");
      auto const fast   = pcrepp::replace<R"((a))", "$18446744073709551615">("a");
      CHECK(fast.has_value() == normal.has_value());
      if (fast.has_value() && normal.has_value()) {
        CHECK(*fast == *normal);
      }
      CHECK_FALSE(normal);
      if (not fast.has_value() && not normal.has_value()) {
        CHECK(fast.error() == normal.error());
      }
    } else {
      auto const normal = pcrepp::replace<R"((a))">("a", "$4294967295");
      auto const fast   = pcrepp::replace<R"((a))", "$4294967295">("a");
      CHECK(fast.has_value() == normal.has_value());
      if (fast.has_value() && normal.has_value()) {
        CHECK(*fast == *normal);
      }
      CHECK_FALSE(normal);
      if (not fast.has_value() && not normal.has_value()) {
        CHECK(fast.error() == normal.error());
      }
    }
  };
  verify_size_max();
}

TEST_CASE("review: NTTP fast replacement preserves fallback errors", "[review][nttp_replace][fast]") {
  SECTION("malformed reference") {
    auto const normal = pcrepp::replace<R"((a))">("a", "X$");
    auto const fast   = pcrepp::replace<R"((a))", "X$">("a");
    CHECK(fast.has_value() == normal.has_value());
    if (fast.has_value() && normal.has_value()) {
      CHECK(*fast == *normal);
    }
    CHECK_FALSE(normal);
    if (not fast.has_value() && not normal.has_value()) {
      CHECK(fast.error() == normal.error());
    }
  }

  SECTION("unset group") {
    auto const normal = pcrepp::replace<R"((x)?y)">("y", "<$1>");
    auto const fast   = pcrepp::replace<R"((x)?y)", "<$1>">("y");
    CHECK(fast.has_value() == normal.has_value());
    if (fast.has_value() && normal.has_value()) {
      CHECK(*fast == *normal);
    }
    CHECK_FALSE(normal);
    if (not fast.has_value() && not normal.has_value()) {
      CHECK(fast.error() == normal.error());
    }
  }

  SECTION("unknown name") {
    auto const normal = pcrepp::replace<R"((?<known>a))">("a", "$missing");
    auto const fast   = pcrepp::replace<R"((?<known>a))", "$missing">("a");
    CHECK(fast.has_value() == normal.has_value());
    if (fast.has_value() && normal.has_value()) {
      CHECK(*fast == *normal);
    }
    CHECK_FALSE(normal);
    if (not fast.has_value() && not normal.has_value()) {
      CHECK(fast.error() == normal.error());
    }
  }

  SECTION("out-of-range number") {
    auto const normal = pcrepp::replace<R"((a))">("a", "$2");
    auto const fast   = pcrepp::replace<R"((a))", "$2">("a");
    CHECK(fast.has_value() == normal.has_value());
    if (fast.has_value() && normal.has_value()) {
      CHECK(*fast == *normal);
    }
    CHECK_FALSE(normal);
    if (not fast.has_value() && not normal.has_value()) {
      CHECK(fast.error() == normal.error());
    }
  }

  SECTION("compile-time capture shape mismatch") {
    auto const normal = pcrepp::replace<R"((?n)(a))">("a", "$1");
    auto const fast   = pcrepp::replace<R"((?n)(a))", "$1">("a");
    CHECK(fast.has_value() == normal.has_value());
    if (fast.has_value() && normal.has_value()) {
      CHECK(*fast == *normal);
    }
    CHECK_FALSE(normal);
    if (not fast.has_value() && not normal.has_value()) {
      CHECK(fast.error() == normal.error());
    }
  }
}

TEST_CASE("review: NTTP find handles no-auto-capture patterns", "[review][nttp_find][capture_count]") {
  auto const unnamed = pcrepp::find<R"((?n)(a))">("a");
  REQUIRE(unnamed);
  CHECK(unnamed->matched);
  CHECK(unnamed->groups.size() == 1uz);

  auto const named = pcrepp::find<R"((?n)(?<name>a)(b))">("ab");
  REQUIRE(named);
  CHECK(named->matched);
  CHECK(named->get<1>() == "ab");
  CHECK(named->get<2>() == "a");
}
