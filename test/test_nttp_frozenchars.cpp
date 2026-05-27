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
  for (auto const& [whole, digits] : pcrepp::find_all<kRaw>("a1 bb22 ccc333")) {
    if (whole.empty()) {
      continue;
    }
    hits.emplace_back(digits);
  }

  REQUIRE(hits.size() == 3);
  CHECK(hits[0] == "1");
  CHECK(hits[1] == "22");
  CHECK(hits[2] == "333");
}
