#pragma once

#include <array>
#include <charconv>
#include <concepts>
#include <cstring>
#include <expected>
#include <iterator>
#include <memory>
#include <ranges>
#include <tuple>
#include <string>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <type_traits>
#include <vector>

// frozencharsのヘッダが存在する場合は型変換のためにインクルードする
#if __has_include(<frozenchars.hpp>)
#include <frozenchars.hpp>
#define PCREPP_HAS_FROZENCHARS
#elif __has_include(<frozenchars/frozenchars.hpp>)
#include <frozenchars/frozenchars.hpp>
#define PCREPP_HAS_FROZENCHARS
#endif

#include "fast_float/fast_float.h"

#define PCRE2_CODE_UNIT_WIDTH 8
#include "pcre2.h"

namespace pcrepp {

/**
 * @brief 高速化のためのオプション定数
 * バリデーション済みの文字列を扱う場合に、PCRE2 による UTF チェックをスキップできます。
 */
inline constexpr unsigned int no_utf_check = PCRE2_NO_UTF_CHECK;

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

  /**
   * @brief コンストラクタ：文字列リテラルから初期化
   */
  constexpr fixed_string(char const (&src)[N]) {
    std::ranges::copy(src, value.begin());
  }

#ifdef PCREPP_HAS_FROZENCHARS
  /**
   * @brief コンストラクタ：frozenchars::FrozenString から初期化
   */
  template <size_t M>
  constexpr fixed_string(frozenchars::FrozenString<M> const& src) {
    static_assert(M <= N, "FrozenString is too large");
    auto const s = src.sv();
    std::ranges::copy(s, value.begin());
    value[s.size()] = '\0';
  }
#endif

