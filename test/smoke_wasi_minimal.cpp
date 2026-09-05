/**
 * @file test/smoke_wasi_minimal.cpp
 * @brief 例外なしビルド (-fno-exceptions) の検証。
 *
 * Catch2 が使えない wasip1 CI 向けに、主要 API が例外なしで
 * コンパイル・実行できることを確認する。
 */
#include <cstdio>
#include <string>

#include "pcrepp.hpp"

namespace {

auto check(bool cond, char const* name) -> int {
  if (not cond) {
    std::printf("FAIL: %s\n", name);
    return 1;
  }
  return 0;
}

}  // namespace

int main() {
  auto fails = 0;

  auto ctx_res = pcrepp::context<>::create(R"((\d+)-(\w+))");
  fails += check(static_cast<bool>(ctx_res), "create");
  if (not ctx_res) {
    std::printf("SMOKE_NO_EXCEPTIONS_NG\n");
    return 1;
  }
  auto&      ctx    = *ctx_res;
  auto const target = std::string{"abc 123-xyz def"};

  auto fr = ctx.find(target, pcrepp::use_tls);
  fails += check(static_cast<bool>(fr) && static_cast<bool>(*fr), "find");
  if (fr && *fr) {
    fails += check(fr->get(1) == "123", "get(1)");
    fails += check(fr->get(2) == "xyz", "get(2)");
    fails += check(fr->get<int>(1) == 123, "get<int>");
  }

  auto count = 0;
  for ([[maybe_unused]] auto const& m : ctx.find_all(target)) {
    ++count;
  }
  fails += check(count == 1, "find_all");

  auto rep = ctx.replace(target, "[$1]");
  fails += check(static_cast<bool>(rep) && *rep == "abc [123] def", "replace");

  auto parts = ctx.split("a1b22c");
  fails += check(not parts.empty(), "split");

  auto full = ctx.match("123-xyz");
  fails += check(static_cast<bool>(full) && *full, "match");

  auto nttp = pcrepp::find<R"(\d+)">(target);
  fails += check(static_cast<bool>(nttp) && nttp->get<1>() == "123", "nttp find");

  auto fu = pcrepp::find_unchecked<R"(\d+)">(target);
  fails += check(static_cast<bool>(fu) && fu->get<1>() == "123", "nttp find_unchecked");

  auto nr = pcrepp::replace<R"(\d+)">(target, "#");
  fails += check(static_cast<bool>(nr), "nttp replace");

  auto ru = pcrepp::replace_unchecked<R"(\d+)">(target, "#");
  fails += check(static_cast<bool>(ru), "nttp replace_unchecked");

  fails += check(fr->operator[](-1).empty(), "mr[-1] empty");

  if (fails == 0) {
    std::printf("SMOKE_NO_EXCEPTIONS_OK\n");
    return 0;
  }
  std::printf("SMOKE_NO_EXCEPTIONS_NG\n");
  return 1;
}
