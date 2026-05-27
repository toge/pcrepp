#include "pcrepp.hpp"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("constexpr capture group counting", "[capture_count]") {
  using pcrepp::detail::count_capture_groups;

  static_assert(count_capture_groups(R"()") == 0);
  static_assert(count_capture_groups(R"((a))") == 1);
  static_assert(count_capture_groups(R"((a)(b)(c))") == 3);
  static_assert(count_capture_groups(R"((?:a)(b)(?:c))") == 1);
  static_assert(count_capture_groups(R"((?=a)(?!b)(?<=c)(?<!d))") == 0);
  static_assert(count_capture_groups(R"((?<name>a)(?'id'b)(?P<x>c))") == 3);
  static_assert(count_capture_groups(R"(\((a)\)[(][)]([)])") == 2);
  static_assert(count_capture_groups(R"((?#comment)(a)(?>x)(b))") == 2);
  static_assert(count_capture_groups(R"([]](a))") == 1);
  static_assert(count_capture_groups(R"([^]](a))") == 1);
  static_assert(count_capture_groups(R"([()])") == 0);
  static_assert(count_capture_groups(R"re(<li class=\"item-card[\\s\\S]*?data-product-id=\"([0-9]+)\"[\\s\\S]*?data-product-price=\"([0-9]+)\"[\\s\\S]*?<div class=\"item-card__title\"><a[\\s\\S]*?href=\"(https://booth\\.pm/ja/items/[0-9]+)\">([\\s\\S]*?)</a>[\\s\\S]*?<div class=\"item-card__shop-name\">([\\s\\S]*?)</div>\000")re") == 5);
  CHECK(count_capture_groups(R"((a)(b))") == 2);
}
