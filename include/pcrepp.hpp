#pragma once

#include <array>
#include <charconv>
#include <concepts>
#include <cstring>
#include <expected>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <tuple>
#include <string>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <type_traits>
#include <vector>

// C++23 std::generator サポート確認
#if __has_include(<generator>) && __cpp_lib_generator >= 202207L
#include <generator>
#define PCREPP_HAS_GENERATOR 1
#else
#define PCREPP_HAS_GENERATOR 0
#endif

// frozencharsのヘッダが存在する場合は型変換のためにインクルードする
#if __has_include(<frozenchars.hpp>)
#include <frozenchars.hpp>
#define PCREPP_HAS_FROZENCHARS
#elif __has_include(<frozenchars/frozenchars.hpp>)
#include <frozenchars/frozenchars.hpp>
#define PCREPP_HAS_FROZENCHARS
#endif

#ifdef WITH_CTRE
#include <ctre.hpp>
#endif

#include "fast_float/fast_float.h"

// 文字列リテラル NTTP（find<"a+"> など）との曖昧性回避のため、
// frozenchars の同名オーバーロードはデフォルト無効にする。
#ifndef PCREPP_ENABLE_FROZENCHARS_NTTP_OVERLOADS
#define PCREPP_ENABLE_FROZENCHARS_NTTP_OVERLOADS 0
#endif

#define PCRE2_CODE_UNIT_WIDTH 8
#include "pcre2.h"

namespace pcrepp {

/**
 * @brief 高速化のためのオプション定数
 * バリデーション済みの文字列を扱う場合に、PCRE2 による UTF チェックをスキップできます。
 */
inline constexpr unsigned int no_utf_check = PCRE2_NO_UTF_CHECK;

/**
 * @brief pcre2_substitute の option には PCRE2 定数を直接使用する
 *
 * `context::replace(target, replacement, option)` の `option` に PCRE2 の置換フラグを組み合わせて使用する。
 * ```cpp
 * ctx.replace(target, "$1", PCRE2_SUBSTITUTE_GLOBAL | PCRE2_SUBSTITUTE_EXTENDED);
 * ```
 *
 * 主なフラグ:
 * - `PCRE2_SUBSTITUTE_GLOBAL` : 全箇所を置換する（デフォルト動作）
 * - `PCRE2_SUBSTITUTE_EXTENDED` : 置換文字列で $1、${1}、\U...\E 等の拡張構文を有効にする
 * - `PCRE2_SUBSTITUTE_OVERFLOW_LENGTH` : バッファ不足時に必要サイズを返す
 * - `PCRE2_SUBSTITUTE_REPLACEMENT_ONLY` : 置換後の文字列のみ返す（マッチしない部分を除外）
 * - `PCRE2_SUBSTITUTE_UNKNOWN_UNSET` : 未定義の名前付きキャプチャを空文字列として扱う
 * - `PCRE2_SUBSTITUTE_UNSET_EMPTY` : 未マッチのキャプチャグループを空文字列として扱う
 */

/**
 * @struct fixed_string
 * @brief NTTP用のコンパイル時文字列ラッパー
 *
 * テンプレート引数として正規表現パターンを指定するためのヘルパー型です。
 * char配列をコンパイル時に std::string_view に変換します。
 *
 * @tparam N 文字列の長さ（null終端を含む）
 */
template <size_t N>
struct fixed_string {
  std::array<char, N> value{};
  size_t length = (N > 0uz ? N - 1uz : 0uz);

  /**
   * @brief コンストラクタ：文字列リテラルから初期化
   */
  constexpr fixed_string(char const (&src)[N]) {
    static_assert(N > 0uz, "fixed_string requires N > 0");
    std::ranges::copy(src, value.begin());
    length = N - 1uz;
  }

#ifdef PCREPP_HAS_FROZENCHARS
  /**
   * @brief コンストラクタ：frozenchars::FrozenString から初期化
   */
  template <size_t M>
  constexpr fixed_string(frozenchars::FrozenString<M> const& src) {
    static_assert(M <= N, "FrozenString capacity is too large");
    auto const s = src.sv();
    if (s.size() >= N) {
      // 念のため終端NUL分を残して切り詰め
      std::ranges::copy(s.substr(0uz, N - 1uz), value.begin());
      value[N - 1uz] = '\0';
      length = N - 1uz;
      return;
    }

    std::ranges::copy(s.begin(), s.end(), value.begin());
    value[s.size()] = '\0';
    length = s.size();
  }
#endif

