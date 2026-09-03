/**
 * @file test/smoke_wasi_minimal.cpp
 * @brief PCREPP_WASI_MINIMAL モードの検証。
 *
 * -fno-exceptions + PCREPP_WASI_MINIMAL 付きでビルドされる。
 * expected を返す主要 API (create/find/replace/match/split/NTTP版) が
 * 例外なしでコンパイル・実行できることを確認する。
 * 例外を投げる版 (unchecked 系・throw コンストラクタ) は対象外。
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
    std::printf("SMOKE_WASI_MINIMAL_NG\n");
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

  auto nr = pcrepp::replace<R"(\d+)">(target, "#");
  fails += check(static_cast<bool>(nr), "nttp replace");

  if (fails == 0) {
    std::printf("SMOKE_WASI_MINIMAL_OK\n");
    return 0;
  }
  std::printf("SMOKE_WASI_MINIMAL_NG\n");
  return 1;
}
