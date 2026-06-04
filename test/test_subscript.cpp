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

TEST_CASE("match_result named get works with non-null-terminated string_view", "[match_result][nul_safety]") {
  // 名前の直後に 'X' を配置し、string_view を名前部分だけ取ったときに
  // 旧実装だと 'valueX' まで読み込んでいた可能性がある。
  // 新実装では NUL 終端バッファにコピーしてから PCRE2 に渡すため安全。
  pcrepp::context ctx{"(?<value>[^:]+)"};
  pcrepp::match_result mr{ctx};

  std::string_view target = "abc";
  auto const rc = ctx.find(target, mr);
  REQUIRE(rc);
  REQUIRE(*rc > 0);

  // value 文字列を名前に持つバッファ + 直後に非 NUL 文字
  std::string buf = "valueXYZ";
  std::string_view name{buf.data(), 5uz};  // "value" (NUL 終端なし)

  CHECK(mr.get<int>(name) == 0);
  CHECK(mr.get<std::string>(name) == "abc");
  CHECK(name == "value");  // view の範囲が変わっていないこと
}

TEST_CASE("match_result named get works with long names (heap buffer path)", "[match_result][nul_safety]") {
  // 256 バイト超の名前でヒープ経路もカバー
  pcrepp::context ctx{"(?<longname>[^:]+)"};
  pcrepp::match_result mr{ctx};

  std::string_view target = "x";
  auto const rc = ctx.find(target, mr);
  REQUIRE(rc);
  REQUIRE(*rc > 0);

  // 256 文字超の名前の string_view
  std::string buf(300, 'a');
  // 後半に 'longname' を埋め込み、最初の文字から 300 バイト全部を view とする
  // → lookup_named_capture は size > 255 でヒープ経路を使う
  for (size_t i = 0; i < std::string_view{"longname"}.size(); ++i) {
    buf[100uz + i] = std::string_view{"longname"}[i];
  }
  // "longname" 自体は 8 文字なので、ヒープ経路に入るには 256 バイト超全体の view が必要
  // → ここでは name を buf 全体 (300 バイト) にして、ヒープ経路を踏ませる
  std::string_view name_long{buf.data() + 100uz, 8uz};  // 8 バイトは短い → SBO
  CHECK(mr.get<std::string>(name_long) == "x");

  // 本当にヒープ経路: 256 バイト以上の view
  std::string_view name_heap{buf.data(), 300uz};
  // 最初の 8 バイトが "aaaaaaaa" なので、その名前ではヒットしない → 空が返る
  CHECK(mr.get<std::string>(name_heap).empty());
}

TEST_CASE("match_result try_get distinguishes success and failure", "[match_result][try_get]") {
  pcrepp::context ctx{"(?<i>\\d+),(?<f>[+-]?\\d+\\.\\d+)"};
  pcrepp::match_result mr{ctx};

  std::string_view target = "42,3.14";
  auto const rc = ctx.find(target, mr);
  REQUIRE(rc);
  REQUIRE(*rc > 0);

  // 成功
  auto i_ok = mr.try_get<int>("i");
  REQUIRE(i_ok.has_value());
  CHECK(*i_ok == 42);

  auto f_ok = mr.try_get<double>("f");
  REQUIRE(f_ok.has_value());
  CHECK(*f_ok == 3.14);

  // インデックスでも同じ
  auto i0 = mr.try_get<int>(1uz);
  REQUIRE(i0.has_value());
  CHECK(*i0 == 42);

  auto f0 = mr.try_get<double>(2uz);
  REQUIRE(f0.has_value());
  CHECK(*f0 == 3.14);

  // 範囲外インデックス → nullopt
  CHECK(not mr.try_get<int>(99uz).has_value());
  // 存在しない名前 → nullopt
  CHECK(not mr.try_get<int>("nope").has_value());
}

TEST_CASE("match_result try_get reports conversion failure for non-numeric content", "[match_result][try_get]") {
  pcrepp::context ctx{"(?<v>[^:]+)"};
  pcrepp::match_result mr{ctx};

  // 数値として解釈できない文字列
  {
    std::string_view target = "abc";
    auto const rc = ctx.find(target, mr);
    REQUIRE(rc);
    REQUIRE(*rc > 0);

    CHECK(not mr.try_get<int>("v").has_value());
    CHECK(not mr.try_get<double>("v").has_value());
    CHECK(not mr.try_get<float>("v").has_value());
  }
  // 範囲外整数
  {
    std::string_view target = "99999999999999999999999";
    auto const rc = ctx.find(target, mr);
    REQUIRE(rc);
    REQUIRE(*rc > 0);
    CHECK(not mr.try_get<int>("v").has_value());
    CHECK(not mr.try_get<std::int8_t>("v").has_value());
  }
  // 全桁消費できない ("123abc" のようなケース)
  {
    std::string_view target = "123abc";
    auto const rc = ctx.find(target, mr);
    REQUIRE(rc);
    REQUIRE(*rc > 0);
    CHECK(not mr.try_get<int>("v").has_value());
    CHECK(not mr.try_get<double>("v").has_value());
  }
  // 成功ケース
  {
    std::string_view target = "42";
    auto const rc = ctx.find(target, mr);
    REQUIRE(rc);
    REQUIRE(*rc > 0);
    auto v = mr.try_get<int>("v");
    REQUIRE(v.has_value());
    CHECK(*v == 42);
  }
}