  /**
   * @brief 文字列を std::string_view に変換
   * @return null終端を除いた文字列ビュー
   */
  constexpr auto view() const noexcept -> std::string_view {
    return {value.data(), length};
  }
};
template <size_t N>
fixed_string(char const (&)[N]) -> fixed_string<N>;

#ifdef PCREPP_HAS_FROZENCHARS
/**
 * @brief frozenchars::FrozenString を fixed_string に変換する
 */
template <size_t N>
constexpr auto to_fixed_string(frozenchars::FrozenString<N> const& fs) {
  return fixed_string<N>(fs);
}
#endif

template <typename T>
concept supported_integer_get_type =
  std::same_as<T, char> ||
  std::same_as<T, signed char> ||
  std::same_as<T, unsigned char> ||
  std::same_as<T, short> ||
  std::same_as<T, unsigned short> ||
  std::same_as<T, int> ||
  std::same_as<T, unsigned int> ||
  std::same_as<T, long> ||
  std::same_as<T, unsigned long> ||
  std::same_as<T, long long> ||
  std::same_as<T, unsigned long long>;

template <typename T>
concept supported_match_result_get_type =
  std::same_as<T, std::string_view> ||
  std::same_as<T, std::string> ||
  std::same_as<T, float> ||
  std::same_as<T, double> ||
  supported_integer_get_type<T>;

template <typename T>
inline auto constexpr supported_match_result_get_is_nothrow =
  std::same_as<T, std::string_view> ||
  std::same_as<T, float> ||
  std::same_as<T, double> ||
  supported_integer_get_type<T>;

struct match_result;

namespace detail {
/**
 * @brief 正規表現パターンのキャプチャグループ数を constexpr で計算
 *
 * 開き括弧を走査し、エスケープ・文字クラス・各種拡張構文を考慮して、
 * キャプチャグループ数を数えます。PCRE2 のセマンティクスに従い、
 * branch reset `(?|...)` については「各分岐の最大値」を採用します。
 *
 * 加算されるもの：
 * - `(...)` : 通常のキャプチャグループ
 * - `(?<name>...)` / `(?'name'...)` / `(?P<name>...)` : 名前付きキャプチャ
 *
 * 加算されないもの：
 * - `(?:...)` : 非キャプチャグループ
 * - `(?=...)` / `(?!...)` / `(?<=...)` / `(?<!...)` : lookaround
 * - `(?>...)` : atomic group
 * - `(?#...)` : コメント
 * - `(*VERB...)` (例: `(*MARK:foo)`, `(*FAIL)`, `(*ACCEPT)`, `(*F)`) : 制御動詞
 * - `(?|...)` 自身は非キャプチャ (内部の最大分岐キャプチャ数が採用される)
 * - `(?(cond)...)` / `(?R)` / `(?1)` 等の再帰・条件・部分式呼び出し
 * - エスケープされた括弧 `\(` / `\)`
 * - 文字クラス内 `[...]` の括弧
 * - `\Q...\E` で囲われた範囲の括弧
 */
constexpr auto count_capture_groups(std::string_view const pattern) noexcept -> size_t {
  // (?|...) ブランチリセットの状態を保持するスタック要素。
  // - current : 現在のブランチで蓄積中のキャプチャ数
  // - max     : ブランチリセット内で観測された最大値
  // - depth   : ブランチリセット内で開いている括弧の深さ
  struct br_state { size_t current; size_t max; size_t depth; };

  /// @attention br_stack の制限: 深さ 64 まで。超過時は誤カウントになる。
  std::array<br_state, 64uz> br_stack{};
  auto capture_count = 0uz;  // 最終的なキャプチャ数
  auto br_depth    = 0uz;  // アクティブな (?|...) のネスト数 (0 = なし)
  auto br_current  = 0uz;  // 現在のブランチで数えたキャプチャ数
  auto br_max      = 0uz;  // 現在の (?|...) 内で最大だったブランチの値
  auto br_parens   = 0uz;  // 現在の (?|...) 内で開いている `(` の深さ

  auto in_class    = false;  // [...] 内
  auto in_literal  = false;  // \Q...\E 内
  auto in_comment  = false;  // (?#...) 内
  auto in_verb     = false;  // (*VERB...) 内
  auto escaped     = false;
  auto class_start = false;
  auto comment_parens = 0uz;  // (?# 内 / (* 内の括弧の深さ
  auto verb_parens    = 0uz;

  auto const finalize_inner_branch_reset = [&]() -> size_t {
    // 現在の (?|...) を閉じるときの共通処理。
    // この (?|...) が生成するキャプチャ数 (= 各ブランチの最大値) を返す。
    br_max = (br_current > br_max) ? br_current : br_max;
    return br_max;
  };

  for (auto i = 0uz; i < pattern.size(); ++i) {
    auto const ch = pattern[i];

    // (?#comment) 内: 対応する ) で抜ける (内部の ( を入れ子カウント)
    // comment_parens は (?# 内で開いている「内側」の ( の数 (外側 (?# の ( は含まない)
    if (in_comment) {
      if (escaped) { escaped = false; continue; }
      if (ch == '\\') { escaped = true; continue; }
      if (ch == '(') { ++comment_parens; continue; }
      if (ch == ')') {
        if (comment_parens == 0uz) {
          // (?# の対応する )
          in_comment = false;
        } else {
          --comment_parens;
        }
      }
      continue;
    }

    // (*VERB) 内: 対応する ) で抜ける (内部の ( を入れ子カウント)
    if (in_verb) {
      if (escaped) { escaped = false; continue; }
      if (ch == '\\') { escaped = true; continue; }
      if (ch == '(') { ++verb_parens; continue; }
      if (ch == ')') {
        if (verb_parens == 0uz) {
          in_verb = false;
        } else {
          --verb_parens;
        }
      }
      continue;
    }

    // \Q...\E 内: すべてリテラル (エスケープシーケンスは存在せず、\E のみが終端)
    if (in_literal) {
      if (ch == '\\' && i + 1uz < pattern.size() && pattern[i + 1uz] == 'E') {
        ++i;  // 'E' をスキップ
        in_literal = false;
      }
      continue;
    }

    // [...] 内
    if (in_class) {
      if (escaped) { escaped = false; class_start = false; continue; }
      if (ch == '\\') {
        class_start = false;
        // クラス内でも \Q...\E は有効
        if (i + 1uz < pattern.size() && pattern[i + 1uz] == 'Q') {
          in_literal = true;
          ++i;  // Q 自体をスキップ
          continue;
        }
        escaped = true;
        continue;
      }
      if (ch == ']' && !class_start) {
        in_class = false;
      }
      class_start = false;
      continue;
    }

    // 通常モード
    if (escaped) {
      escaped = false;
      if (ch == 'Q') {
        in_literal = true;
      }
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    if (ch == '[') {
      in_class = true;
      class_start = true;
      if (i + 1uz < pattern.size() && pattern[i + 1uz] == '^') {
        ++i;
        class_start = true;
      }
      continue;
    }

    if (ch == '|') {
      if (br_depth > 0uz) {
        br_max = (br_current > br_max) ? br_current : br_max;
        br_current = 0uz;
      }
      continue;
    }

    if (ch == ')') {
      if (br_depth > 0uz) {
        if (br_parens > 0uz) {
          --br_parens;  // 内側グループの閉じ
        } else {
          // (?|...) 自体の閉じ
          auto const inner_caps = finalize_inner_branch_reset();
          --br_depth;
          if (br_depth == 0uz) {
            // 最外の (?|) の閉じ: キャプチャ数を total に確定
            capture_count += inner_caps;
            br_current = 0uz;
            br_max = 0uz;
            br_parens = 0uz;
          } else {
            // ネストした内側の (?|...) の閉じ → 外側へ寄与
            auto const outer = br_stack[br_depth - 1uz];
            br_current = outer.current + inner_caps;
            br_max = outer.max;
            br_parens = outer.depth;
          }
        }
      }
      continue;
    }

    if (ch != '(') {
      continue;
    }

    // ch == '('

    // (?#comment)
    if (i + 2uz < pattern.size() && pattern[i + 1uz] == '?' && pattern[i + 2uz] == '#') {
      in_comment = true;
      comment_parens = 0uz;  // (?# 自体の ( は含めず、内部の入れ子のみカウント
      i += 2uz;  // ?# をスキップ
      continue;
    }

    // (*VERB)
    if (i + 1uz < pattern.size() && pattern[i + 1uz] == '*') {
      in_verb = true;
      verb_parens = 0uz;
      i += 1uz;  // * をスキップ
      continue;
    }

    // (?|...) branch reset
    if (i + 2uz < pattern.size() && pattern[i + 1uz] == '?' && pattern[i + 2uz] == '|') {
      if (br_depth < br_stack.size()) {
        br_stack[br_depth] = {br_current, br_max, br_parens};
        ++br_depth;
        br_current = 0uz;
        br_max = 0uz;
        br_parens = 0uz;
      }
      i += 2uz;  // ?| をスキップ
      continue;
    }

    // キャプチャ判定
    auto is_capture = false;
    if (i + 1uz >= pattern.size() || pattern[i + 1uz] != '?') {
      is_capture = true;
    } else {
      // (?...) 系: 名前付きキャプチャなら true
      if (i + 2uz < pattern.size()) {
        auto const marker = pattern[i + 2uz];
        if (marker == '<') {
          if (i + 3uz < pattern.size()) {
            auto const next = pattern[i + 3uz];
            if (next != '=' && next != '!') {
              is_capture = true;  // (?<name>...)
            }
          }
        } else if (marker == '\'') {
          is_capture = true;  // (?'name'...)
        } else if (marker == 'P') {
          if (i + 3uz < pattern.size() && pattern[i + 3uz] == '<') {
            is_capture = true;  // (?P<name>...)
          }
        }
        // (?:) (?=) (?!...) (?>...) (?<=) (?<!) (?(... (?R) (?1) は非キャプチャ
      }
    }

    // ブランチリセット内の場合: どのグループも (キャプチャ・非キャプチャ問わず)
    // `(` の入れ子を追跡する必要がある。キャプチャならカウンタも増やす。
    if (br_depth > 0uz) {
      ++br_parens;
      if (is_capture) {
        ++br_current;
      }
    } else if (is_capture) {
      ++capture_count;
    }
  }

  // 閉じられていない (?| がある場合 (フォールバック): 現在の最大値を total に加算
  if (br_depth > 0uz) {
    auto pending = finalize_inner_branch_reset();
    // ネストした未閉じスタックをフラットに加算
    for (auto d = br_depth; d > 0uz; --d) {
      auto const outer = br_stack[d - 1uz];
      pending += outer.current;
    }
    capture_count += pending;
  }

  return capture_count;
}

/**
 * @brief N個の std::string_view からなるタプル型を生成するメタ関数
 * @tparam T 要素型（通常は std::string_view）
 * @tparam Is インデックスシーケンス
 */
template <typename T, size_t>
struct same_type {
  using type = T;
};

/**
 * @brief N個の std::string_view をまとめたタプル型（内部実装用）
 * @tparam T 要素型
 * @tparam Is インデックスシーケンス
 */
template <typename T, size_t... Is>
auto make_repeated_tuple_type_impl(std::index_sequence<Is...>) -> std::tuple<typename same_type<T, Is>::type...>;

/**
 * @brief N個の std::string_view をまとめたタプル型
 * @tparam N タプルの要素数
 */
template <size_t N>
using string_view_tuple_t = decltype(make_repeated_tuple_type_impl<std::string_view>(std::make_index_sequence<N>{}));

/**
 * @brief マッチ結果を表すタプル型：bool（マッチ成否） + N個の std::string_view
 * @tparam N キャプチャグループ数
 */
template <size_t N>
using nttp_match_tuple_t = string_view_tuple_t<N>;

/**
 * @brief 整数型パースのコア実装
 * @tparam T パース対象の整数型
 * @param sv パース対象の文字列
 * @return パース結果とエラーコード
 */
template <supported_integer_get_type T>
auto parse_integer_core(std::string_view const sv) noexcept -> std::pair<T, bool> {
  auto value = T{};
  auto const [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), value);
  return {value, ec == std::errc{} && ptr == sv.data() + sv.size()};
}

template <supported_integer_get_type T>
auto parse_integer(std::string_view const sv) noexcept -> T {
  auto const [value, ok] = parse_integer_core<T>(sv);
  return ok ? value : T{};
}

template <supported_integer_get_type T>
auto try_parse_integer(std::string_view const sv) noexcept -> std::optional<T> {
  auto const [value, ok] = parse_integer_core<T>(sv);
  return ok ? std::optional<T>{value} : std::nullopt;
}

/**
 * @brief 浮動小数点型パースのコア実装
 * @tparam T パース対象の浮動小数点型
 * @param sv パース対象の文字列
 * @return パース結果とエラーコード
 */
template <typename T>
  requires(std::same_as<T, float> || std::same_as<T, double>)
auto parse_floating_core(std::string_view const sv) noexcept -> std::pair<T, bool> {
  auto value = T{};
  auto const result = fast_float::from_chars(sv.data(), sv.data() + sv.size(), value);
  return {value, result.ec == std::errc{} && result.ptr == sv.data() + sv.size()};
}

template <typename T>
  requires(std::same_as<T, float> || std::same_as<T, double>)
auto parse_floating(std::string_view const sv) noexcept -> T {
  auto const [value, ok] = parse_floating_core<T>(sv);
  return ok ? value : T{};
}

template <typename T>
  requires(std::same_as<T, float> || std::same_as<T, double>)
auto try_parse_floating(std::string_view const sv) noexcept -> std::optional<T> {
  auto const [value, ok] = parse_floating_core<T>(sv);
  return ok ? std::optional<T>{value} : std::nullopt;
}

struct tls_match_data_cache {
  pcre2_match_data* data           = nullptr;
  uint32_t          capture_count = 0;

  ~tls_match_data_cache() {
    if (data) {
      pcre2_match_data_free(data);
    }
  }

  auto get(pcre2_code const* code) -> pcre2_match_data* {
    auto cc = uint32_t{};
    pcre2_pattern_info(code, PCRE2_INFO_CAPTURECOUNT, &cc);
    if (not data || cc > capture_count) {
      if (data) {
        pcre2_match_data_free(data);
        data = nullptr;
      }
      data          = pcre2_match_data_create_from_pattern(code, nullptr);
      capture_count = cc;
    }
    return data;
  }
};

inline auto get_tls_match_data(pcre2_code const* code) -> pcre2_match_data* {
  static thread_local tls_match_data_cache cache;
  return cache.get(code);
}

/**
 * @brief pcre2_substring_number_from_name は NUL 終端文字列を要求するため、
 *        std::string_view を内部バッファにコピーして NUL 終端したうえで呼び出す。
 *
 * 短い名前は SBO 風の固定バッファで賄い、長さは動的に確保する。
 * 名前が空の場合は PCRE2_ERROR_NOSUBSTRING (-56) を直接返す。
 */
inline auto lookup_named_capture(pcre2_code const* code, std::string_view const name) -> int {
  if (name.empty()) {
    return PCRE2_ERROR_NOSUBSTRING;
  }
  if (name.size() <= 255uz) {
    // 短ければスタックバッファにコピー (最大 255 バイト + NUL)
    auto buf = std::array<char, 256uz>{};
    std::ranges::copy(name, buf.begin());
    buf[name.size()] = '\0';
    return pcre2_substring_number_from_name(code, reinterpret_cast<PCRE2_SPTR8>(buf.data()));
  }
  // 長い名前はヒープで
  auto owned = std::string{name};
  return pcre2_substring_number_from_name(code, reinterpret_cast<PCRE2_SPTR8>(owned.c_str()));
}
}  // namespace detail

struct use_tls_t {};
inline constexpr auto use_tls = use_tls_t{};


template <bool UseJIT>
struct iterator;

template <bool UseJIT = true, unsigned JITFlags = PCRE2_JIT_COMPLETE>
struct context;

/**
 * @struct match_result
 * @brief 個別のマッチング結果と状態を保持する構造体
 */
struct match_result {
  struct data_holder {
    pcre2_code const* code    = nullptr;
    pcre2_match_data* data    = nullptr;
    size_t*           ovector = nullptr;
    std::string_view  target  = {};  ///< @brief 非所有。get() コール時点で元の文字列が生存している必要あり

