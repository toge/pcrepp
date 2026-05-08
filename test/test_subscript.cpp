#include "pcrepp.hpp"
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

namespace {
template <typename T>
concept has_index_get = requires(pcrepp::match_result const& mr) {
  { mr.template get<T>(0uz) } -> std::same_as<T>;
};

template <typename T>
concept has_named_get = requires(pcrepp::match_result const& mr) {
  { mr.template get<T>("value") } -> std::same_as<T>;
};

struct unsupported_type {};

static_assert(has_index_get<std::string_view>);
static_assert(has_index_get<std::string>);
static_assert(has_index_get<float>);
static_assert(has_index_get<double>);
static_assert(has_index_get<std::int8_t>);
static_assert(has_index_get<std::uint8_t>);
static_assert(has_index_get<std::int16_t>);
static_assert(has_index_get<std::uint16_t>);
static_assert(has_index_get<std::int32_t>);
static_assert(has_index_get<std::uint32_t>);
static_assert(has_index_get<std::int64_t>);
static_assert(has_index_get<std::uint64_t>);
static_assert(has_named_get<std::string_view>);
static_assert(not has_index_get<unsupported_type>);
}

TEST_CASE("match_result subscript operator", "[match_result]") {
  pcrepp::context ctx{"(\\w+):(\\d+)"};
  pcrepp::match_result mr{ctx};

  std::string_view target = "age:30";
  auto const rc = ctx.find(target, mr);
  REQUIRE(rc);
  REQUIRE(*rc > 0);

  SECTION("size_t subscript") {
    CHECK(mr[0uz] == "age:30");
    CHECK(mr[1uz] == "age");
    CHECK(mr[2uz] == "30");
  }

  SECTION("int subscript") {
    CHECK(mr[0] == "age:30");
    CHECK(mr[1] == "age");
    CHECK(mr[2] == "30");
  }

  SECTION("get returns string_view by default") {
    CHECK(mr.get(0uz) == "age:30");
    CHECK(mr.get(1uz) == "age");
    CHECK(mr.get(2uz) == "30");
  }

  SECTION("get converts to string and integer types") {
    CHECK(mr.get<std::string>(1uz) == "age");
    CHECK(mr.get<int>(2uz) == 30);
    CHECK(mr.get<unsigned int>(2uz) == 30u);
    CHECK(mr.get<long long>(2uz) == 30ll);
    CHECK(mr.get<std::uint8_t>(2uz) == static_cast<std::uint8_t>(30));
  }
}

TEST_CASE("match_result named subscript operator", "[match_result]") {
  pcrepp::context ctx{"(?<key>\\w+):(?<value>[+-]?(?:\\d+\\.\\d+|\\d+))"};
  pcrepp::match_result mr{ctx};

  std::string_view target = "height:180.5";
  auto const rc = ctx.find(target, mr);
  REQUIRE(rc);
  REQUIRE(*rc > 0);

  SECTION("string_view subscript") {
    CHECK(mr["key"] == "height");
    CHECK(mr["value"] == "180.5");
    CHECK(mr["nonexistent"] == "");
  }

  SECTION("string literal subscript") {
    CHECK(mr["key"] == "height");
    CHECK(mr["value"] == "180.5");
  }

  SECTION("get converts named groups") {
    CHECK(mr.get<std::string>("key") == "height");
    CHECK(mr.get<float>("value") == 180.5f);
    CHECK(mr.get<double>("value") == 180.5);
  }
}

TEST_CASE("match_result get returns default value on conversion failure", "[match_result]") {
  pcrepp::context ctx{"(?<value>[^:]+)"};
  pcrepp::match_result mr{ctx};

  std::string_view target = "abc";
  auto const rc = ctx.find(target, mr);
  REQUIRE(rc);
  REQUIRE(*rc > 0);

  CHECK(mr.get<int>("value") == 0);
  CHECK(mr.get<unsigned int>("value") == 0u);
  CHECK(mr.get<float>("value") == 0.0f);
  CHECK(mr.get<double>("value") == 0.0);
  CHECK(mr.get<std::string>("missing").empty());
  CHECK(mr.get("missing").empty());
}

TEST_CASE("match_result get requires full numeric parse", "[match_result]") {
  pcrepp::context ctx{"(?<value>[^:]+)"};
  pcrepp::match_result mr{ctx};

  std::string_view target = "123abc";
  auto const rc = ctx.find(target, mr);
  REQUIRE(rc);
  REQUIRE(*rc > 0);

  CHECK(mr.get<int>("value") == 0);
  CHECK(mr.get<float>("value") == 0.0f);
}

TEST_CASE("match_result get returns default for out-of-range integers", "[match_result]") {
  pcrepp::context ctx{"(?<value>[^:]+)"};
  pcrepp::match_result mr{ctx};

  std::string_view target = "999999999999999999999999";
  auto const rc = ctx.find(target, mr);
  REQUIRE(rc);
  REQUIRE(*rc > 0);

  CHECK(mr.get<int>("value") == 0);
  CHECK(mr.get<std::uint8_t>("value") == static_cast<std::uint8_t>(0));
}
