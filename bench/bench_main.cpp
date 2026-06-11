#include "pcrepp.hpp"
#include <pcre2.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace {

// ===================================================================
//  Data generation with fixed seed for reproducibility
// ===================================================================
//
// generate_text: English + mixed ASCII numeric patterns
// generate_japanese_text: Japanese UTF-8 text with embedded numbers
// ===================================================================

auto generate_text(size_t size) -> std::string {
  static auto rng = std::mt19937{42};

  static auto constexpr words = std::to_array<std::string_view>({
    "the", "quick", "brown", "fox", "jumps", "over", "lazy", "dog",
    "Lorem", "ipsum", "dolor", "sit", "amet", "consectetur",
    "adipiscing", "elit", "sed", "do", "eiusmod", "tempor",
    "incididunt", "ut", "labore", "et", "dolore", "magna", "aliqua",
    "Ut", "enim", "ad", "minim", "veniam", "quis", "nostrud",
    "exercitation", "ullamco", "laboris", "nisi", "ut", "aliquip",
    "ex", "ea", "commodo", "consequat", "Duis", "aute", "irure",
    "dolor", "in", "reprehenderit", "in", "voluptate", "velit",
    "esse", "cillum", "dolore", "eu", "fugiat", "nulla", "pariatur",
    "Excepteur", "sint", "occaecat", "cupidatat", "non", "proident",
    "sunt", "in", "culpa", "qui", "officia", "deserunt", "mollit",
    "anim", "id", "est", "laborum",
    "test@example.com", "user@domain.co.jp",
    "100yen", "price:500", "id:42",
  });

  std::string result;
  result.reserve(size);
  auto word_dist = std::uniform_int_distribution<size_t>{0, words.size() - 1};
  auto punct_dist = std::uniform_int_distribution<int>{0, 19};

  while (result.size() < size) {
    result += words[word_dist(rng)];
    auto const p = punct_dist(rng);
    if (p == 0)
      result += ".\n";
    else if (p == 1)
      result += ",\n";
    else
      result += ' ';
  }
  result.resize(size);
  return result;
}

auto generate_japanese_text(size_t size) -> std::string {
  static auto rng = std::mt19937{42};

  static auto constexpr words = std::to_array<std::string_view>({
    "東京", "大阪", "名古屋", "福岡", "札幌",
    "価格", "料金", "数量", "在庫", "注文",
    "100円", "500円", "1000円", "3000円",
    "りんご", "みかん", "ぶどう", "いちご",
    "メール", "注文", "発送", "返品",
    "user@example.jp",
    "12345-6789", "03-1234-5678",
  });

  std::string result;
  result.reserve(size);
  auto word_dist = std::uniform_int_distribution<size_t>{0, words.size() - 1};
  auto punct_dist = std::uniform_int_distribution<int>{0, 19};

  while (result.size() < size) {
    result += words[word_dist(rng)];
    auto const p = punct_dist(rng);
    if (p == 0)
      result += "。\n";
    else if (p == 1)
      result += "、\n";
    else
      result += ' ';
  }
  result.resize(size);
  return result;
}

// ===================================================================
//  Patterns and data sizes
// ===================================================================

struct PatternInfo {
  std::string_view name;
  std::string_view expr;
};

auto constexpr patterns = std::to_array<PatternInfo>({
  {"literal", "the"},
  {"word",    "\\b\\w+\\b"},
  {"email",   "[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\\.[A-Za-z]{2,}"},
  {"yen",     "(\\d+)円"},
  {"jword",   "[\\w\\W]+"},
  {"jany",    "."},
});

struct DataInfo {
  std::string_view label;
  size_t bytes;
};

auto constexpr data_sizes = std::to_array<DataInfo>({
  {"1KB",   1024},
  {"100KB", 102400},
  {"1MB",   1048576},
});

auto constexpr api_names = std::to_array<std::string_view>({
  "pcrepp (JIT)",
  "pcrepp (no JIT)",
  "raw PCRE2 (JIT)",
  "raw PCRE2 (no JIT)",
});

// ===================================================================
//  RAII helpers for raw PCRE2
// ===================================================================

struct RawCode {
  pcre2_code* code = nullptr;