    data_holder(pcre2_code const* c) : code(c) {
      if (code) {
        data    = pcre2_match_data_create_from_pattern(code, nullptr);
        ovector = data ? pcre2_get_ovector_pointer(data) : nullptr;
      }
    }

    /// @brief 既存の pcre2_match_data を受け取るコンストラクタ（E11 oversized match_data 用）
    data_holder(pcre2_code const* c, pcre2_match_data* d) : code(c), data(d) {
      ovector = data ? pcre2_get_ovector_pointer(data) : nullptr;
    }

    ~data_holder() {
      if (data) {
        pcre2_match_data_free(data);
      }
    }
  };
  std::shared_ptr<data_holder> holder;

  match_result() = default;
  template <bool UseJIT, unsigned JITFlags>
  match_result(context<UseJIT, JITFlags> const& ctx);
  template <bool UseJIT, unsigned JITFlags>
  match_result(context<UseJIT, JITFlags> const& ctx, size_t oversized_capture_count);

  /**
   * @brief other の内容をこのオブジェクトにコピーする共通ヘルパ
   * @param other コピー元
   */
  void copy_from(match_result const& other) {
    if (other.holder && other.holder->code) {
      holder = std::make_shared<data_holder>(other.holder->code);
      copy_ovector(holder->ovector, holder->data, other.holder->ovector, other.holder->data);
      holder->target = other.holder->target;
    } else {
      holder.reset();
    }
  }

  match_result(match_result const& other) { copy_from(other); }

  auto operator=(match_result const& other) -> match_result& {
    if (this != &other) {
      copy_from(other);
    }
    return *this;
  }

  match_result(match_result&&) noexcept = default;
  auto operator=(match_result&&) noexcept -> match_result& = default;

  /**
   * @brief 指定したインデックスのグループ文字列を取得する
   */
  template <supported_match_result_get_type T = std::string_view>
  auto get(size_t const index) const noexcept(supported_match_result_get_is_nothrow<T>) -> T {
    auto const sv = get_view(index);
    if constexpr (std::same_as<T, std::string_view>) {
      return sv;
    } else if constexpr (std::same_as<T, std::string>) {
      return std::string{sv};
    } else if constexpr (supported_integer_get_type<T>) {
      return detail::parse_integer<T>(sv);
    } else {
      return detail::parse_floating<T>(sv);
    }
  }

  /**
   * @brief 名前付きキャプチャグループを取得する
   *
   * name は NUL 終端されていなくても安全 (内部で NUL 終端バッファにコピーする)
   */
  template <supported_match_result_get_type T = std::string_view>
  auto get(std::string_view const name) const noexcept(supported_match_result_get_is_nothrow<T>) -> T {
    if (not holder || not holder->code || not holder->data) {
      return T{};
    }
    auto const index = detail::lookup_named_capture(holder->code, name);
    if (index < 0) {
      return T{};
    }
    return get<T>(static_cast<size_t>(index));
  }

  /**
   * @brief 数値型を std::optional として取得する (変換失敗を std::nullopt で判別可能)
   *
   * 文字列・浮動小数点型は `T` ではなく `std::optional<T>` を返す。
   * 対応しない型を指定するとコンパイルエラー。
   */
  template <supported_integer_get_type T>
  auto try_get(size_t const index) const noexcept -> std::optional<T> {
    auto const sv = get_view(index);
    if (sv.empty()) {
      // マッチ範囲外 / 未マッチ / 値なし
      return std::nullopt;
    }
    return detail::try_parse_integer<T>(sv);
  }
  template <typename T>
    requires(std::same_as<T, float> || std::same_as<T, double>)
  auto try_get(size_t const index) const noexcept -> std::optional<T> {
    auto const sv = get_view(index);
    if (sv.empty()) {
      return std::nullopt;
    }
    return detail::try_parse_floating<T>(sv);
  }

  /**
   * @brief std::string_view 版 try_get — アンマッチは nullopt、空マッチは optional{""}
   *
   * `PCRE2_UNSET` を直接確認することで空マッチとアンマッチを区別する。
   */
  template <typename T>
    requires std::same_as<T, std::string_view>
  auto try_get(size_t const index) const noexcept -> std::optional<std::string_view> {
    return try_get_view(index);
  }

  /**
   * @brief std::string 版 try_get — アンマッチは nullopt、空マッチは optional{""}
   */
  template <typename T>
    requires std::same_as<T, std::string>
  auto try_get(size_t const index) const -> std::optional<std::string> {
    auto sv = try_get<std::string_view>(index);
    if (not sv) return std::nullopt;
    return std::string{*sv};
  }

  /**
   * @brief 名前付きグループの数値型を std::optional として取得
   */
  template <supported_integer_get_type T>
  auto try_get(std::string_view const name) const noexcept -> std::optional<T> {
    if (not holder || not holder->code || not holder->data) {
      return std::nullopt;
    }
    auto const index = detail::lookup_named_capture(holder->code, name);
    if (index < 0) {
      return std::nullopt;
    }
    return try_get<T>(static_cast<size_t>(index));
  }
  template <typename T>
    requires(std::same_as<T, float> || std::same_as<T, double>)
  auto try_get(std::string_view const name) const noexcept -> std::optional<T> {
    if (not holder || not holder->code || not holder->data) {
      return std::nullopt;
    }
    auto const index = detail::lookup_named_capture(holder->code, name);
    if (index < 0) {
      return std::nullopt;
    }
    return try_get<T>(static_cast<size_t>(index));
  }

  /**
   * @brief std::string_view 版 try_get (名前指定) — アンマッチは nullopt
   */
  template <typename T>
    requires std::same_as<T, std::string_view>
  auto try_get(std::string_view const name) const noexcept -> std::optional<std::string_view> {
    if (not holder || not holder->code || not holder->data) return std::nullopt;
    auto const idx = detail::lookup_named_capture(holder->code, name);
    if (idx < 0) return std::nullopt;
    return try_get<std::string_view>(static_cast<size_t>(idx));
  }

  /**
   * @brief std::string 版 try_get (名前指定) — アンマッチは nullopt
   */
  template <typename T>
    requires std::same_as<T, std::string>
  auto try_get(std::string_view const name) const -> std::optional<std::string> {
    auto sv = try_get<std::string_view>(name);
    if (not sv) return std::nullopt;
    return std::string{*sv};
  }

  /**
   * @brief match_result を N 要素タプルに変換（構造化束縛用）
   * @tparam N グループ数（全体マッチ + キャプチャ数）
   * @note 実装は detail::match_result_to_tuple を使用するため、.hpp 末尾に out-of-line 定義
   */
  template <size_t N>
  auto to_tuple() const -> detail::string_view_tuple_t<N>;

  auto     operator[](size_t const index) const noexcept -> std::string_view { return get(index); }
  /// @brief 負値を渡すと std::out_of_range を投げる
  auto operator[](int const index) const -> std::string_view {
    if (index < 0) {
      throw std::out_of_range{"match_result::operator[]: negative index"};
    }
    return get(static_cast<size_t>(index));
  }
  auto     operator[](std::string_view const name) const noexcept -> std::string_view { return get(name); }
  explicit operator bool() const noexcept { return holder && holder->data; }

  auto size() const noexcept -> size_t { return holder ? pcre2_get_ovector_count(holder->data) : 0uz; }

  // イテレーション対応
  struct group_iterator {
    using iterator_category = std::forward_iterator_tag;
    using value_type        = std::string_view;
    using difference_type   = std::ptrdiff_t;
    using pointer           = value_type*;
    using reference         = value_type&;

    match_result const* res   = nullptr;
    size_t index = 0uz;

    auto operator++() noexcept -> group_iterator& {
      ++index;
      return *this;
    }
    auto operator*() const noexcept -> std::string_view { return res->get(index); }
    auto operator==(group_iterator const& other) const noexcept -> bool { return index == other.index; }
  };

  auto begin() const -> group_iterator { return {this, 0uz}; }
  auto end() const -> group_iterator { return {this, size()}; }

  auto start_pos() const noexcept -> size_t { return holder ? holder->ovector[0] : 0uz; }
  auto end_pos()   const noexcept -> size_t { return holder ? holder->ovector[1] : 0uz; }

private:
  /**
   * @brief ovector を宛先容量に収まる範囲でコピーする
   */
  static auto copy_ovector(size_t* dst_ovector, pcre2_match_data* dst_data, size_t const* src_ovector, pcre2_match_data* src_data) noexcept -> void {
    if (not dst_ovector || not dst_data || not src_ovector || not src_data) {
      return;
    }
    auto const dst_count  = static_cast<size_t>(pcre2_get_ovector_count(dst_data));
    auto const src_count  = static_cast<size_t>(pcre2_get_ovector_count(src_data));
    auto const pair_count = (dst_count < src_count) ? dst_count : src_count;
    std::memcpy(dst_ovector, src_ovector, sizeof(size_t) * pair_count * 2uz);
    for (auto const i : std::views::iota(pair_count * 2uz, dst_count * 2uz)) {
      dst_ovector[i] = PCRE2_UNSET;
    }
  }

  match_result(pcre2_code const* code, pcre2_match_data* src_data, std::string_view target) {
    if (code && src_data) {
      holder = std::make_shared<data_holder>(code);
      copy_ovector(holder->ovector, holder->data, pcre2_get_ovector_pointer(src_data), src_data);
      holder->target = target;
    }
  }

  auto try_get_view(size_t const index) const noexcept -> std::optional<std::string_view> {
    if (not holder || not holder->data) return std::nullopt;
    if (index >= pcre2_get_ovector_count(holder->data)) return std::nullopt;
    auto const s = holder->ovector[index * 2uz];
    auto const e = holder->ovector[index * 2uz + 1uz];
    if (s == PCRE2_UNSET || e == PCRE2_UNSET) return std::nullopt;
    if (s > e) return std::nullopt;
    if (s > holder->target.size()) return std::nullopt;
    return holder->target.substr(s, e - s);
  }

  auto get_view(size_t const index) const noexcept -> std::string_view {
    if (not holder || not holder->data) {
      return {};
    }
    auto const s = holder->ovector[index * 2uz + 0uz];
    auto const e = holder->ovector[index * 2uz + 1uz];
    if (s == PCRE2_UNSET || e == PCRE2_UNSET) {
      return {};
    }
    if (s > holder->target.size()) {
      return {};
    }
    return holder->target.substr(s, e - s);
  }

