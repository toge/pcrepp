/**
 * @file bench_main.cpp
 * @brief Google Benchmark を使った pcrepp パフォーマンス計測
 *
 * 比較軸:
 * - pcrepp (JIT) vs pcrepp (NoJIT) vs raw PCRE2
 * - find / find_all / replace / compile
 * - NTTP find vs runtime find
 * - テキストサイズ: 1KB / 100KB / 1MB
 */
#include <benchmark/benchmark.h>
#include "pcrepp.hpp"
#include <pcre2.h>

#include <algorithm>
#include <array>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace {

// ===================================================================
//  データ生成
// ===================================================================

auto generate_text(size_t const size) -> std::string {
  static auto rng = std::mt19937{42};
  static auto constexpr words = std::to_array<std::string_view>({
    "the", "quick", "brown", "fox", "jumps", "over", "lazy", "dog",
    "Lorem", "ipsum", "dolor", "sit", "amet", "consectetur",
    "adipiscing", "elit", "sed", "do", "eiusmod", "tempor",
    "test@example.com", "user@domain.co.jp",
    "100yen", "price:500", "id:42",
  });
  auto result = std::string{};
  result.reserve(size);
  auto word_dist = std::uniform_int_distribution<size_t>{0, words.size() - 1};
  auto punct_dist = std::uniform_int_distribution<int>{0, 19};
  while (result.size() < size) {
    result += words[word_dist(rng)];
    auto const p = punct_dist(rng);
    if (p == 0)      result += ".\n";
    else if (p == 1) result += ",\n";
    else             result += ' ';
  }
  result.resize(size);
  return result;
}

auto generate_japanese_text(size_t const size) -> std::string {
  static auto rng = std::mt19937{42};
  static auto constexpr words = std::to_array<std::string_view>({
    "東京", "大阪", "名古屋", "価格", "料金",
    "100円", "500円", "1000円",
    "りんご", "みかん", "user@example.jp",
  });
  auto result = std::string{};
  result.reserve(size);
  auto word_dist = std::uniform_int_distribution<size_t>{0, words.size() - 1};
  auto punct_dist = std::uniform_int_distribution<int>{0, 9};
  while (result.size() < size) {
    result += words[word_dist(rng)];
    auto const p = punct_dist(rng);
    if (p == 0)      result += "。\n";
    else if (p == 1) result += "、\n";
    else             result += ' ';
  }
  result.resize(size);
  return result;
}

// キャッシュ済みテキストデータ
auto const& get_text(int64_t const bytes) {
  static auto cache = std::unordered_map<int64_t, std::string>{};
  auto it = cache.find(bytes);
  if (it == cache.end()) {
    auto [ins, ok] = cache.emplace(bytes, generate_text(static_cast<size_t>(bytes)));
    std::ignore = ok;
    return ins->second;
  }
  return it->second;
}

auto const& get_jp_text(int64_t const bytes) {
  static auto cache = std::unordered_map<int64_t, std::string>{};
  auto it = cache.find(bytes);
  if (it == cache.end()) {
    auto [ins, ok] = cache.emplace(bytes, generate_japanese_text(static_cast<size_t>(bytes)));
    std::ignore = ok;
    return ins->second;
  }
  return it->second;
}

// raw PCRE2 用 RAII ラッパ
struct RawCode {
  pcre2_code* code = nullptr;
  RawCode(std::string_view const pat, bool const jit) {
    auto ec = int{};
    auto eo = PCRE2_SIZE{};
    code = pcre2_compile(reinterpret_cast<PCRE2_SPTR8>(pat.data()), pat.size(),
                         0, &ec, &eo, nullptr);
    if (code && jit) { pcre2_jit_compile(code, PCRE2_JIT_COMPLETE); }
  }
  ~RawCode() { if (code) { pcre2_code_free(code); } }
};

struct RawMatchData {
  pcre2_match_data* md = nullptr;
  explicit RawMatchData(pcre2_code const* c) {
    md = pcre2_match_data_create_from_pattern(c, nullptr);
  }
  ~RawMatchData() { if (md) { pcre2_match_data_free(md); } }
};

// ===================================================================
//  BM_Compile — パターンコンパイルの速度
// ===================================================================
static void BM_Compile_JIT(benchmark::State& state) {
  for (auto _ : state) {
    auto ctx = pcrepp::context<true>{R"(\d+)"};
    benchmark::DoNotOptimize(ctx);
  }
}
BENCHMARK(BM_Compile_JIT);

static void BM_Compile_NoJIT(benchmark::State& state) {
  for (auto _ : state) {
    auto ctx = pcrepp::context<false>{R"(\d+)"};
    benchmark::DoNotOptimize(ctx);
  }
}
BENCHMARK(BM_Compile_NoJIT);

