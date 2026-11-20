#include "pcrepp.hpp"
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>

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