  match_result(pcre2_code const* code) {
    if (code) {
      holder = std::make_shared<data_holder>(code);
    }
  }

  auto get_ovector() const noexcept -> size_t* { return holder ? holder->ovector : nullptr; }
  auto get_data() const noexcept -> pcre2_match_data* { return holder ? holder->data : nullptr; }
  auto set_target(std::string_view const t) noexcept {
    if (holder) {
      holder->target = t;
    }
  }

  template <bool UseJIT> friend struct iterator;
  template <bool UseJIT, unsigned JITFlags> friend struct context;
};

namespace detail {
/**
 * @brief match_result をタプルに変換（内部実装用）
 *
 * match_result の各キャプチャグループを std::get<> でアクセス可能なタプルに変換します。
 *
 * @tparam N タプルの要素数（全体マッチ + キャプチャ数）
 * @tparam Is インデックスシーケンス
 * @param mr マッチ結果
 * @return 全体マッチ + 各キャプチャグループを含むタプル
 */
template <size_t N, size_t... Is>
auto match_result_to_tuple_impl(match_result const& mr, std::index_sequence<Is...>) -> nttp_match_tuple_t<N> {
  return std::make_tuple(mr.get(Is)...);
}

/**
 * @brief match_result をタプルに変換（パブリック用）
 *
 * @tparam N タプルの要素数（全体マッチ + キャプチャ数）
 * @param mr マッチ結果
 * @return 全体マッチ + 各キャプチャグループを含むタプル
 */
template <size_t N>
auto match_result_to_tuple(match_result const& mr) -> nttp_match_tuple_t<N> {
  return match_result_to_tuple_impl<N>(mr, std::make_index_sequence<N>{});
}
}  // namespace detail

/// @brief match_result::to_tuple<N>() の out-of-line 定義（detail::match_result_to_tuple を使用）
template <size_t N>
inline auto match_result::to_tuple() const -> detail::string_view_tuple_t<N> {
  return detail::match_result_to_tuple<N>(*this);
}

/**
 * @struct iterator
 * @brief すべてのマッチ箇所を列挙するための前方イテレータ
 */
template <bool UseJIT>
struct iterator {
  using iterator_concept  = std::forward_iterator_tag;
  using iterator_category = std::forward_iterator_tag;
  using value_type        = match_result;
  using difference_type   = std::ptrdiff_t;
  using pointer           = match_result const*;
  using reference         = match_result const&;

  context<UseJIT> const* ctx = nullptr;
  std::string_view target = {};
  size_t pos = 0uz;
  unsigned int option = 0;
  bool is_end = true;
  match_result result;
  std::string* error_ref = nullptr;

  iterator() = default;
  iterator(context<UseJIT> const* c, std::string_view t, size_t p, unsigned int o, bool end,
           std::string* err = nullptr);

  auto operator++() -> iterator&;
  auto operator++(int) -> iterator {
    auto tmp = *this;
    ++(*this);
    return tmp;
  }
  auto operator*() const -> match_result const& { return result; }
  auto operator->() const -> match_result const* { return &result; }
  auto operator==(iterator const& other) const -> bool {
    if (is_end && other.is_end) {
      return ctx == other.ctx;
    }
    return is_end == other.is_end && ctx == other.ctx && pos == other.pos;
  }
};

/**
 * @struct match_range
 * @brief match_result を巡回するための view
 */
template <bool UseJIT>
struct match_range : std::ranges::view_interface<match_range<UseJIT>> {
  iterator<UseJIT> first;
  iterator<UseJIT> last;
  std::string m_error;

  match_range() = default;
  match_range(iterator<UseJIT> f, iterator<UseJIT> l,
              std::string* err = nullptr) : first(f), last(l) {
    if (err) {
      first.error_ref = err;
      last.error_ref  = err;
    }
  }

  auto error() const -> std::string_view { return m_error; }
  explicit operator bool() const noexcept { return m_error.empty(); }

  auto front() const -> match_result { return *begin(); }
  constexpr auto begin() const { return first; }
  constexpr auto end() const { return last; }
};

/**
 * @struct context
 * @brief コンパイル済み正規表現を管理する構造体
 *
 * @tparam UseJIT JIT コンパイルを使用するか（デフォルト true）
 * @tparam JITFlags JIT コンパイルフラグ（デフォルト PCRE2_JIT_COMPLETE）。UseJIT=false 時は無視される。
 */
template <bool UseJIT, unsigned JITFlags>
struct context {
private:
  pcre2_code*          code      = nullptr;
  pcre2_match_context* match_ctx = nullptr;  ///< 制限設定用。nullptr は「制限なし」
  PCRE2_SIZE           jit_size_ = 0;       ///< JIT コードサイズ（キャッシュ用）
  friend struct match_result;

  /// @brief match_ctx が未生成なら生成する（遅延初期化）
  void ensure_match_context() {
    if (not match_ctx) { match_ctx = pcre2_match_context_create(nullptr); }
  }

public:
  context() = default;
  context(std::string_view const src, unsigned int option = 0) {
    if (auto const res = compile(src, option); !res) {
      throw std::runtime_error{res.error()};
    }
  }
  ~context() {
    release();
    if (match_ctx) {
      pcre2_match_context_free(match_ctx);
    }
  }

  /**
   * @brief 例外を投げないファクトリメソッド
   */
  static auto create(std::string_view const src, unsigned int option = 0) -> std::expected<context<UseJIT, JITFlags>, std::string> {
    auto ctx = context<UseJIT, JITFlags>{};
    if (auto const res = ctx.compile(src, option); not res) {
      return std::unexpected{res.error()};
    }
    return ctx;
  }

  context(context const&) = delete;
  auto operator=(context const&) -> context& = delete;

  context(context&& other) noexcept : code(other.code), match_ctx(other.match_ctx), jit_size_(other.jit_size_) {
    other.code      = nullptr;
    other.match_ctx = nullptr;
    other.jit_size_ = 0;
  }
  auto operator=(context&& other) noexcept -> context& {
    if (this != &other) {
      release();
      if (match_ctx) { pcre2_match_context_free(match_ctx); }
      code            = other.code;
      match_ctx       = other.match_ctx;
      jit_size_       = other.jit_size_;
      other.code      = nullptr;
      other.match_ctx = nullptr;
      other.jit_size_ = 0;
    }
    return *this;
  }

  /**
   * @brief コンパイル済みの pcre2_code へのポインタを取得
   */
  auto get_code() const noexcept -> pcre2_code const* { return code; }

  /**
   * @brief 正規表現オブジェクトを解放する
   *
   */
  void release() noexcept {
    if (code) {
      pcre2_code_free(code);
      code = nullptr;
    }
  }

  /**
   * @brief 正規表現のコンパイル
   *
   * UseJITフラグが立っている場合はJITコンパイルも行う
   * @param src 正規表現のパターン文字列
   * @param option コンパイルオプション (PCRE2_UTF, PCRE2_CASELESSなど)
   * @return std::expected<void, std::string> 失敗ならエラーメッセージを返す
   */
  auto compile(std::string_view const src, unsigned int option = 0) -> std::expected<void, std::string> {
    release();

    auto ec = 0;
    auto eo = PCRE2_SIZE{};
    auto const c  = pcre2_compile(reinterpret_cast<PCRE2_SPTR8>(src.data()), src.size(), option, &ec, &eo, nullptr);
    if (not c) {
      auto msg = std::array<PCRE2_UCHAR8, 256uz>{};
      pcre2_get_error_message(ec, msg.data(), msg.size());
      return std::unexpected{"Compile error at offset " + std::to_string(eo) + ": " + reinterpret_cast<char const*>(msg.data())};
    }
    if constexpr (UseJIT) {
      if (auto const jrc = pcre2_jit_compile(c, JITFlags); jrc < 0) {
        auto jit_available = 0;
        pcre2_config(PCRE2_CONFIG_JIT, &jit_available);
        if (!jit_available) {
          // JIT 非対応環境 (Emscripten 等) では JIT をスキップして Interpreter にフォールバック
        } else {
          auto msg = std::array<PCRE2_UCHAR8, 256uz>{};
          pcre2_get_error_message(jrc, msg.data(), msg.size());
          pcre2_code_free(c);
          return std::unexpected{"JIT compile error: " + std::string{reinterpret_cast<char const*>(msg.data())}};
        }
      }
    }
    this->code = c;
    this->jit_size_ = 0;
    std::ignore = pcre2_pattern_info(c, PCRE2_INFO_JITSIZE, &this->jit_size_);
    return {};
  }

  /**
   * @brief 名前付きキャプチャグループのインデックスを取得する
   *
   * name は NUL 終端されていなくても安全 (内部で NUL 終端バッファにコピーする)
   *
   * @param name キャプチャグループ名
   * @return 見つかった場合はインデックス、見つからない場合は負値
   */
  auto capture_index(std::string_view const name) const noexcept -> int {
    if (not code) {
      return -1;
    }
    return detail::lookup_named_capture(code, name);
  }

  // ================================================================
  // E5: パターン情報クエリ
  // ================================================================

  /// @brief キャプチャグループ数を返す（全体マッチは含まない）
  auto capture_count() const noexcept -> uint32_t {
    if (not code) return 0u;
    auto count = uint32_t{};
    std::ignore = pcre2_pattern_info(code, PCRE2_INFO_CAPTURECOUNT, &count);
    return count;
  }

  /// @brief 名前付きキャプチャグループの一覧を返す（名前、インデックス）のペアのベクター
  auto named_captures() const -> std::vector<std::pair<std::string, int>> {
    if (not code) return {};
    auto name_count      = uint32_t{};
    auto name_entry_size = uint32_t{};
    pcre2_pattern_info(code, PCRE2_INFO_NAMECOUNT, &name_count);
    pcre2_pattern_info(code, PCRE2_INFO_NAMEENTRYSIZE, &name_entry_size);
    PCRE2_SPTR name_table = nullptr;
    pcre2_pattern_info(code, PCRE2_INFO_NAMETABLE, &name_table);
    auto result = std::vector<std::pair<std::string, int>>{};
    result.reserve(name_count);
    for (auto i = 0u; i < name_count; ++i) {
      auto const* entry = name_table + i * name_entry_size;
      auto const  idx   = static_cast<int>((static_cast<unsigned>(entry[0]) << 8u) | static_cast<unsigned>(entry[1]));
      // PCRE2 仕様により名称は entry+2 以降に NUL 終端で格納される
      auto const  name  = std::string{reinterpret_cast<char const*>(entry + 2)};
      result.emplace_back(name, idx);
    }
    return result;
  }

