/**
 * @file bench_compare.cpp
 * @brief CTRE delegation vs PCRE2 raw performance comparison
 *
 * Compares three execution paths for the same patterns:
 *   1. pcrepp::find<Pattern>()        — NTTP, may delegate to CTRE
 *   2. pcrepp::context<true>::find()  — runtime PCRE2 with JIT
 *   3. pcrepp::context<false>::find() — runtime PCRE2 without JIT
 *
 * Build:
 *   cmake -B build -DBUILD_BENCH=ON ...
 *   cmake --build build --parallel
 *   ./build/bench/bench_main --benchmark_filter="BM_NttpFind|BM_JitFind|..."
 *
 * Key findings (2026-07-05):
 *   - Nested quantifiers ((\w+\s+)+): PCRE2 is 8-28% faster than CTRE.
 *     Delegation to CTRE was removed — PCRE2's auto-possessify and
 *     required-byte optimizations handle these efficiently.
 *   - Backreference patterns: always PCRE2 (CTRE cannot handle).
 *   - Adversarial patterns ((a|aa|aaa)+[b-z] on "aaaa...1"):
 *     PCRE2 noJIT: ~12ms  (exponential backtracking, 19513 partitions)
 *     CTRE (DFA):  ~12ns  (O(n), no backtracking)
 *     → CTRE is up to 973,000x faster on worst-case input.
 *     See BM_Adversarial_* / BM_DirectCtre_* benchmarks.
 *   - Variable-length lookbehind: PCRE2 cannot compile,
 *     CTRE 3.11.0 has GCC 16 compatibility issues — currently unsupported.
 */
#include <benchmark/benchmark.h>
#include "pcrepp.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#ifdef WITH_CTRE
#include <ctre.hpp>
#endif

