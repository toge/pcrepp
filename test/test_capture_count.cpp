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

  // 制御動詞 (*VERB) はキャプチャではない
  static_assert(count_capture_groups(R"((*MARK:foo)(a)(*ACCEPT)(b))") == 2);
  static_assert(count_capture_groups(R"((*FAIL)(a))") == 1);
  static_assert(count_capture_groups(R"((a)(*F)(b))") == 2);

  // コメント (?#...) 内の ( ) は無視する
  static_assert(count_capture_groups(R"((?#(no)(no))(a))") == 1);
  static_assert(count_capture_groups(R"((?#has (paren) inside)(a)(b))") == 2);

  // \Q...\E 内の ( ) はリテラル
  static_assert(count_capture_groups(R"(\Q(a)(b)\E(c))") == 1);
  static_assert(count_capture_groups(R"(\Q\(*\E(x)(y))") == 2);

  // 文字クラス内の \Q...\E
  static_assert(count_capture_groups(R"([\Q(a)\E](b))") == 1);

  // branch reset (?|...) 内の最大キャプチャ数
  static_assert(count_capture_groups(R"((?|(a)(b)|(c)(d)))") == 2);
  static_assert(count_capture_groups(R"((?|(a)|(b)(c)))") == 2);
  static_assert(count_capture_groups(R"((?|(a)(b)(c)|(d)|(e)(f)))") == 3);
  // ブランチ 1: 1 個 (?:a) + (b) = 1、ブランチ 2: (c)(d)(e) = 3 → max = 3
  static_assert(count_capture_groups(R"((?|(?:a)(b)|(c)(d)(e)))") == 3);
  // ネスト: 外側ブランチ 1 は内側 (?|(a)|(b)(c)) = 2、外側ブランチ 2 は (d) = 1 → max = 2
  static_assert(count_capture_groups(R"((?|(?|(a)|(b)(c))|(d)))") == 2);

  // 制御動詞が閉じない場合はパターン末尾まで読み飛ばす
  static_assert(count_capture_groups(R"((a)(*MARK:no_close)") == 1);
}