  /// @brief コンパイル済みパターンのバイトサイズを返す
  auto pattern_size() const noexcept -> size_t {
    if (not code) return 0uz;
    auto sz = size_t{};
    std::ignore = pcre2_pattern_info(code, PCRE2_INFO_SIZE, &sz);
    return sz;
  }

  /// @brief JIT コンパイル済みコードのバイトサイズを返す（JIT 無効時は 0）
  auto jit_size() const noexcept -> size_t {
    if (not code) return 0uz;
    auto sz = size_t{};
    std::ignore = pcre2_pattern_info(code, PCRE2_INFO_JITSIZE, &sz);
    return sz;
  }

  /// @brief コンパイル時に有効だったオプションフラグを返す
  auto options() const noexcept -> uint32_t {
    if (not code) return 0u;
    auto opts = uint32_t{};
    std::ignore = pcre2_pattern_info(code, PCRE2_INFO_ALLOPTIONS, &opts);
    return opts;
  }

  // ================================================================
  // E3 / E10: マッチ制限・オフセット制限
  // ================================================================

  /// @brief マッチ再帰回数の上限を設定する（バックトラック制限）
  auto set_match_limit(uint32_t const limit) -> context& {
    ensure_match_context();
    pcre2_set_match_limit(match_ctx, limit);
    return *this;
  }

  /// @brief バックトラックスタック深度の上限を設定する
  auto set_depth_limit(uint32_t const limit) -> context& {
    ensure_match_context();
    pcre2_set_depth_limit(match_ctx, limit);
    return *this;
  }

  /// @brief ヒープメモリ使用量の上限を設定する（キロバイト単位）
  auto set_heap_limit(uint32_t const limit) -> context& {
    ensure_match_context();
    pcre2_set_heap_limit(match_ctx, limit);
    return *this;
  }

  /// @brief マッチ検索のオフセット上限を設定する（バイト単位）
  auto set_offset_limit(size_t const limit) -> context& {
    ensure_match_context();
    pcre2_set_offset_limit(match_ctx, static_cast<PCRE2_SIZE>(limit));
    return *this;
  }

  /**
   * @brief 文字列に対して正規表現を検索し、マッチ結果を低レベルな pcre2_match_data に格納する
   *
   * @param target 検索対象の文字列
   * @param data マッチデータを格納するバッファ
   * @param start 検索開始位置
   * @param option 検索オプション
   * @return std::expected<int, std::string> マッチしたグループ数、マッチしなかった場合は0、エラーの場合はエラーメッセージを返す
   */
  auto find(std::string_view const target, pcre2_match_data* data, size_t const start = 0uz, unsigned int const option = 0) const -> std::expected<int, std::string> {
    if (not code) {
      return std::unexpected{"Not compiled."};
    }
    if (not data) {
      return std::unexpected{"Match data is null."};
    }
    auto const rc = [&] -> int {
      if constexpr (UseJIT) {
        if (jit_size_ > 0) [[likely]] {
          return pcre2_jit_match(code, reinterpret_cast<PCRE2_SPTR8>(target.data()), target.size(), start, option, data, match_ctx);
        }
        // JIT が利用可能でなかった場合は Interpreter にフォールバック
      }
      return pcre2_match(code, reinterpret_cast<PCRE2_SPTR8>(target.data()), target.size(), start, option, data, match_ctx);
    }();
    if (rc >= 0) {
      return rc;
    }
    if (rc == PCRE2_ERROR_NOMATCH) {
      return 0;
    }
    // PCRE2 エラーコードとメッセージを含めてエラーを返す
    auto msg = std::array<PCRE2_UCHAR8, 256uz>{};
    pcre2_get_error_message(rc, msg.data(), msg.size());
    return std::unexpected{"Match error at code " + std::to_string(rc) + ": " + reinterpret_cast<char const*>(msg.data())};
  }

  /**
   * @brief 文字列に対して正規表現を検索し、マッチ結果をmatch_resultオブジェクトに格納する
   *
   * @param target 検索対象の文字列
   * @param mr マッチ結果を格納するオブジェクト
   * @param start 検索開始位置
   * @param option 検索オプション (PCRE2_NOTEMPTYなど)
   * @return std::expected<int, std::string> マッチしたグループ数、マッチしなかった場合は0、エラーの場合はエラーメッセージを返す
   */
  auto find(std::string_view const target, match_result& mr, size_t const start = 0uz, unsigned int const option = 0) const -> std::expected<int, std::string> {
    if (not mr.holder || not mr.holder->data) {
      return std::unexpected{"match_result not initialized."};
    }
    mr.set_target(target);
    return find(target, mr.get_data(), start, option);
  }

  /**
   * @brief スレッドローカルなバッファを再利用して検索を行う（マッチ結果はコピーされる）
   */
  auto find(std::string_view const target, use_tls_t, size_t const start = 0uz, unsigned int const option = 0) const -> std::expected<match_result, std::string> {
    if (not code) {
      return std::unexpected{"Not compiled."};
    }
    auto* md = detail::get_tls_match_data(code);
    auto const rc = find(target, md, start, option);
    if (not rc) {
      return std::unexpected{rc.error()};
    }
    if (*rc <= 0) {
      return match_result{};
    }
    return match_result{code, md, target};
  }

  /**
   * @brief 文字列に対して正規表現を検索し、最初のマッチ結果を返す
   *
   * @param target 検索対象の文字列
   * @param start 検索開始位置
   * @param option 検索オプション (PCRE2_NOTEMPTYなど)
   * @return std::expected<match_result, std::string> マッチした場合はmatch_result、マッチしなかった場合は空のmatch_result、エラーの場合はエラーメッセージを返す
   */
  auto find(std::string_view const target, size_t const start = 0uz, unsigned int const option = 0) const -> std::expected<match_result, std::string> {
    auto mr = match_result{*this};
    auto const rc = find(target, mr, start, option);
    if (not rc) {
      return std::unexpected{rc.error()};
    }
    if (*rc <= 0) {
      return match_result{};
    }
    return mr;
  }

  /**
   * @brief 完全一致するかどうかを判定する便利メソッド
   *
   * mr が未初期化の場合は自動で初期化してから検索する。
   * JIT は PCRE2_ENDANCHORED をサポートしないため、マッチ終端位置を手動検証する。
   *
   * @param target 判定対象の文字列
   * @param mr マッチ結果を格納するオブジェクト（未初期化でも可）
   * @param option マッチオプション
   * @return std::expected<bool, std::string> 完全一致する場合はtrue、それ以外はfalse
   */
  auto match(std::string_view const target, match_result& mr, unsigned int const option = 0) const -> std::expected<bool, std::string> {
    if (not mr.holder || not mr.holder->data) {
      mr = match_result{*this};
    }
    // PCRE2_ANCHORED で先頭固定。JIT は PCRE2_ENDANCHORED 非対応のため終端を手動検証する
    auto const rc = find(target, mr, 0uz, option | PCRE2_ANCHORED);
    if (not rc) {
      return std::unexpected{rc.error()};
    }
    if (*rc <= 0) {
      return false;
    }
    return mr.end_pos() == target.size();
  }

  /**
   * @brief 文字列置換
   *
   * @param target 置換対象の文字列
   * @param replacement 置換後の文字列
   * @param option 置換オプション
   * @return std::expected<std::string, std::string> 置換後の文字列
   */
  auto replace(std::string_view const target, std::string_view const replacement, unsigned int const option = PCRE2_SUBSTITUTE_GLOBAL) const -> std::expected<std::string, std::string> {
    if (not code) {
      return std::unexpected{"Not compiled."};
    }
    auto outlen = target.size() + (target.size() / 5uz) + replacement.size() + 256uz;
    auto buffer = std::string(outlen, '\0');
    auto blen   = outlen;
    auto constexpr overflow_retry_flag = PCRE2_SUBSTITUTE_OVERFLOW_LENGTH;
    auto const substitute_option       = option | overflow_retry_flag;
    auto const rc = pcre2_substitute(
      code,
      reinterpret_cast<PCRE2_SPTR8>(target.data()), target.size(),
      0uz,
      substitute_option,
      nullptr,
      nullptr,
      reinterpret_cast<PCRE2_SPTR8>(replacement.data()), replacement.size(),
      reinterpret_cast<PCRE2_UCHAR8*>(buffer.data()),
      &blen
    );
    if (rc >= 0) {
      buffer.resize(blen);
      return buffer;
    }
    // バッファが足りない場合は必要なサイズをもとに再試行
    auto   fail_code = rc;
    if (rc == PCRE2_ERROR_NOMEMORY) {
      buffer.resize(blen);
      auto const rc2 = pcre2_substitute(
        code,
        reinterpret_cast<PCRE2_SPTR8>(target.data()), target.size(),
        0uz,
        substitute_option,
        nullptr,
        nullptr,
        reinterpret_cast<PCRE2_SPTR8>(replacement.data()), replacement.size(),
        reinterpret_cast<PCRE2_UCHAR8*>(buffer.data()),
        &blen
      );
      if (rc2 >= 0) {
        buffer.resize(blen);
        return buffer;
      }
      fail_code = rc2;
    }
    auto err_msg = std::array<PCRE2_UCHAR8, 256uz>{};
    pcre2_get_error_message(fail_code, err_msg.data(), err_msg.size());
    return std::unexpected{"Substitute error (code " + std::to_string(fail_code) + "): " + reinterpret_cast<char const*>(err_msg.data())};
  }

  /**
   * @brief ラムダ式を用いた動的置換 (C7: expected 統一版)
   * @param target 対象文字列
   * @param callback マッチ結果を受け取り置換後の文字列を返す関数
   * @return std::expected<std::string, std::string> 置換後の文字列またはエラー
   */
  template <typename F>
    requires std::invocable<F, match_result const&>
  auto replace(std::string_view const target, F&& callback) const -> std::expected<std::string, std::string> {
    if (not code) {
      return std::unexpected{"Not compiled."};
    }
    auto result = std::string{};
    result.reserve(target.size() + (target.size() / 5uz) + 256uz);

    auto append_pos = 0uz;
    auto search_pos = 0uz;
    auto mr         = match_result{*this};
    while (true) {
      auto const rc = find(target, mr, search_pos, 0);
      if (not rc) {
        return std::unexpected{rc.error()};
      }
      if (*rc <= 0) {
        break;
      }
      auto const start = mr.start_pos();
      auto const end   = mr.end_pos();
      result.append(target.substr(append_pos, start - append_pos));
      result.append(callback(mr));
      append_pos = end;
      search_pos = (start == end) ? end + 1uz : end;
      if (search_pos > target.size()) {
        break;
      }
    }
    if (append_pos < target.size()) {
      result.append(target.substr(append_pos));
    }
    return result;
  }