  /**
   * @brief 文字列を std::string_view に変換
   * @return null終端を除いた文字列ビュー
   */
  constexpr auto view() const noexcept -> std::string_view {
    return {value.data(), N - 1uz};
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
 * @brief `(?` の直後が名前付きキャプチャグループかを判定
 *
 * PCRE2 の以下の構文を識別します：
 * - `(?<name>...)` / `(?'name'...)` : 名前付きキャプチャ
 * - `(?P<name>...)` : Python形式の名前付きキャプチャ
 *
 * 次の非キャプチャ構文は false を返します：
 * - `(?:...)` : 非キャプチャグループ
 * - `(?=...)` / `(?!...)` : lookahead
 * - `(?<=...)` / `(?<!...)` : lookbehind
 * - `(?>...)` : atomic group
 *
 * @param pattern 正規表現パターン
 * @param open_paren_pos 開き括弧 '(' の位置
 * @return 名前付きキャプチャグループなら true、それ以外は false
 */
constexpr auto is_named_capture_after_question(std::string_view const pattern, size_t const open_paren_pos) noexcept -> bool {
  auto const q_pos = open_paren_pos + 1uz;
  if (q_pos >= pattern.size() || pattern[q_pos] != '?') {
    return false;
  }
  auto const marker_pos = q_pos + 1uz;
  if (marker_pos >= pattern.size()) {
    return false;
  }
  if (pattern[marker_pos] == '\'') {
    return true;
  }
  if (pattern[marker_pos] == 'P') {
    return marker_pos + 1uz < pattern.size() && pattern[marker_pos + 1uz] == '<';
  }
  if (pattern[marker_pos] == '<') {
    if (marker_pos + 1uz >= pattern.size()) {
      return false;
    }
    auto const next = pattern[marker_pos + 1uz];
    return next != '=' && next != '!';
  }
  return false;
}

/**
 * @brief 正規表現パターンのキャプチャグループ数を constexpr で計算
 *
 * 開き括弧を走査し、エスケープと文字クラスを考慮して、キャプチャグループ数を数えます。
 * 以下は加算されます：
 * - `(...)` : 通常のキャプチャグループ
 * - `(?<name>...)` / `(?'name'...)` / `(?P<name>...)` : 名前付きキャプチャ
 *
 * 以下は加算されません：
 * - `(?:...)` : 非キャプチャグループ
 * - `(?=...)` / `(?!...)` / `(?<=...)` / `(?<!...)` : lookaround
 * - `(?>...)` : atomic group
 * - `(?|...)` : branch reset group
 * - `(?#...)` : コメント
 * - エスケープされた括弧 `\(` / `\)`
 * - 文字クラス内の括弧 `[...]`
 *
 * @param pattern 正規表現パターン
 * @return キャプチャグループ数（ゼロ以上）
 */
constexpr auto count_capture_groups(std::string_view const pattern) noexcept -> size_t {
  auto capture_count = 0uz;
  auto escaped = false;
  auto in_class = false;
  auto class_start = false;

  for (auto i = 0uz; i < pattern.size(); ++i) {
    auto const ch = pattern[i];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    if (in_class) {
      if (ch == ']' && !class_start) {
        in_class = false;
      }
      class_start = false;
      continue;
    }
    if (ch == '[') {
      in_class = true;
      class_start = true;
      if (i + 1uz < pattern.size() && pattern[i + 1uz] == '^') {
        class_start = false; // [^] も [^...] と同様に最初の ] をリテラルとして扱う必要があるため、次の文字で判定する
        ++i;
        class_start = true;
      }
      continue;
    }
    if (ch != '(') {
      continue;
    }
    if (i + 1uz >= pattern.size() || pattern[i + 1uz] != '?') {
      ++capture_count;
      continue;
    }
    if (is_named_capture_after_question(pattern, i)) {
      ++capture_count;
    }
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
using nttp_match_tuple_t = decltype(std::tuple_cat(std::tuple<bool>{}, std::declval<string_view_tuple_t<N>>()));

/**
 * @brief マッチなしを表す空のタプルを生成（内部実装用）
 * @tparam N タプルの要素数
 * @tparam Is インデックスシーケンス
 */
template <size_t N, size_t... Is>
constexpr auto make_empty_match_tuple_impl(std::index_sequence<Is...>) noexcept -> nttp_match_tuple_t<N> {
  return std::tuple_cat(std::make_tuple(false), std::make_tuple((static_cast<void>(Is), std::string_view{})...));
}

/**
 * @brief マッチなしを表す空のタプルを生成
 *
 * マッチが失敗した場合に bool=false で初期化された全要素を返します。
 *
 * @tparam N タプルの要素数（bool + キャプチャ数）
 * @return bool(false) + N個の空の std::string_view を含むタプル
 */
template <size_t N>
constexpr auto make_empty_match_tuple() noexcept -> nttp_match_tuple_t<N> {
  return make_empty_match_tuple_impl<N>(std::make_index_sequence<N>{});
}

template <supported_integer_get_type T>
auto parse_integer(std::string_view const sv) noexcept -> T {
  auto value = T{};
  auto const [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), value);
  if (ec == std::errc{} && ptr == sv.data() + sv.size()) {
    return value;
  }
  return {};
}

template <typename T>
  requires(std::same_as<T, float> || std::same_as<T, double>)
auto parse_floating(std::string_view const sv) noexcept -> T {
  auto value = T{};
  auto const result = fast_float::from_chars(sv.data(), sv.data() + sv.size(), value);
  if (result.ec == std::errc{} && result.ptr == sv.data() + sv.size()) {
    return value;
  }
  return {};
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
}  // namespace detail

struct use_tls_t {};
inline constexpr auto use_tls = use_tls_t{};


template <bool UseJIT>
struct iterator;

template <bool UseJIT>
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
    std::string_view  target  = {};

    data_holder(pcre2_code const* c) : code(c) {
      if (code) {
        data    = pcre2_match_data_create_from_pattern(code, nullptr);
        ovector = pcre2_get_ovector_pointer(data);
      }
    }

    ~data_holder() {
      if (data) {
        pcre2_match_data_free(data);
      }
    }
  };
  std::shared_ptr<data_holder> holder;

  match_result() = default;
  template <bool UseJIT>
  match_result(context<UseJIT> const& ctx);

  match_result(match_result const& other) {
    if (other.holder && other.holder->code) {
      holder = std::make_shared<data_holder>(other.holder->code);
      auto const count = pcre2_get_ovector_count(other.holder->data);
      std::memcpy(holder->ovector, other.holder->ovector, sizeof(size_t) * count * 2uz);
      holder->target = other.holder->target;
    }
  }

  auto operator=(match_result const& other) -> match_result& {
    if (this != &other) {
      if (other.holder && other.holder->code) {
        holder = std::make_shared<data_holder>(other.holder->code);
        auto const count = pcre2_get_ovector_count(other.holder->data);
        std::memcpy(holder->ovector, other.holder->ovector, sizeof(size_t) * count * 2uz);
        holder->target = other.holder->target;
      } else {
        holder.reset();
      }
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
   */
  template <supported_match_result_get_type T = std::string_view>
  auto get(std::string_view const name) const noexcept(supported_match_result_get_is_nothrow<T>) -> T {
    if (not holder || not holder->code || not holder->data) {
      return T{};
    }
    auto const index = pcre2_substring_number_from_name(holder->code, reinterpret_cast<PCRE2_SPTR8>(name.data()));
    if (index < 0) {
      return T{};
    }
    return get<T>(static_cast<size_t>(index));
  }

  auto     operator[](size_t const index) const noexcept -> std::string_view { return get(index); }
  auto     operator[](int const index) const noexcept -> std::string_view { return get(static_cast<size_t>(index)); }
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
  match_result(pcre2_code const* code, pcre2_match_data* src_data, std::string_view target) {
    if (code && src_data) {
      holder = std::make_shared<data_holder>(code);
      auto const count = pcre2_get_ovector_count(src_data);
      std::memcpy(holder->ovector, pcre2_get_ovector_pointer(src_data), sizeof(size_t) * count * 2uz);
      holder->target = target;
    }
  }

  auto get_view(size_t const index) const noexcept -> std::string_view {
    if (not holder || not holder->data || index >= pcre2_get_ovector_count(holder->data)) {
      return {};
    }
    auto const s = holder->ovector[index * 2uz + 0uz];
    auto const e = holder->ovector[index * 2uz + 1uz];
    if (s == PCRE2_UNSET || e == PCRE2_UNSET) {
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
  template <bool UseJIT> friend struct context;
};

namespace detail {
/**
 * @brief match_result をタプルに変換（内部実装用）
 *
 * match_result の各キャプチャグループを std::get<> でアクセス可能なタプルに変換します。
 *
 * @tparam N タプルの要素数（bool + キャプチャ数）
 * @tparam Is インデックスシーケンス
 * @param mr マッチ結果
 * @return bool(true) + 全体マッチ + 各キャプチャグループを含むタプル
 */
template <size_t N, size_t... Is>
auto match_result_to_tuple_impl(match_result const& mr, std::index_sequence<Is...>) -> nttp_match_tuple_t<N> {
  return std::tuple_cat(std::make_tuple(static_cast<bool>(mr)), std::make_tuple(mr.get(Is)...));
}

/**
 * @brief match_result をタプルに変換（パブリック用）
 *
 * @tparam N タプルの要素数（bool + キャプチャ数）
 * @param mr マッチ結果
 * @return bool(マッチ成功) + 全体マッチ + 各キャプチャグループを含むタプル
 */
template <size_t N>
auto match_result_to_tuple(match_result const& mr) -> nttp_match_tuple_t<N> {
  return match_result_to_tuple_impl<N>(mr, std::make_index_sequence<N>{});
}
}  // namespace detail

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
  bool is_end = true;
  match_result result;

  iterator() = default;
  iterator(context<UseJIT> const* c, std::string_view t, size_t p, bool end);

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
      return true;
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

  match_range() = default;
  match_range(iterator<UseJIT> f, iterator<UseJIT> l) : first(f), last(l) {}

  constexpr auto begin() const { return first; }
  constexpr auto end() const { return last; }
};

/**
 * @struct context
 * @brief コンパイル済み正規表現を管理する構造体
 */
template <bool UseJIT = true>
struct context {
private:
  pcre2_code* code = nullptr;
  friend struct match_result;

public:
  context() = default;
  context(std::string_view const src, unsigned int option = 0) {
    if (auto const res = compile(src, option); !res) {
      throw std::runtime_error{res.error()};
    }
  }
  ~context() { release(); }

  /**
   * @brief 例外を投げないファクトリメソッド
   */
  static auto create(std::string_view const src, unsigned int option = 0) -> std::expected<context<UseJIT>, std::string> {
    auto ctx = context<UseJIT>{};
    if (auto const res = ctx.compile(src, option); not res) {
      return std::unexpected{res.error()};
    }
    return ctx;
  }

  context(context const&) = delete;
  auto operator=(context const&) -> context& = delete;

  context(context&& other) noexcept : code(other.code) { other.code = nullptr; }
  auto operator=(context&& other) noexcept -> context& {
    if (this != &other) {
      release();
      code       = other.code;
      other.code = nullptr;
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
  auto compile(std::string_view const src, unsigned int option = 0) noexcept -> std::expected<void, std::string> {
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
      pcre2_jit_compile(c, PCRE2_JIT_COMPLETE);
    }
    this->code = c;
    return {};
  }

  /**
   * @brief 名前付きキャプチャグループのインデックスを取得する
   *
   * @param name キャプチャグループ名
   * @return 見つかった場合はインデックス、見つからない場合は負値
   */
  auto capture_index(std::string_view const name) const noexcept -> int {
    if (not code) {
      return -1;
    }
    return pcre2_substring_number_from_name(code, reinterpret_cast<PCRE2_SPTR8>(name.data()));
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
        return pcre2_jit_match(code, reinterpret_cast<PCRE2_SPTR8>(target.data()), target.size(), start, option, data, nullptr);
      } else {
        return pcre2_match(code, reinterpret_cast<PCRE2_SPTR8>(target.data()), target.size(), start, option, data, nullptr);
      }
    }();
    if (rc >= 0) {
      return rc;
    }
    if (rc == PCRE2_ERROR_NOMATCH) {
      return 0;
    }
    return std::unexpected{"Match error."};
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
    if (not mr.holder) {
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
   * @param target 判定対象の文字列
   * @param mr マッチ結果を格納するオブジェクト
   * @param option マッチオプション
   * @return std::expected<bool, std::string> 完全一致する場合はtrue、それ以外はfalse
   */
  auto match(std::string_view const target, match_result& mr, unsigned int const option = 0) const noexcept -> std::expected<bool, std::string> {
    auto const rc = find(target, mr, 0uz, option | PCRE2_ANCHORED | PCRE2_ENDANCHORED);
    if (not rc) {
      return std::unexpected{rc.error()};
    }
    return *rc > 0;
  }

  /**
   * @brief 文字列置換
   *
   * @param target 置換対象の文字列
   * @param replacement 置換後の文字列
   * @param option 置換オプション
   * @return std::expected<std::string, std::string> 置換後の文字列
   */
  auto replace(std::string_view const target, std::string_view const replacement, unsigned int const option = PCRE2_SUBSTITUTE_GLOBAL) const noexcept -> std::expected<std::string, std::string> {
    if (not code) {
      return std::unexpected{"Not compiled."};
    }
    auto outlen = target.size() + replacement.size() + 1024uz;
    auto buffer = std::string(outlen, '\0');
    auto blen   = outlen;
    auto const rc = pcre2_substitute(
      code,
      reinterpret_cast<PCRE2_SPTR8>(target.data()), target.size(),
      0uz,
      option,
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
    if (rc == PCRE2_ERROR_NOMEMORY) {
      buffer.resize(blen);
      auto const rc2 = pcre2_substitute(
        code,
        reinterpret_cast<PCRE2_SPTR8>(target.data()), target.size(),
        0uz,
        option,
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
    }
    return std::unexpected{"Replace error."};
  }

  /**
   * @brief ラムダ式を用いた動的置換
   * @param target 対象文字列
   * @param callback マッチ結果を受け取り置換後の文字列を返す関数
   * @return 置換後の文字列
   */
  template <typename F>
    requires std::invocable<F, match_result const&>
  auto replace(std::string_view const target, F&& callback) const -> std::string {
    auto result = std::string{};
    result.reserve(target.size());

    auto last_pos = 0uz;
    for (auto& mr : find_all(target)) {
      auto const start = mr.start_pos();
      auto const end = mr.end_pos();
      result.append(target.substr(last_pos, start - last_pos));
      result.append(callback(mr));
      last_pos = end;
      // 0長マッチ対策: 同じ位置でマッチし続けるのを防ぐ
      if (start == end) {
        ++last_pos;
      }
    }
    result.append(target.substr(last_pos));
    return result;
  }

  /**
   * @brief 全てのマッチ箇所をイテレートするためのrangeを返す
   *
   * @param target 検索対象の文字列
   * @return match_range<UseJIT>
   */
  auto find_all(std::string_view const target) const noexcept -> match_range<UseJIT> {
    return {iterator<UseJIT>(this, target, 0uz, false), iterator<UseJIT>(this, target, 0uz, true)};
  }

  /**
   * @brief 正規表現を区切り文字列として文字列を分割する
   *
   * @param target 分割対象の文字列
   * @return std::vector<std::string_view> 分割された文字列 区切り文字列がなければtarget全体が1要素のベクター
   */
  auto split(std::string_view const target) const noexcept -> std::vector<std::string_view> {
    auto res  = std::vector<std::string_view>{};

    auto last = 0uz;
    for (auto& mr : find_all(target)) {
      auto const start = mr.start_pos();
      auto const end = mr.end_pos();
      res.push_back(target.substr(last, start - last));
      last = end;
    }
    res.push_back(target.substr(last));
    return res;
  }
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
  auto get() const -> std::string_view {
    if (not matched) {
      return {};
    }
    static auto const ctx_res = context<UseJIT>::create(Pattern.view());
    if (not ctx_res) {
      return {};
    }
    auto const index = ctx_res->capture_index(Name.view());
    if (index < 0) {
      return {};
    }
    auto const uindex = static_cast<size_t>(index);
    if (uindex >= groups.size()) {
      return {};
    }
    return groups[uindex];
  }
};

/**
 * @brief NTTP 版 find/find_all の戻り値型エイリアス
 */
template <fixed_string Pattern, bool UseJIT = true>
using nttp_find_result_t = nttp_match_result<Pattern, UseJIT>;

namespace detail {
template <fixed_string Pattern, bool UseJIT, size_t... Is>
auto make_nttp_result_impl(match_result const& mr, std::index_sequence<Is...>) -> nttp_find_result_t<Pattern, UseJIT> {
  return {
    .matched = static_cast<bool>(mr),
    .groups = {mr.get(Is)...}
  };
}

template <fixed_string Pattern, bool UseJIT>
auto make_nttp_result(match_result const& mr) -> nttp_find_result_t<Pattern, UseJIT> {
  return make_nttp_result_impl<Pattern, UseJIT>(mr, std::make_index_sequence<nttp_group_count_v<Pattern>>{});
}

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
}  // namespace detail

/**
 * @brief nttp_match_result を構造化束縛可能にする get（tuple-like プロトコル）
 */
template <size_t Index, fixed_string Pattern, bool UseJIT>
auto get(nttp_match_result<Pattern, UseJIT> const& result) noexcept {
  static_assert(Index < (nttp_group_count_v<Pattern> + 1uz));
  return result.template get<Index>();
}

/**
 * @brief NTTP 版 find：正規表現をテンプレート引数で指定する検索
 *
 * 与えられたパターンで最初のマッチを検索し、結果をオブジェクトで返します。
 * パターンコンパイルやマッチ実行で失敗した場合は std::runtime_error を送出します。
 */
template <fixed_string Pattern, bool UseJIT = true>
auto find(std::string_view const target, size_t const start = 0uz, unsigned int const option = 0) -> nttp_find_result_t<Pattern, UseJIT> {
  static auto const ctx_res = context<UseJIT>::create(Pattern.view());
  if (not ctx_res) {
    throw std::runtime_error{"NTTP find compile error: " + ctx_res.error()};
  }
  auto const& ctx = *ctx_res;
  auto*       md  = detail::get_tls_match_data(ctx.get_code());
  auto const  res = ctx.find(target, md, start, option);
  if (not res) {
    throw std::runtime_error{"NTTP find match error: " + res.error()};
  }
  if (*res <= 0) {
    return {};
  }
  return detail::make_nttp_result_raw<Pattern, UseJIT>(md, target);
}

/**
 * @brief NTTP 版 find_all：正規表現をテンプレート引数で指定する全マッチ検索
 */
template <fixed_string Pattern, bool UseJIT = true>
auto find_all(std::string_view const target) {
  static auto const ctx_res = context<UseJIT>::create(Pattern.view());
  if (not ctx_res) {
    throw std::runtime_error{"NTTP find_all compile error: " + ctx_res.error()};
  }
  auto const& ctx = *ctx_res;
  return ctx.find_all(target) | std::views::transform([](auto const& mr) {
    return detail::make_nttp_result<Pattern, UseJIT>(mr);
  });
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
   */
  auto find(std::string_view const target, size_t const start = 0uz, unsigned int const option = 0) const {
    return pcrepp::find<Pattern, UseJIT>(target, start, option);
  }

  /**
   * @brief 全てのマッチを検索
   */
  auto find_all(std::string_view const target) const {
    return pcrepp::find_all<Pattern, UseJIT>(target);
  }

  /**
   * @brief 完全一致を確認
   */
  auto match(std::string_view const target, unsigned int const option = 0) const {
    static auto const ctx_res = context<UseJIT>::create(Pattern.view());
    if (not ctx_res) {
      throw std::runtime_error{"NTTP match compile error: " + ctx_res.error()};
    }
    match_result mr;
    auto const res = ctx_res->match(target, mr, option);
    if (not res) {
      throw std::runtime_error{"NTTP match error: " + res.error()};
    }
    return *res;
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

// frozenchars ライブラリが利用可能な場合、テンプレート引数に指定可能にする
#ifdef PCREPP_HAS_FROZENCHARS
/**
 * @brief frozenchars::FrozenString 版 find
 */
template <frozenchars::FrozenString Pattern, bool UseJIT = true>
auto find(std::string_view const target, size_t const start = 0uz, unsigned int const option = 0) {
  return find<to_fixed_string(Pattern), UseJIT>(target, start, option);
}

/**
 * @brief frozenchars::FrozenString 版 find_all
 */
template <frozenchars::FrozenString Pattern, bool UseJIT = true>
auto find_all(std::string_view const target) {
  return find_all<to_fixed_string(Pattern), UseJIT>(target);
}

/**
 * @brief frozenchars::FrozenString 版 compile
 */
template <frozenchars::FrozenString Pattern, bool UseJIT = true>
constexpr auto compile() {
  return compile<to_fixed_string(Pattern), UseJIT>();
}
#endif // PCREPP_HAS_FROZENCHARS

template <bool UseJIT>
inline match_result::match_result(context<UseJIT> const& ctx) : match_result(ctx.code) {}

template <bool UseJIT>
inline iterator<UseJIT>::iterator(context<UseJIT> const* c, std::string_view t, size_t p, bool end) : ctx(c), target(t), pos(p), is_end(end) {
  if (!is_end && ctx) {
    result  = match_result(*ctx);
    if (auto const rc = ctx->find(target, result, pos); not rc || *rc <= 0) {
      is_end = true;
    }
  }
}

template <bool UseJIT>
inline auto iterator<UseJIT>::operator++() -> iterator& {
  if (ctx && not is_end) {
    auto const prev_end = result.end_pos();
    auto const next_pos = (prev_end == pos) ? pos + 1uz : prev_end;  // ゼロ長マッチ対策
    pos = next_pos;
    if (pos > target.size()) {
      is_end = true;
      return *this;
    }
    if (auto const rc = ctx->find(target, result, pos); not rc || *rc <= 0) {
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

#endif