  RawCode(std::string_view pattern, bool use_jit) {
    int errcode;
    PCRE2_SIZE erroffset;
    code = pcre2_compile(
      reinterpret_cast<PCRE2_SPTR>(pattern.data()),
      pattern.size(), 0, &errcode, &erroffset, nullptr);
    if (use_jit && code)
      pcre2_jit_compile(code, PCRE2_JIT_COMPLETE);
  }

  ~RawCode() {
    if (code) pcre2_code_free(code);
  }

  RawCode(RawCode const&) = delete;
  auto operator=(RawCode const&) = delete;
  RawCode(RawCode&& other) noexcept : code(std::exchange(other.code, nullptr)) {}
  auto operator=(RawCode&& other) noexcept -> RawCode& {
    if (this != &other) {
      if (code) pcre2_code_free(code);
      code = std::exchange(other.code, nullptr);
    }
    return *this;
  }
};

RawCode make_raw_code(std::string_view pattern, bool use_jit) {
  return RawCode{pattern, use_jit};
}

struct RawMatchData {
  pcre2_match_data* md = nullptr;

  explicit RawMatchData(pcre2_code* code) {
    md = pcre2_match_data_create_from_pattern(code, nullptr);
  }

  ~RawMatchData() {
    if (md) pcre2_match_data_free(md);
  }

  RawMatchData(RawMatchData const&) = delete;
  auto operator=(RawMatchData const&) = delete;
  RawMatchData(RawMatchData&& other) noexcept : md(std::exchange(other.md, nullptr)) {}
  auto operator=(RawMatchData&& other) noexcept -> RawMatchData& {
    if (this != &other) {
      if (md) pcre2_match_data_free(md);
      md = std::exchange(other.md, nullptr);
    }
    return *this;
  }
};

// ===================================================================
//  Benchmark iteration counts
// ===================================================================

auto base_iters(size_t data_size) -> int {
  if (data_size >= 1048576) return 10;
  if (data_size >= 102400)  return 100;
  return 1000;
}

// ===================================================================
//  Measurement helper
// ===================================================================

template <typename F>
auto measure(int iterations, F&& f) -> double {
  f();
  auto const start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < iterations; ++i)
    f();
  auto const end = std::chrono::high_resolution_clock::now();
  return std::chrono::duration<double, std::micro>(end - start).count()
         / static_cast<double>(iterations);
}

// ===================================================================
//  Results
// ===================================================================

struct BenchEntry {
  std::string_view op;
  std::string_view pattern;
  std::string_view data_size;
  std::string_view api;
  double us;
};

auto entries = std::vector<BenchEntry>{};

void run_and_record(
  std::string_view op,
  std::string_view pat,
  std::string_view dsize,
  std::string_view api,
  int iters,
  auto&& fn
) {
  auto us = measure(iters, std::forward<decltype(fn)>(fn));
  entries.push_back({op, pat, dsize, api, us});
}

// ===================================================================
//  Output
// ===================================================================

void print_table(std::string_view heading, std::string_view op_name) {
  std::cout << "\n### " << heading << "\n\n";

  auto const col_width = 15;
  auto const col_label = [&](auto const& s) {
    auto const w = static_cast<int>(s.size());
    auto const pad = (col_width - w) / 2;
    if (pad <= 0) return std::string{s};
    return std::string(pad, ' ') + std::string{s} + std::string(col_width - w - pad, ' ');
  };

  // Header
  std::cout << "| " << std::setw(10) << std::left << "Pattern"
            << " | " << std::setw(8) << std::left << "Size";
  for (auto const& api : api_names)
    std::cout << " | " << col_label(api);
  std::cout << " |\n";

  // Separator
  std::cout << "|" << std::string(12, '-') << "|" << std::string(10, '-');
  for (auto i = 0u; i < api_names.size(); ++i)
    std::cout << "|" << std::string(col_width + 2, '-');
  std::cout << "|\n";

  // Data rows
  for (auto const& pat : patterns) {
    for (auto const& ds : data_sizes) {
      std::cout << "| " << std::setw(10) << std::left << pat.name
                << " | " << std::setw(8) << std::left << ds.label;
      for (auto const& api : api_names) {
        auto it = std::ranges::find_if(entries, [&](auto const& e) {
          return e.op == op_name && e.pattern == pat.name
              && e.data_size == ds.label && e.api == api;
        });
        if (it != entries.end()) {
          auto const us_str = std::to_string(it->us);
          auto const dot = us_str.find('.');
          auto const int_part = us_str.substr(0, dot);
          auto const frac_part = dot != std::string::npos ? us_str.substr(dot + 1, 2) : "00";
          auto const formatted = int_part + "." + frac_part + " us";
          std::cout << " | " << std::setw(col_width) << std::right << formatted;
        } else {
          std::cout << " | " << std::setw(col_width) << std::right << "-";
        }
      }
      std::cout << " |\n";
    }
  }
  std::cout << "\n";
}

} // anonymous namespace