  /**
   * @brief ラムダ式を用いた動的置換 (throw 版)
   *
   * `replace(target, callback)` の expected を unwrap して返す便利版。
   * コンパイルエラーなど内部エラー発生時は std::runtime_error を送出する。
   *
   * @param target 対象文字列
   * @param callback マッチ結果を受け取り置換後の文字列を返す関数
   * @return 置換後の文字列
   */
  template <typename F>
    requires std::invocable<F, match_result const&>
  auto replace_unchecked(std::string_view const target, F&& callback) const -> std::string {
    auto res = replace(target, std::forward<F>(callback));
    if (not res) {
      throw std::runtime_error{res.error()};
    }
    return std::move(*res);
  }

  /**
   * @brief 全てのマッチ箇所をイテレートするためのrangeを返す
   *
   * @param target 検索対象の文字列
   * @param option 検索オプション (PCRE2_NOTBOL 等)
   * @param start 検索開始バイト位置（デフォルト 0）
   * @return match_range<UseJIT>
   */
  auto find_all(std::string_view const target, unsigned int const option = 0, size_t const start = 0uz) const -> match_range<UseJIT> {
    auto range = match_range<UseJIT>{};
    range.first = iterator<UseJIT>(this, target, start, option, false);
    range.first.error_ref = &range.m_error;
    range.last  = iterator<UseJIT>(this, target, start, option, true);
    range.last.error_ref  = &range.m_error;
    return range;
  }

  /**
   * @brief 正規表現を区切り文字列として文字列を分割する
   *
   * @param target 分割対象の文字列
   * @param option 検索オプション (PCRE2_CASELESS 等)
   * @return std::vector<std::string_view> 分割された文字列。区切りがなければ target 全体が 1 要素
   */
  auto split(std::string_view const target, unsigned int const option = 0) const -> std::vector<std::string_view> {
    auto res  = std::vector<std::string_view>{};
    auto last = 0uz;
    for (auto& mr : find_all(target, option)) {
      res.push_back(target.substr(last, mr.start_pos() - last));
      last = mr.end_pos();
    }
    res.push_back(target.substr(last));
    return res;
  }

  /**
   * @brief 完全一致するかどうかを判定する便利メソッド（match_result 不要版）
   *
   * @param target 判定対象の文字列
   * @param option マッチオプション
   * @return std::expected<bool, std::string> 完全一致する場合はtrue
   */
  auto match(std::string_view const target, unsigned int const option = 0) const -> std::expected<bool, std::string> {
    auto mr = match_result{*this};
    return match(target, mr, option);
  }

#if PCREPP_HAS_GENERATOR
  /**
   * @brief split() の lazy view 版（C++23 std::generator を使用）
   *
   * vector を確保せず、大量分割時に省メモリ。
   * @param target 分割対象の文字列
   * @param option 検索オプション
   */
  auto split_view(std::string_view const target, unsigned int const option = 0) const
    -> std::generator<std::string_view> {
    auto last = 0uz;
    for (auto& mr : find_all(target, option)) {
      co_yield target.substr(last, mr.start_pos() - last);
      last = mr.end_pos();
    }
    co_yield target.substr(last);
  }
#endif
};

/**
 * @brief NTTP パターンのキャプチャグループ数を取得する constexpr 変数
 *
 * パターンのキャプチャグループ数に +1 したもの（全体マッチ分を含む）
 *
 * @tparam Pattern NTTP として指定された正規表現パターン
 */
template <fixed_string Pattern>
inline constexpr auto nttp_group_count_v = detail::count_capture_groups(Pattern.view()) + 1uz;

/**
 * @brief NTTP 版 find の戻り値オブジェクト
 *
 * bool 変換・構造化束縛・`get<index>()`・`get<"name">()` をサポートします。
 *
 * @tparam Pattern NTTP として指定された正規表現パターン
 * @tparam UseJIT JIT コンパイルを使用するか
 */
template <fixed_string Pattern, bool UseJIT = true>
struct nttp_match_result {
  bool matched = false;
  std::array<std::string_view, nttp_group_count_v<Pattern>> groups{};

  explicit operator bool() const noexcept {
    return matched;
  }

  template <size_t Index>
  auto get() const noexcept {
    static_assert(Index < (nttp_group_count_v<Pattern> + 1uz));
    if constexpr (Index == 0uz) {
      return matched;
    } else {
      return groups[Index - 1uz];
    }
  }

