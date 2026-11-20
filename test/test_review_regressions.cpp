#include "pcrepp.hpp"
#include <catch2/catch_test_macros.hpp>
#include <optional>
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