auto main() -> int {
  std::cout << std::fixed << std::setprecision(2);

  auto const now = std::chrono::system_clock::now();
  auto const tt = std::chrono::system_clock::to_time_t(now);
  std::cout << "# Benchmark: pcrepp vs PCRE2\n\n";
  std::cout << "Date: " << std::put_time(std::gmtime(&tt), "%Y-%m-%d") << "\n";
  std::cout << "Seed: 42 (fixed)\n\n";

  // --- Generate data ------------------------------------------------
  std::cout << "## Data\n\n";
  auto datas = std::vector<std::pair<std::string_view, std::string>>{};
  for (auto const& [label, bytes] : data_sizes) {
    datas.emplace_back(label, generate_text(bytes));
    std::cout << "- " << label << " (English) (" << bytes << " bytes)\n";
  }
  for (auto const& [label, bytes] : data_sizes) {
    auto const jlabel = std::string{label} + "(ja)";
    datas.emplace_back(jlabel, generate_japanese_text(bytes));
    std::cout << "- " << jlabel << " (Japanese) (" << bytes << " bytes)\n";
  }

  // --- Run benchmarks ------------------------------------------------
  for (auto const& [data_label, data] : datas) {
    for (auto const& [pat_name, pattern] : patterns) {
      // Pre-compile for match/replace benchmarks
      auto ctx_jit      = pcrepp::context<true>{pattern};
      auto ctx_nojit    = pcrepp::context<false>{pattern};
      auto raw_code_jit = RawCode{pattern, true};
      auto raw_code_nojit = RawCode{pattern, false};

      // Iteration counts
      auto const b = base_iters(data.size());
      auto const n_compile    = std::max(100, b * 10);
      auto const n_first      = std::max(1, b);
      auto const n_find_all   = std::max(1, b / 2);
      auto const n_replace    = std::max(1, b / 2);

      // --- compile --------------------------------------------------
      run_and_record("compile", pat_name, data_label, "pcrepp (JIT)", n_compile, [&] {
        [[maybe_unused]] auto c = pcrepp::context<true>{pattern};
      });
      run_and_record("compile", pat_name, data_label, "pcrepp (no JIT)", n_compile, [&] {
        [[maybe_unused]] auto c = pcrepp::context<false>{pattern};
      });
      run_and_record("compile", pat_name, data_label, "raw PCRE2 (JIT)", n_compile, [&] {
        [[maybe_unused]] auto c = RawCode{pattern, true};
      });
      run_and_record("compile", pat_name, data_label, "raw PCRE2 (no JIT)", n_compile, [&] {
        [[maybe_unused]] auto c = RawCode{pattern, false};
      });

      // --- first_match ----------------------------------------------
      run_and_record("first_match", pat_name, data_label, "pcrepp (JIT)", n_first, [&] {
        [[maybe_unused]] auto mr = pcrepp::match_result{ctx_jit};
        [[maybe_unused]] auto rc = ctx_jit.find(data, mr);
      });
      run_and_record("first_match", pat_name, data_label, "pcrepp (no JIT)", n_first, [&] {
        [[maybe_unused]] auto mr = pcrepp::match_result{ctx_nojit};
        [[maybe_unused]] auto rc = ctx_nojit.find(data, mr);
      });
      run_and_record("first_match", pat_name, data_label, "raw PCRE2 (JIT)", n_first, [&] {
        auto md = RawMatchData{raw_code_jit.code};
        [[maybe_unused]] auto rc = pcre2_match(
          raw_code_jit.code,
          reinterpret_cast<PCRE2_SPTR>(data.data()), data.size(),
          0, 0, md.md, nullptr);
      });
      run_and_record("first_match", pat_name, data_label, "raw PCRE2 (no JIT)", n_first, [&] {
        auto md = RawMatchData{raw_code_nojit.code};
        [[maybe_unused]] auto rc = pcre2_match(
          raw_code_nojit.code,
          reinterpret_cast<PCRE2_SPTR>(data.data()), data.size(),
          0, 0, md.md, nullptr);
      });

      // --- find_all -------------------------------------------------
      run_and_record("find_all", pat_name, data_label, "pcrepp (JIT)", n_find_all, [&] {
        for ([[maybe_unused]] auto const& m : ctx_jit.find_all(data)) {}
      });
      run_and_record("find_all", pat_name, data_label, "pcrepp (no JIT)", n_find_all, [&] {
        for ([[maybe_unused]] auto const& m : ctx_nojit.find_all(data)) {}
      });
      run_and_record("find_all", pat_name, data_label, "raw PCRE2 (JIT)", n_find_all, [&] {
        auto md = RawMatchData{raw_code_jit.code};
        PCRE2_SIZE start = 0;
        while (start < data.size()) {
          auto rc = pcre2_match(
            raw_code_jit.code,
            reinterpret_cast<PCRE2_SPTR>(data.data()), data.size(),
            start, 0, md.md, nullptr);
          if (rc < 0) break;
          auto* ovector = pcre2_get_ovector_pointer(md.md);
          start = ovector[1];
          if (ovector[0] == ovector[1])
            ++start;
        }
      });
      run_and_record("find_all", pat_name, data_label, "raw PCRE2 (no JIT)", n_find_all, [&] {
        auto md = RawMatchData{raw_code_nojit.code};
        PCRE2_SIZE start = 0;
        while (start < data.size()) {
          auto rc = pcre2_match(
            raw_code_nojit.code,
            reinterpret_cast<PCRE2_SPTR>(data.data()), data.size(),
            start, 0, md.md, nullptr);
          if (rc < 0) break;
          auto* ovector = pcre2_get_ovector_pointer(md.md);
          start = ovector[1];
          if (ovector[0] == ovector[1])
            ++start;
        }
      });

      // --- replace --------------------------------------------------
      run_and_record("replace", pat_name, data_label, "pcrepp (JIT)", n_replace, [&] {
        [[maybe_unused]] auto rlt = ctx_jit.replace(data, "X");
      });
      run_and_record("replace", pat_name, data_label, "pcrepp (no JIT)", n_replace, [&] {
        [[maybe_unused]] auto rlt = ctx_nojit.replace(data, "X");
      });
      run_and_record("replace", pat_name, data_label, "raw PCRE2 (JIT)", n_replace, [&] {
        auto md = RawMatchData{raw_code_jit.code};
        auto output = std::vector<PCRE2_UCHAR>(data.size());
        PCRE2_SIZE outlen = output.size();
        [[maybe_unused]] auto rc = pcre2_substitute(
          raw_code_jit.code,
          reinterpret_cast<PCRE2_SPTR>(data.data()), data.size(),
          0, PCRE2_SUBSTITUTE_GLOBAL, md.md, nullptr,
          reinterpret_cast<PCRE2_SPTR>("X"), 1,
          output.data(), &outlen);
      });
      run_and_record("replace", pat_name, data_label, "raw PCRE2 (no JIT)", n_replace, [&] {
        auto md = RawMatchData{raw_code_nojit.code};
        auto output = std::vector<PCRE2_UCHAR>(data.size());
        PCRE2_SIZE outlen = output.size();
        [[maybe_unused]] auto rc = pcre2_substitute(
          raw_code_nojit.code,
          reinterpret_cast<PCRE2_SPTR>(data.data()), data.size(),
          0, PCRE2_SUBSTITUTE_GLOBAL, md.md, nullptr,
          reinterpret_cast<PCRE2_SPTR>("X"), 1,
          output.data(), &outlen);
      });
    }
  }

  // --- Print results ------------------------------------------------
  print_table("Compile", "compile");
  print_table("First Match", "first_match");
  print_table("Find All", "find_all");
  print_table("Replace", "replace");

  return 0;
}