  template <fixed_string Name>
  auto get() const -> std::string_view;
};

/**
 * @brief NTTP 版 find/find_all の戻り値型エイリアス
 */
template <fixed_string Pattern, bool UseJIT = true>
using nttp_find_result_t = nttp_match_result<Pattern, UseJIT>;

namespace detail {
template <fixed_string Pattern, bool UseJIT, size_t... Is>
auto make_nttp_result_raw_impl(pcre2_match_data* md, std::string_view target, std::index_sequence<Is...>) -> nttp_find_result_t<Pattern, UseJIT> {
  auto const* ovector = pcre2_get_ovector_pointer(md);
  auto const  count   = pcre2_get_ovector_count(md);
  auto const  get_view = [&](size_t const i) -> std::string_view {
    if (i >= count) {
      return {};
    }
    auto const s = ovector[i * 2uz + 0uz];
    auto const e = ovector[i * 2uz + 1uz];
    if (s == PCRE2_UNSET || e == PCRE2_UNSET) {
      return {};
    }
    return target.substr(s, e - s);
  };
  return {.matched = true, .groups = {get_view(Is)...}};
}

template <fixed_string Pattern, bool UseJIT>
auto make_nttp_result_raw(pcre2_match_data* md, std::string_view target) -> nttp_find_result_t<Pattern, UseJIT> {
  return make_nttp_result_raw_impl<Pattern, UseJIT>(md, target, std::make_index_sequence<nttp_group_count_v<Pattern>>{});
}

/**
 * @brief NTTP パターンに対応するコンパイル済み context への参照を返す
 *
 * パターンをキーにプロセス内で 1 度だけコンパイルし、その後は同じ static
 * インスタンスを共有する。コンパイル失敗時は std::runtime_error を送出する。
 * `find<Pattern>` / `find_all<Pattern>` / `nttp_match_result::get<Name>` /
 * `nttp_regex<Pattern>::match` から共通利用される。
 */
/**
 * @brief パターン文字列に非 ASCII 文字が含まれていれば PCRE2_UTF を返す。
 *
 * NTTP 版 (`get_nttp_context`) と Runtime 版 (`context_create_utf`) で
 * 「非 ASCII パターン → 自動的に PCRE2_UTF を付与」というポリシーを
 * 統一するためのヘルパ。`constexpr` なので NTTP の `compile_opt` 計算にも使える。
 */
inline constexpr auto auto_utf_options(std::string_view const pattern) noexcept -> unsigned int {
  for (auto const c : pattern) {
    if (static_cast<unsigned char>(c) > 0x7F) {
      return PCRE2_UTF;
    }
  }
  return 0;
}

#ifdef WITH_CTRE
/**
 * @brief パターンが CTRE で処理すべきか constexpr 判定
 *
 * CTRE は可変長 lookbehind (`(?<=…{2,})` / `(?<!…+)`) のように
 * PCRE2 がコンパイルできないパターンを処理するために使う。
 * 高速化のための委譲は行わない (PCRE2 の JIT 最適化の方が十分速い)。
 *
 * CTRE 推奨条件:
 * - `(?<=...quantifier)` / `(?<!...quantifier)`: CTRE 推奨 (可変長 lookbehind)
 *
 * PCRE2 強制 (CTRE 非対応):
 * - `\1`-`\9` : 後方参照
 * - `(?R)` / `(?&` / `(?P>` / `(?0)` : 再帰
 * - `(?(` : 条件分岐
 */
constexpr auto ctre_recommended(std::string_view const pattern) noexcept -> bool {
  auto in_class    = false;
  auto in_literal  = false;
  auto in_comment  = false;
  auto in_verb     = false;
  auto escaped     = false;
  auto class_start = false;
  auto comment_parens = 0uz;
  auto verb_parens    = 0uz;

  auto depth                = 0uz;
  auto in_lookbehind        = false;
  auto has_var_lookbehind   = false;
  auto has_backref_or_recurse = false;

  for (auto i = 0uz; i < pattern.size(); ++i) {
    auto const ch = pattern[i];

    // (?#comment) 内
    if (in_comment) {
      if (escaped) { escaped = false; continue; }
      if (ch == '\\') { escaped = true; continue; }
      if (ch == '(') { ++comment_parens; continue; }
      if (ch == ')') {
        if (comment_parens == 0uz) { in_comment = false; }
        else { --comment_parens; }
      }
      continue;
    }

    // (*VERB) 内
    if (in_verb) {
      if (escaped) { escaped = false; continue; }
      if (ch == '\\') { escaped = true; continue; }
      if (ch == '(') { ++verb_parens; continue; }
      if (ch == ')') {
        if (verb_parens == 0uz) { in_verb = false; }
        else { --verb_parens; }
      }
      continue;
    }

    // \Q...\E 内
    if (in_literal) {
      if (ch == '\\' && i + 1uz < pattern.size() && pattern[i + 1uz] == 'E') {
        ++i; in_literal = false;
      }
      continue;
    }

    // [...] 内
    if (in_class) {
      if (escaped) { escaped = false; class_start = false; continue; }
      if (ch == '\\') {
        class_start = false;
        if (i + 1uz < pattern.size() && pattern[i + 1uz] == 'Q') {
          in_literal = true;
          ++i;
          continue;
        }
        escaped = true;
        continue;
      }
      if (ch == ']' && !class_start) { in_class = false; }
      class_start = false;
      continue;
    }

    // 通常モード
    if (escaped) {
      escaped = false;
      if (!has_backref_or_recurse && ch >= '1' && ch <= '9') {
        has_backref_or_recurse = true;
      }
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    if (ch == '[') {
      in_class = true;
      class_start = true;
      if (i + 1uz < pattern.size() && pattern[i + 1uz] == '^') { ++i; }
      continue;
    }

    // 量化子が可変長 lookbehind 内にある → CTRE 推奨
    if (ch == '*' || ch == '+' || ch == '?') {
      if (in_lookbehind) {
        has_var_lookbehind = true;
      }
      continue;
    }
    if (ch == '{') {
      if (in_lookbehind) {
        for (auto j = i + 1uz; j < pattern.size(); ++j) {
          auto const nc = pattern[j];
          if (nc == '}') break;
          if (nc == ',') { has_var_lookbehind = true; break; }
          if (nc < '0' || nc > '9') break;
        }
      }
      continue;
    }

    // 再帰・条件判定
    if (ch == '(' && i + 1uz < pattern.size()) {
      auto const next = pattern[i + 1uz];
      if (next == '?') {
        if (i + 2uz < pattern.size()) {
          auto const m2 = pattern[i + 2uz];
          if (m2 == 'R') {
            has_backref_or_recurse = true;
          } else if (m2 == '&') {
            has_backref_or_recurse = true;
          } else if (m2 == 'P' && i + 3uz < pattern.size() && pattern[i + 3uz] == '>') {
            has_backref_or_recurse = true;
          } else if (m2 == '0') {
            has_backref_or_recurse = true;
          } else if (m2 == '(') {
            has_backref_or_recurse = true;
          } else if (m2 == '<') {
            if (i + 3uz < pattern.size()) {
              auto const m3 = pattern[i + 3uz];
              if (m3 == '=' || m3 == '!') {
                in_lookbehind = true;
              }
            }
          }
        }
      } else if (next == '*') {
        in_verb = true;
        verb_parens = 0uz;
        ++i;
        continue;
      }

      ++depth;
      continue;
    }

    if (ch == ')') {
      if (in_lookbehind) {
        in_lookbehind = false;
      }
      if (depth > 0uz) {
        --depth;
      }
      continue;
    }
  }

  if (has_var_lookbehind && !has_backref_or_recurse) {
    return true;
  }
  return false;
}

/**
 * @brief NTTP パターンが CTRE を使うべきかを示す constexpr 変数
 */
template <fixed_string Pattern>
inline constexpr auto use_ctre_for_pattern_v = ctre_recommended(Pattern.view());

/**
 * @brief CTRE の match result を nttp_match_result に変換
 */
template <fixed_string Pattern, bool UseJIT>
auto ctre_to_nttp_result(auto const& m) -> nttp_match_result<Pattern, UseJIT> {
  nttp_match_result<Pattern, UseJIT> result;
  result.matched = true;
  [&]<size_t... Is>(std::index_sequence<Is...>) {
    ((result.groups[Is] = static_cast<std::string_view>(m.template get<Is>())), ...);
  }(std::make_index_sequence<nttp_group_count_v<Pattern>>{});
  return result;
}

/**
 * @brief CTRE の match result を tuple (find_all 用) に変換
 */
template <typename Match, size_t... Is>
auto ctre_to_tuple_impl(Match const& m, std::index_sequence<Is...>) {
  return std::make_tuple(static_cast<std::string_view>(m.template get<Is>())...);
}

template <fixed_string Pattern>
auto ctre_match_to_tuple(auto const& m) {
  return ctre_to_tuple_impl(m, std::make_index_sequence<nttp_group_count_v<Pattern>>{});
}

/**
 * @brief pcrepp::fixed_string を ctll::fixed_string に変換 (consteval)
 *
 * CTRE のテンプレート引数としてパターンを渡すためのブリッジ関数。
 */
template <fixed_string Pattern>
consteval auto to_ctre_pattern() {
  constexpr auto sv = Pattern.view();
  return ctll::fixed_string<Pattern.length>{ctll::construct_from_pointer, sv.data()};
}
#endif  // WITH_CTRE

template <fixed_string Pattern, bool UseJIT = true>
inline auto get_nttp_context() -> context<UseJIT> const& {
  static auto const ctx_res = context<UseJIT>::create(Pattern.view(), auto_utf_options(Pattern.view()));
  if (not ctx_res) {
    throw std::runtime_error{"NTTP context compile error: " + ctx_res.error()};
  }
  return *ctx_res;
}
}  // namespace detail

/**
 * @brief nttp_match_result::get<Name>() の実装 (get_nttp_context 利用)
 */
template <fixed_string Pattern, bool UseJIT>
template <fixed_string Name>
auto nttp_match_result<Pattern, UseJIT>::get() const -> std::string_view {
  if (not matched) {
    return {};
  }
  auto const& ctx = detail::get_nttp_context<Pattern, UseJIT>();
  auto const  index = ctx.capture_index(Name.view());
  if (index < 0) {
    return {};
  }
  auto const uindex = static_cast<size_t>(index);
  if (uindex >= groups.size()) {
    return {};
  }
  return groups[uindex];
}

/**
 * @brief nttp_match_result を構造化束縛可能にする get（tuple-like プロトコル）
 */
template <size_t Index, fixed_string Pattern, bool UseJIT>
auto get(nttp_match_result<Pattern, UseJIT> const& result) noexcept {
  static_assert(Index < (nttp_group_count_v<Pattern> + 1uz));
  return result.template get<Index>();
}

/**
 * @brief NTTP 版 find：正規表現をテンプレート引数で指定する検索 (C7: expected 版)
 *
 * 与えられたパターンで最初のマッチを検索し、結果を expected で返します。
 * パターンコンパイルやマッチ実行で失敗した場合は std::unexpected を返します。
 *
 * @see find_unchecked 例外版
 */
template <fixed_string Pattern, bool UseJIT = true>
auto find(std::string_view const target, size_t const start = 0uz, unsigned int const option = 0)
  -> std::expected<nttp_find_result_t<Pattern, UseJIT>, std::string> {
#ifdef WITH_CTRE
  if constexpr (detail::use_ctre_for_pattern_v<Pattern>) {
    constexpr auto cp = detail::to_ctre_pattern<Pattern>();
    auto const sv = target.substr(start);
    if (auto m = ctre::search<cp>(sv)) {
      return detail::ctre_to_nttp_result<Pattern, UseJIT>(m);
    }
    return nttp_find_result_t<Pattern, UseJIT>{};
  } else
#endif
  {
    // コンパイルエラーを例外ではなく unexpected として返す
    context<UseJIT> const* ctx_ptr = nullptr;
    try {
      ctx_ptr = &detail::get_nttp_context<Pattern, UseJIT>();
    } catch (std::runtime_error const& e) {
      return std::unexpected{std::string{e.what()}};
    }
    auto*      md  = detail::get_tls_match_data(ctx_ptr->get_code());
    auto const res = ctx_ptr->find(target, md, start, option);
    if (not res) {
      return std::unexpected{res.error()};
    }
    if (*res <= 0) {
      return nttp_find_result_t<Pattern, UseJIT>{};
    }
    return detail::make_nttp_result_raw<Pattern, UseJIT>(md, target);
  }
}

/**
 * @brief NTTP 版 find の throw 版
 *
 * `find<Pattern>()` の expected を unwrap して返す便利版。
 * エラー発生時は std::runtime_error を送出する。
 */
template <fixed_string Pattern, bool UseJIT = true>
auto find_unchecked(std::string_view const target, size_t const start = 0uz, unsigned int const option = 0)
  -> nttp_find_result_t<Pattern, UseJIT> {
  auto res = find<Pattern, UseJIT>(target, start, option);
  if (not res) {
    throw std::runtime_error{"NTTP find error: " + res.error()};
  }
  return std::move(*res);
}

/**
 * @brief NTTP 版 find_all：正規表現をテンプレート引数で指定する全マッチ検索
 */
template <fixed_string Pattern, bool UseJIT = true>
auto find_all(std::string_view const target, unsigned int const option = 0) {
#ifdef WITH_CTRE
  if constexpr (detail::use_ctre_for_pattern_v<Pattern>) {
    constexpr auto cp = detail::to_ctre_pattern<Pattern>();
    return ctre::search_all<cp>(target) | std::views::transform([](auto const& m) {
      return detail::ctre_match_to_tuple<Pattern>(m);
    });
  } else
#endif
  {
    auto const& ctx = detail::get_nttp_context<Pattern, UseJIT>();
    return ctx.find_all(target, option) | std::views::transform([](auto const& mr) {
      return detail::match_result_to_tuple<nttp_group_count_v<Pattern>>(mr);
    });
  }
}

/**
 * @brief NTTP 版正規表現オブジェクト
 *
 * テンプレート引数で指定された正規表現のコンパイル結果を管理します。
 * インスタンス化は軽量で、内部でコンパイル結果がキャッシュされます。
 */
template <fixed_string Pattern, bool UseJIT = true>
struct nttp_regex {
  constexpr nttp_regex() = default;

  /**
   * @brief 最初のマッチを検索
   * @param target 検索対象の文字列
   * @param start 検索開始バイト位置
   * @param option マッチオプション
   * @return std::expected<nttp_match_result<Pattern, UseJIT>, std::string>
   */
  auto find(std::string_view const target, size_t const start = 0uz, unsigned int const option = 0) const {
    return pcrepp::find<Pattern, UseJIT>(target, start, option);
  }

  /**
   * @brief 全てのマッチを検索
   * @param target 検索対象の文字列
   * @param option マッチオプション
   * @return std::vector<detail::nttp_match_tuple_t<nttp_group_count_v<Pattern>>>
   * @note find_all は戻り値が [whole, g1, ...]（N+1 要素）
   */
  auto find_all(std::string_view const target, unsigned int const option = 0) const {
    return pcrepp::find_all<Pattern, UseJIT>(target, option);
  }

  /**
   * @brief 完全一致を確認 (C7: expected 版)
   * @param target 判定対象の文字列
   * @param option マッチオプション
   * @return std::expected<bool, std::string> 完全一致するか、エラーメッセージ
   */
  auto match(std::string_view const target, unsigned int const option = 0) const -> std::expected<bool, std::string> {
    context<UseJIT> const* ctx_ptr = nullptr;
    try {
      ctx_ptr = &detail::get_nttp_context<Pattern, UseJIT>();
    } catch (std::runtime_error const& e) {
      return std::unexpected{std::string{e.what()}};
    }
    auto mr = match_result{*ctx_ptr};
    return ctx_ptr->match(target, mr, option);
  }