namespace {

// ===================================================================
//  テキスト生成 — 全パターンがマッチするよう設計
// ===================================================================
auto generate_compare_text(size_t const size) -> std::string {
  static auto rng = std::mt19937{42};
  static auto constexpr words = std::to_array<std::string_view>({
    "the", "brown", "fox", "jumps", "over", "lazy", "dog",
    "42", "100", "500", "9999",
    "foo", "bar", "foo", "baz", "bar",
  });
  auto result = std::string{};
  result.reserve(size);
  auto word_dist = std::uniform_int_distribution<size_t>{0, words.size() - 1};
  while (result.size() < size) {
    result += words[word_dist(rng)];
    auto const p = static_cast<int>(rng()) % 5;
    if (p == 0)      result += ".\n";
    else if (p == 1) result += ",\n";
    else             result += ' ';
  }
  result.resize(size);
  return result;
}

auto const& get_text(int64_t const bytes) {
  static auto cache = std::unordered_map<int64_t, std::string>{};
  auto it = cache.find(bytes);
  if (it == cache.end()) {
    auto [ins, ok] = cache.emplace(bytes, generate_compare_text(static_cast<size_t>(bytes)));
    std::ignore = ok;
    return ins->second;
  }
  return it->second;
}

// ===================================================================
//  ユーティリティ
// ===================================================================
/// パターンから context を生成。失敗しても std::runtime_error を投げずに
/// 空の optional を返す — 可変長 lookbehind など PCRE2 非対応パターン対策。
template <bool JIT>
auto try_make_context(char const* pattern)
  -> std::optional<pcrepp::context<JIT>> {
  auto res = pcrepp::context<JIT>::create(pattern);
  if (not res) { return std::nullopt; }
  return std::move(*res);
}

// ===================================================================
//  ヘルパーマクロ: find (first match) ベンチマーク
// ===================================================================

#define DEFINE_NTTP_FIND_BENCH(name, pattern_str)                             \
  static void BM_NttpFind_##name(benchmark::State& state) {                  \
    auto const& text = get_text(state.range(0));                              \
    for (auto _ : state) {                                                    \
      auto res = pcrepp::find<pattern_str>(text);                             \
      benchmark::DoNotOptimize(res);                                          \
    }                                                                         \
    state.SetBytesProcessed(state.iterations() * state.range(0));             \
  }                                                                           \
  BENCHMARK(BM_NttpFind_##name)->Arg(1024)->Arg(102400);

#define DEFINE_JIT_FIND_BENCH(name, pattern_str)                              \
  static void BM_JitFind_##name(benchmark::State& state) {                   \
    auto ctx_opt = try_make_context<true>(pattern_str);                       \
    if (not ctx_opt) { state.SkipWithError("PCRE2 compile failed"); return; } \
    auto const ctx = std::move(*ctx_opt);                                     \
    auto const& text = get_text(state.range(0));                              \
    for (auto _ : state) {                                                    \
      auto res = ctx.find(text);                                              \
      benchmark::DoNotOptimize(res);                                          \
    }                                                                         \
    state.SetBytesProcessed(state.iterations() * state.range(0));             \
  }                                                                           \
  BENCHMARK(BM_JitFind_##name)->Arg(1024)->Arg(102400);

#define DEFINE_NoJitFind_BENCH(name, pattern_str)                             \
  static void BM_NoJitFind_##name(benchmark::State& state) {                 \
    auto ctx_opt = try_make_context<false>(pattern_str);                      \
    if (not ctx_opt) { state.SkipWithError("PCRE2 compile failed"); return; } \
    auto const ctx = std::move(*ctx_opt);                                     \
    auto const& text = get_text(state.range(0));                              \
    for (auto _ : state) {                                                    \
      auto res = ctx.find(text);                                              \
      benchmark::DoNotOptimize(res);                                          \
    }                                                                         \
    state.SetBytesProcessed(state.iterations() * state.range(0));             \
  }                                                                           \
  BENCHMARK(BM_NoJitFind_##name)->Arg(1024)->Arg(102400);

// ===================================================================
//  ヘルパーマクロ: find_all (all matches) ベンチマーク
// ===================================================================

#define DEFINE_NTTP_FINDALL_BENCH(name, pattern_str)                          \
  static void BM_NttpFindAll_##name(benchmark::State& state) {                \
    auto const& text = get_text(state.range(0));                              \
    for (auto _ : state) {                                                    \
      auto count = 0uz;                                                       \
      auto all_res = pcrepp::find_all<pattern_str>(text);                     \
      if (all_res) {                                                          \
        for ([[maybe_unused]] auto const& mr : *all_res) {                    \
          ++count;                                                              \
        }                                                                       \
      }                                                                         \
      benchmark::DoNotOptimize(count);                                        \
    }                                                                         \
    state.SetBytesProcessed(state.iterations() * state.range(0));             \
  }                                                                           \
  BENCHMARK(BM_NttpFindAll_##name)->Arg(1024)->Arg(102400);

#define DEFINE_JIT_FINDALL_BENCH(name, pattern_str)                           \
  static void BM_JitFindAll_##name(benchmark::State& state) {                \
    auto ctx_opt = try_make_context<true>(pattern_str);                       \
    if (not ctx_opt) { state.SkipWithError("PCRE2 compile failed"); return; } \
    auto const ctx = std::move(*ctx_opt);                                     \
    auto const& text = get_text(state.range(0));                              \
    for (auto _ : state) {                                                    \
      auto count = 0uz;                                                       \
      for ([[maybe_unused]] auto& mr : ctx.find_all(text)) { ++count; }      \
      benchmark::DoNotOptimize(count);                                        \
    }                                                                         \
    state.SetBytesProcessed(state.iterations() * state.range(0));             \
  }                                                                           \
  BENCHMARK(BM_JitFindAll_##name)->Arg(1024)->Arg(102400);

#define DEFINE_NoJitFindAll_BENCH(name, pattern_str)                          \
  static void BM_NoJitFindAll_##name(benchmark::State& state) {              \
    auto ctx_opt = try_make_context<false>(pattern_str);                      \
    if (not ctx_opt) { state.SkipWithError("PCRE2 compile failed"); return; } \
    auto const ctx = std::move(*ctx_opt);                                     \
    auto const& text = get_text(state.range(0));                              \
    for (auto _ : state) {                                                    \
      auto count = 0uz;                                                       \
      for ([[maybe_unused]] auto& mr : ctx.find_all(text)) { ++count; }      \
      benchmark::DoNotOptimize(count);                                        \
    }                                                                         \
    state.SetBytesProcessed(state.iterations() * state.range(0));             \
  }                                                                           \
  BENCHMARK(BM_NoJitFindAll_##name)->Arg(1024)->Arg(102400);

// ===================================================================
//  パターン 1: 単純リテラル — PCRE2 デフォルト (baseline)
//  グループ数: 0 → find_all のタプル要素数 = 1 (全体マッチのみ)
// ===================================================================
DEFINE_NTTP_FIND_BENCH(Literal, R"(the)")
DEFINE_JIT_FIND_BENCH(Literal, R"(the)")
DEFINE_NoJitFind_BENCH(Literal, R"(the)")

DEFINE_NTTP_FINDALL_BENCH(Literal, R"(the)")
DEFINE_JIT_FINDALL_BENCH(Literal, R"(the)")
DEFINE_NoJitFindAll_BENCH(Literal, R"(the)")

// ===================================================================
//  パターン 2: 単純クラス+量化子 — PCRE2 デフォルト (baseline)
//  グループ数: 0 → tuple = [whole]
// ===================================================================
DEFINE_NTTP_FIND_BENCH(Digits, R"(\d+)")
DEFINE_JIT_FIND_BENCH(Digits, R"(\d+)")
DEFINE_NoJitFind_BENCH(Digits, R"(\d+)")

DEFINE_NTTP_FINDALL_BENCH(Digits, R"(\d+)")
DEFINE_JIT_FINDALL_BENCH(Digits, R"(\d+)")
DEFINE_NoJitFindAll_BENCH(Digits, R"(\d+)")

// ===================================================================
//  パターン 3: ネスト量化子 ((\w+\s+)+) — CTRE 推奨 (PCRE2 が苦手)
//  グループ数: 1 → tuple = [whole, g1]
//  両エンジンともコンパイル可能 → CTRE vs PCRE2 の速度比較が可能
// ===================================================================
DEFINE_NTTP_FIND_BENCH(NestedQuant, R"((\w+\s+)+)")
DEFINE_JIT_FIND_BENCH(NestedQuant, R"((\w+\s+)+)")
DEFINE_NoJitFind_BENCH(NestedQuant, R"((\w+\s+)+)")

DEFINE_NTTP_FINDALL_BENCH(NestedQuant, R"((\w+\s+)+)")
DEFINE_JIT_FINDALL_BENCH(NestedQuant, R"((\w+\s+)+)")
DEFINE_NoJitFindAll_BENCH(NestedQuant, R"((\w+\s+)+)")

// ===================================================================
//  パターン 4: (省略) 可変長 lookbehind は CTRE v3.11.0 + GCC 16
//  でコンパイルエラーになるためテストをスキップする。
//  理論上は CTRE 推奨だが、現実的にはネスト量化子が主な受益者。
// ===================================================================

// ===================================================================
//  パターン 5: 後方参照 — PCRE2 強制 (CTRE 非対応)
//  CTRE フォールバックが ON でも PCRE2 を使う。
//  グループ数: 1 → tuple = [whole, g1]
// ===================================================================
DEFINE_NTTP_FIND_BENCH(Backref, R"((\w+)\s+\1)")
DEFINE_JIT_FIND_BENCH(Backref, R"((\w+)\s+\1)")
DEFINE_NoJitFind_BENCH(Backref, R"((\w+)\s+\1)")

DEFINE_NTTP_FINDALL_BENCH(Backref, R"((\w+)\s+\1)")
DEFINE_JIT_FINDALL_BENCH(Backref, R"((\w+)\s+\1)")
DEFINE_NoJitFindAll_BENCH(Backref, R"((\w+)\s+\1)")

// ===================================================================
//  コンパイル時間ベンチマーク (参考)
// ===================================================================
static void BM_Compile_JIT(benchmark::State& state) {
  for (auto _ : state) {
    auto ctx = pcrepp::context<true>{R"((\w+\s+)+)"};
    benchmark::DoNotOptimize(ctx);
  }
}
BENCHMARK(BM_Compile_JIT);

static void BM_Compile_NoJIT(benchmark::State& state) {
  for (auto _ : state) {
    auto ctx = pcrepp::context<false>{R"((\w+\s+)+)"};
    benchmark::DoNotOptimize(ctx);
  }
}
BENCHMARK(BM_Compile_NoJIT);

// ===================================================================
//  match (フルマッチ) でのバックトラッキング比較
// ===================================================================
//
// PCRE2 の最適化は強力だが、パターン末尾に固定リテラルがないと
// 要求バイト最適化が効かず、指数的バックトラッキングが発生する。
//
// パターン: (a|aa|aaa)+[b-z]
// テキスト: N個のa + "1"  (1 は [b-z] に含まれない → ノーマッチ)
//
// tribonacci T(N+1) 通りのパーティションを探索:
//   N=10 → T(11)=149 通り  32 us
//   N=15 → T(16)=1705 通り  589 us
//   N=20 → T(21)=19513 通り  13 ms (exponential!)
//   N=24 → match_limit 超過でエラー
//
// CTRE の DFA は N に対して O(N) で完了する。
// (注意: このパターンには ctre_recommended は false を返すため、
//  直接 CTRE 呼び出しで比較する)
// ===================================================================

auto const& get_adv_match_text() {
  static auto const text = std::string(20, 'a') + "1";
  return text;
}

auto const& get_quick_match_text() {
  static auto const text = std::string(20, 'a') + "z";
  return text;
}

// PCRE2 経路 (全経路でバックトラッキングの影響を受ける)
static void BM_Adversarial_NttpMatch(benchmark::State& state) {
  auto const& text = get_adv_match_text();
  auto const re = pcrepp::nttp_regex<"(a|aa|aaa)+[b-z]">{};
  for (auto _ : state) {
    auto res = re.match(text);
    benchmark::DoNotOptimize(res);
  }
}
BENCHMARK(BM_Adversarial_NttpMatch);

static void BM_Adversarial_JitMatch(benchmark::State& state) {
  auto const ctx = pcrepp::context<true>{"(a|aa|aaa)+[b-z]"};
  auto const& text = get_adv_match_text();
  for (auto _ : state) {
    auto res = ctx.match(text);
    benchmark::DoNotOptimize(res);
  }
}
BENCHMARK(BM_Adversarial_JitMatch);

static void BM_Adversarial_NoJitMatch(benchmark::State& state) {
  auto const ctx = pcrepp::context<false>{"(a|aa|aaa)+[b-z]"};
  auto const& text = get_adv_match_text();
  for (auto _ : state) {
    auto res = ctx.match(text);
    benchmark::DoNotOptimize(res);
  }
}
BENCHMARK(BM_Adversarial_NoJitMatch);

// Quick match: 即座に成功 (b で終わる → 貪欲マッチで一発成功)
static void BM_Quick_NttpMatch(benchmark::State& state) {
  auto const& text = get_quick_match_text();
  auto const re = pcrepp::nttp_regex<"(a|aa|aaa)+[b-z]">{};
  for (auto _ : state) {
    auto res = re.match(text);
    benchmark::DoNotOptimize(res);
  }
}
BENCHMARK(BM_Quick_NttpMatch);

static void BM_Quick_JitMatch(benchmark::State& state) {
  auto const ctx = pcrepp::context<true>{"(a|aa|aaa)+[b-z]"};
  auto const& text = get_quick_match_text();
  for (auto _ : state) {
    auto res = ctx.match(text);
    benchmark::DoNotOptimize(res);
  }
}
BENCHMARK(BM_Quick_JitMatch);

// ===================================================================
//  直接 CTRE (CTRE フォールバック有効時のみ)
// ===================================================================
#ifdef WITH_CTRE
static void BM_DirectCtre_AdvMatch(benchmark::State& state) {
  auto const& text = get_adv_match_text();
  constexpr auto cp = ctll::fixed_string{"(a|aa|aaa)+[b-z]"};
  for (auto _ : state) {
    auto m = ctre::match<cp>(text);
    benchmark::DoNotOptimize(m);
  }
}
BENCHMARK(BM_DirectCtre_AdvMatch);

static void BM_DirectCtre_QuickMatch(benchmark::State& state) {
  auto const& text = get_quick_match_text();
  constexpr auto cp = ctll::fixed_string{"(a|aa|aaa)+[b-z]"};
  for (auto _ : state) {
    auto m = ctre::match<cp>(text);
    benchmark::DoNotOptimize(m);
  }
}
BENCHMARK(BM_DirectCtre_QuickMatch);
#endif

} // namespace
