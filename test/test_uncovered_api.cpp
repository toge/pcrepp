#include "catch2/catch_all.hpp"
#include "pcrepp.hpp"
#include <algorithm>
#include <string>
#include <vector>

// split_view (std::generator 版) — README 記載の公開 API
#if PCREPP_HAS_GENERATOR
TEST_CASE("split_view yields same tokens as split", "[split_view]") {
  using namespace std::string_view_literals;
  auto const ctx = pcrepp::context<>::create(R"(,\s*)").value();
  auto const target = "apple, banana, cherry"sv;

  auto tokens = std::vector<std::string_view>{};
  for (auto const token : ctx.split_view(target)) {
    tokens.push_back(token);
  }
  REQUIRE(tokens.size() == 3);
  CHECK(tokens[0] == "apple");
  CHECK(tokens[1] == "banana");
  CHECK(tokens[2] == "cherry");
}

TEST_CASE("split_view with no delimiter yields whole target", "[split_view]") {
  using namespace std::string_view_literals;
  auto const ctx = pcrepp::context<>::create(",").value();
  auto tokens = std::vector<std::string_view>{};
  for (auto const token : ctx.split_view("solo"sv)) {
    tokens.push_back(token);
  }
  REQUIRE(tokens.size() == 1);
  CHECK(tokens[0] == "solo");
}

TEST_CASE("split_view keeps empty tokens between adjacent delimiters", "[split_view]") {
  using namespace std::string_view_literals;
  auto const ctx = pcrepp::context<>::create(",").value();
  auto tokens = std::vector<std::string_view>{};
  for (auto const token : ctx.split_view("a,,b,"sv)) {
    tokens.push_back(token);
  }
  REQUIRE(tokens.size() == 4);
  CHECK(tokens[0] == "a");
  CHECK(tokens[1].empty());
  CHECK(tokens[2] == "b");
  CHECK(tokens[3].empty());
}
#endif

// replace_unchecked — throw 版置換
TEST_CASE("replace_unchecked substitutes via callback", "[replace_unchecked]") {
  auto const ctx = pcrepp::context<>::create(R"((\d+))").value();
  auto const result = ctx.replace_unchecked("a1 b22 c333", [](auto const& mr) {
    return std::format("<{}>", std::string{mr.get(1uz)}.size());
  });
  CHECK(result == "a<1> b<2> c<3>");
}

// _re リテラル演算子
TEST_CASE("_re literal produces working NTTP regex", "[nttp_find][literal]") {
  using namespace pcrepp;
  auto const [matched, whole, num] = R"((\d+))"_re.find("id=42").value();
  REQUIRE(matched);
  CHECK(whole == "42");
  CHECK(num == "42");
}
