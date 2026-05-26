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

  CHECK(count_capture_groups(R"((a)(b))") == 2);
}
