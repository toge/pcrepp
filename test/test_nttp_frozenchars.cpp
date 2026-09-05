// frozenchars 非導入環境 (CI 等) ではコンパイルのみで 0 テスト
#if !PCREPP_TEST_HAS_FROZENCHARS

// 空ファイル — Catch2WithMain はリンク済みなのでシンボル衝突なし

#else  // PCREPP_TEST_HAS_FROZENCHARS

#include <vector>

#include "catch2/catch_all.hpp"
#include "frozenchars.hpp"
#define PCREPP_ENABLE_FROZENCHARS_NTTP_OVERLOADS 1
#include "pcrepp.hpp"

using namespace frozenchars::literals;
namespace fop = frozenchars::ops;

TEST_CASE("nttp find_all works with transformed FrozenString pattern", "[nttp][frozenchars]") {
  // join_lines により容量と実長がズレやすいパターンを作る
  static auto constexpr kRaw = R"re(
    (\d+)
  )re"_fs | frozenchars::ops::remove_leading_spaces
         | frozenchars::ops::remove_trailing_spaces
         | frozenchars::ops::join_lines;

  auto hits = std::vector<std::string>{};
  auto all_res = pcrepp::find_all<kRaw>("a1 bb22 ccc333");
  REQUIRE(all_res.has_value());
  for (auto const& mr : *all_res) {
    auto const whole = mr.get(0);
    if (whole.empty()) {
      continue;
    }
    hits.emplace_back(std::string{mr.get(1)});
  }

  REQUIRE(hits.size() == 3);
  CHECK(hits[0] == "1");
  CHECK(hits[1] == "22");
  CHECK(hits[2] == "333");
}

#endif  // PCREPP_TEST_HAS_FROZENCHARS