// ===================================================================
//  BM_Find — 最初のマッチ検索
// ===================================================================
static void BM_Find_JIT(benchmark::State& state) {
  auto const ctx = pcrepp::context<true>{R"(\d+)"};
  auto const& text = get_text(state.range(0));
  for (auto _ : state) {
    auto res = ctx.find(text);
    benchmark::DoNotOptimize(res);
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_Find_JIT)->Arg(1024)->Arg(102400)->Arg(1048576);

static void BM_Find_NoJIT(benchmark::State& state) {
  auto const ctx = pcrepp::context<false>{R"(\d+)"};
  auto const& text = get_text(state.range(0));
  for (auto _ : state) {
    auto res = ctx.find(text);
    benchmark::DoNotOptimize(res);
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_Find_NoJIT)->Arg(1024)->Arg(102400)->Arg(1048576);

static void BM_Find_RawPCRE2_JIT(benchmark::State& state) {
  auto const raw = RawCode{R"(\d+)", true};
  auto const& text = get_text(state.range(0));
  for (auto _ : state) {
    auto md = RawMatchData{raw.code};
    auto const rc = pcre2_jit_match(raw.code,
      reinterpret_cast<PCRE2_SPTR8>(text.data()), text.size(),
      0, 0, md.md, nullptr);
    benchmark::DoNotOptimize(rc);
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_Find_RawPCRE2_JIT)->Arg(1024)->Arg(102400)->Arg(1048576);

// ===================================================================
//  BM_FindAll — 全マッチ検索
// ===================================================================
static void BM_FindAll_JIT(benchmark::State& state) {
  auto const ctx = pcrepp::context<true>{R"(\d+)"};
  auto const& text = get_text(state.range(0));
  for (auto _ : state) {
    auto count = 0uz;
    for ([[maybe_unused]] auto& mr : ctx.find_all(text)) { ++count; }
    benchmark::DoNotOptimize(count);
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_FindAll_JIT)->Arg(1024)->Arg(102400);

static void BM_FindAll_NoJIT(benchmark::State& state) {
  auto const ctx = pcrepp::context<false>{R"(\d+)"};
  auto const& text = get_text(state.range(0));
  for (auto _ : state) {
    auto count = 0uz;
    for ([[maybe_unused]] auto& mr : ctx.find_all(text)) { ++count; }
    benchmark::DoNotOptimize(count);
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_FindAll_NoJIT)->Arg(1024)->Arg(102400);

static void BM_FindAll_RawPCRE2_JIT(benchmark::State& state) {
  auto const raw = RawCode{R"(\d+)", true};
  auto const& text = get_text(state.range(0));
  for (auto _ : state) {
    auto md = RawMatchData{raw.code};
    auto count = 0uz;
    auto start = PCRE2_SIZE{};
    while (start < text.size()) {
      auto const rc = pcre2_jit_match(raw.code,
        reinterpret_cast<PCRE2_SPTR8>(text.data()), text.size(),
        start, 0, md.md, nullptr);
      if (rc < 0) break;
      auto* ov = pcre2_get_ovector_pointer(md.md);
      start = ov[1];
      if (ov[0] == ov[1]) ++start;
      ++count;
    }
    benchmark::DoNotOptimize(count);
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_FindAll_RawPCRE2_JIT)->Arg(1024)->Arg(102400);

// ===================================================================
//  BM_Replace — 文字列置換
// ===================================================================
static void BM_Replace_JIT(benchmark::State& state) {
  auto const ctx = pcrepp::context<true>{R"(\d+)"};
  auto const& text = get_text(state.range(0));
  for (auto _ : state) {
    auto res = ctx.replace(text, "X");
    benchmark::DoNotOptimize(res);
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_Replace_JIT)->Arg(1024)->Arg(102400);

static void BM_Replace_NoJIT(benchmark::State& state) {
  auto const ctx = pcrepp::context<false>{R"(\d+)"};
  auto const& text = get_text(state.range(0));
  for (auto _ : state) {
    auto res = ctx.replace(text, "X");
    benchmark::DoNotOptimize(res);
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_Replace_NoJIT)->Arg(1024)->Arg(102400);

// ===================================================================
//  BM_NttpFind — NTTP find vs runtime find
// ===================================================================
static void BM_NttpFind(benchmark::State& state) {
  auto const& text = get_text(state.range(0));
  for (auto _ : state) {
    auto res = pcrepp::find<R"(\d+)">(text);
    benchmark::DoNotOptimize(res);
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_NttpFind)->Arg(1024)->Arg(102400);

// ===================================================================
//  BM_Japanese — 日本語テキストでの find_all
// ===================================================================
static void BM_Japanese_FindAll_JIT(benchmark::State& state) {
  auto const ctx = pcrepp::context<true>{R"((\d+)円)", PCRE2_UTF};
  auto const& text = get_jp_text(state.range(0));
  for (auto _ : state) {
    auto count = 0uz;
    for ([[maybe_unused]] auto& mr : ctx.find_all(text)) { ++count; }
    benchmark::DoNotOptimize(count);
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_Japanese_FindAll_JIT)->Arg(1024)->Arg(102400);

} // namespace