  /**
   * @brief 文字列置換（F14: context に委譲）
   * @param target 置換対象の文字列
   * @param replacement 置換後の文字列
   * @param option 置換オプション
   * @return std::expected<std::string, std::string> 置換結果またはエラーメッセージ
   */
  auto replace(std::string_view const target, std::string_view const replacement,
               unsigned int const option = PCRE2_SUBSTITUTE_GLOBAL) const
    -> std::expected<std::string, std::string> {
    context<UseJIT> const* ctx_ptr = nullptr;
    try { ctx_ptr = &detail::get_nttp_context<Pattern, UseJIT>(); }
    catch (std::runtime_error const& e) { return std::unexpected{std::string{e.what()}}; }
    return ctx_ptr->replace(target, replacement, option);
  }

  /**
   * @brief コールバック置換（F14: context に委譲）
   * @param target 置換対象の文字列
   * @param callback マッチ結果を受け取り置換後文字列を返す関数
   * @return std::expected<std::string, std::string> 置換結果またはエラーメッセージ
   */
  template <typename F>
    requires std::invocable<F, match_result const&>
  auto replace(std::string_view const target, F&& callback) const
    -> std::expected<std::string, std::string> {
    context<UseJIT> const* ctx_ptr = nullptr;
    try { ctx_ptr = &detail::get_nttp_context<Pattern, UseJIT>(); }
    catch (std::runtime_error const& e) { return std::unexpected{std::string{e.what()}}; }
    return ctx_ptr->replace(target, std::forward<F>(callback));
  }

  /**
   * @brief 文字列分割（F14: context に委譲）
   * @param target 分割対象の文字列
   * @param option マッチオプション
   * @return std::vector<std::string_view> 分割結果
   */
  auto split(std::string_view const target, unsigned int const option = 0) const
    -> std::vector<std::string_view> {
    return detail::get_nttp_context<Pattern, UseJIT>().split(target, option);
  }
};

/**
 * @brief NTTP 版正規表現のコンパイル
 *
 * @tparam Pattern 正規表現パターン
 * @tparam UseJIT JITコンパイルを使用するか
 * @return nttp_regex<Pattern, UseJIT> 正規表現オブジェクト
 */
template <fixed_string Pattern, bool UseJIT = true>
constexpr auto compile() {
  return nttp_regex<Pattern, UseJIT>{};
}

/**
 * @brief リテラル演算子 _re
 *
 * "pattern"_re と記述することで NTTP 版正規表現オブジェクトを生成します。
 */
template <fixed_string Pattern>
constexpr auto operator""_re() {
  return compile<Pattern>();
}

/**
 * @brief NTTP 版: UTF 自動付与版 compile (E9)
 *
 * パターンに非 ASCII 文字が含まれていれば PCRE2_UTF を自動的に付与してコンパイルする。
 * プロセス内で 1 度だけコンパイルし、その後は同じ context を共有する。
 * 「ASCII 中心だが日本語も混じる」という一般的なケースで、PCRE2_UTF の指定忘れを防ぐ。
 *
 * ```cpp
 * auto const& ctx = pcrepp::compile_utf<R"((?<city>[\w\W]+)の(?<item>[\w\W]+))">();
 * pcrepp::match_result mr{ctx};
 * if (ctx.find("東京のラーメン", mr)) {
 *   auto const city = mr["city"]; // PCRE2_UTF 適用済みなので \w が日本語にマッチ
 * }
 * ```
 *
 * @note context はコピー禁止のため参照を返す。
 */
template <fixed_string Pattern, bool UseJIT = true>
inline auto compile_utf() -> context<UseJIT> const& {
  return detail::get_nttp_context<Pattern, UseJIT>();
}

/**
 * @brief Runtime 版: UTF 自動付与版 context::create (E9)
 *
 * `pattern` に非 ASCII 文字が含まれていれば PCRE2_UTF を自動的に付与する。
 * NTTP 版と Runtime 版で「非 ASCII パターン → 自動的に PCRE2_UTF」という
 * ポリシーを統一するためのヘルパ。
 */
inline auto context_create_utf(std::string_view const src, unsigned int const extra_option = 0)
  -> std::expected<context<true>, std::string> {
  return context<true>::create(src, detail::auto_utf_options(src) | extra_option);
}

template <bool UseJIT, unsigned JITFlags>
inline auto context_create_utf(std::string_view const src, unsigned int const extra_option = 0)
  -> std::expected<context<UseJIT, JITFlags>, std::string> {
  return context<UseJIT, JITFlags>::create(src, detail::auto_utf_options(src) | extra_option);
}

// frozenchars ライブラリが利用可能な場合、テンプレート引数に指定可能にする
#ifdef PCREPP_HAS_FROZENCHARS
/**
 * @brief frozenchars::FrozenString 版 find（明示API）
 */
template <frozenchars::FrozenString Pattern, bool UseJIT = true>
auto find_frozen(std::string_view const target, size_t const start = 0uz, unsigned int const option = 0) {
  return find<to_fixed_string(Pattern), UseJIT>(target, start, option);
}

/**
 * @brief frozenchars::FrozenString 版 find_all（明示API）
 */
template <frozenchars::FrozenString Pattern, bool UseJIT = true>
auto find_all_frozen(std::string_view const target, unsigned int const option = 0) {
  return find_all<to_fixed_string(Pattern), UseJIT>(target, option);
}

/**
 * @brief frozenchars::FrozenString 版 compile（明示API）
 */
template <frozenchars::FrozenString Pattern, bool UseJIT = true>
constexpr auto compile_frozen() {
  return compile<to_fixed_string(Pattern), UseJIT>();
}

#if PCREPP_ENABLE_FROZENCHARS_NTTP_OVERLOADS
/**
 * @brief frozenchars::FrozenString 版 find（同名オーバーロード）
 */
template <frozenchars::FrozenString Pattern, bool UseJIT = true>
auto find(std::string_view const target, size_t const start = 0uz, unsigned int const option = 0) {
  return find_frozen<Pattern, UseJIT>(target, start, option);
}

/**
 * @brief frozenchars::FrozenString 版 find_all（同名オーバーロード）
 */
template <frozenchars::FrozenString Pattern, bool UseJIT = true>
auto find_all(std::string_view const target, unsigned int const option = 0) {
  return find_all_frozen<Pattern, UseJIT>(target, option);
}

/**
 * @brief frozenchars::FrozenString 版 compile（同名オーバーロード）
 */
template <frozenchars::FrozenString Pattern, bool UseJIT = true>
constexpr auto compile() {
  return compile_frozen<Pattern, UseJIT>();
}
#endif
#endif // PCREPP_HAS_FROZENCHARS

template <bool UseJIT, unsigned JITFlags>
inline match_result::match_result(context<UseJIT, JITFlags> const& ctx) : match_result(ctx.code) {}

template <bool UseJIT, unsigned JITFlags>
inline match_result::match_result(context<UseJIT, JITFlags> const& ctx, size_t oversized_capture_count) {
  if (ctx.code && oversized_capture_count > 0uz) {
    auto* data = pcre2_match_data_create(static_cast<uint32_t>(oversized_capture_count), nullptr);
    if (data) {
      holder = std::make_shared<data_holder>(ctx.code, data);
    }
  }
}

template <bool UseJIT>
inline iterator<UseJIT>::iterator(context<UseJIT> const* c, std::string_view t, size_t p, unsigned int o, bool end,
                                   std::string* err) : ctx(c), target(t), pos(p), option(o), is_end(end), error_ref(err) {
  if (!is_end && ctx) {
    result = match_result(*ctx);
    if (auto const rc = ctx->find(target, result, pos, option); not rc || *rc <= 0) {
      if (not rc && error_ref) {
        *error_ref = rc.error();
      }
      is_end = true;
    }
  }
}

template <bool UseJIT>
inline auto iterator<UseJIT>::operator++() -> iterator& {
  if (ctx && not is_end) {
    auto const prev_start = result.start_pos();
    auto const prev_end   = result.end_pos();
    auto const next_pos   = (prev_start == prev_end) ? prev_end + 1uz : prev_end;
    pos = next_pos;
    if (pos > target.size()) {
      is_end = true;
      return *this;
    }
    if (auto const rc = ctx->find(target, result, pos, option); not rc || *rc <= 0) {
      if (not rc && error_ref) {
        *error_ref = rc.error();
      }
      is_end = true;
    }
  }
  return *this;
}

}  // namespace pcrepp

namespace std {
template <pcrepp::fixed_string Pattern, bool UseJIT>
struct tuple_size<pcrepp::nttp_match_result<Pattern, UseJIT>>
  : integral_constant<size_t, pcrepp::nttp_group_count_v<Pattern> + 1uz> {};

template <size_t Index, pcrepp::fixed_string Pattern, bool UseJIT>
struct tuple_element<Index, pcrepp::nttp_match_result<Pattern, UseJIT>> {
  static_assert(Index < pcrepp::nttp_group_count_v<Pattern> + 1uz);
  using type = conditional_t<Index == 0uz, bool, std::string_view>;
};
}  // namespace std

#if __has_include(<format>)
#include <format>

/**
 * @brief match_result の std::format 対応
 */
template <>
struct std::formatter<pcrepp::match_result> : std::formatter<std::string_view> {
  auto format(pcrepp::match_result const& res, std::format_context& ctx) const {
    if (not res) {
      return std::format_to(ctx.out(), "No Match");
    }
    std::format_to(ctx.out(), "[");
    for (auto i = 0uz; i < res.size(); ++i) {
      if (i > 0uz) {
        std::format_to(ctx.out(), ", ");
      }
      std::format_to(ctx.out(), "{}", res.get(i));
    }
    return std::format_to(ctx.out(), "]");
  }
};

/**
 * @brief nttp_match_result の std::format 対応（F8）
 *
 * 出力形式: "[matched, group0, group1, ...]" または "No Match"
 */
template <pcrepp::fixed_string Pattern, bool UseJIT>
struct std::formatter<pcrepp::nttp_match_result<Pattern, UseJIT>> : std::formatter<std::string_view> {
  auto format(pcrepp::nttp_match_result<Pattern, UseJIT> const& res, std::format_context& ctx) const {
    if (not res) {
      return std::format_to(ctx.out(), "No Match");
    }
    std::format_to(ctx.out(), "[matched");
    for (auto const& g : res.groups) {
      std::format_to(ctx.out(), ", {}", g);
    }
    return std::format_to(ctx.out(), "]");
  }
};

#endif
