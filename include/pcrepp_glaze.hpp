#pragma once

#include "pcrepp.hpp"
#include <glaze/glaze.hpp>

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace pcrepp {

namespace detail {

/// @brief コンパイル済み正規表現から名前付きキャプチャの名前リストを取得する
inline auto get_regex_named_captures(pcre2_code const* code) -> std::vector<std::string> {
  if (not code) {
    return {};
  }
  auto name_count      = uint32_t{};
  auto name_entry_size = uint32_t{};
  PCRE2_SPTR name_table = nullptr;

  pcre2_pattern_info(code, PCRE2_INFO_NAMECOUNT, &name_count);
  pcre2_pattern_info(code, PCRE2_INFO_NAMEENTRYSIZE, &name_entry_size);
  pcre2_pattern_info(code, PCRE2_INFO_NAMETABLE, &name_table);

  auto names = std::vector<std::string>{};
  names.reserve(name_count);
  for (auto i = 0u; i < name_count; ++i) {
    auto const* entry = name_table + i * name_entry_size;
    names.emplace_back(reinterpret_cast<char const*>(entry + 2));
  }
  return names;
}

template <typename>
inline constexpr bool always_false = false;

/// @brief match_result から指定された型で名前付きキャプチャの値を取得する
template <typename FieldType>
auto get_field_from_match(match_result const& mr, std::string_view name) -> FieldType {
  if constexpr (std::same_as<FieldType, std::string_view>) {
    return mr.template get<std::string_view>(name);
  } else if constexpr (std::same_as<FieldType, std::string>) {
    return mr.template get<std::string>(name);
  } else if constexpr (std::same_as<FieldType, bool>) {
    auto const sv = mr.template get<std::string_view>(name);
    return sv == "true" || sv == "1";
  } else if constexpr (supported_integer_get_type<FieldType>) {
    return mr.template get<FieldType>(name);
  } else if constexpr (std::same_as<FieldType, float> || std::same_as<FieldType, double>) {
    return mr.template get<FieldType>(name);
  } else {
    static_assert(always_false<FieldType>,
                  "unsupported field type for pcrepp::extract_as. "
                  "supported: string_view, string, bool, integer types, float, double");
  }
}

}  // namespace detail

/**
 * @brief match_result から glaze meta を持つ集成体 T にフィールドをマッピングする
 *
 * 名前付きキャプチャの名前と glaze のフィールド名を照合し、一致するものを代入する。
 * glz::opts でエラーハンドリングを制御:
 * - error_on_unknown_keys: regex の named capture が struct にない場合エラー
 * - error_on_missing_keys: struct field に対応する named capture がない場合エラー
 *
 * @tparam T glaze::meta または glz::object で登録された集成体型
 * @tparam Opts glaze のオプション (デフォルト: glz::opts{})
 */
template <glz::glaze_object_t T, glz::opts Opts = glz::opts{}>
auto extract_as(match_result const& mr) -> std::expected<T, std::string> {
  if (not mr) {
    if constexpr (Opts.error_on_missing_keys) {
      return std::unexpected{std::string{"no match"}};
    }
    return T{};
  }

  using reflect_t = glz::reflect<T>;
  constexpr auto N = reflect_t::size;

  /// error_on_unknown_keys: regex 側の named capture が struct に存在するか確認
  if constexpr (Opts.error_on_unknown_keys) {
    auto const regex_names = detail::get_regex_named_captures(mr.get_code());
    for (auto const& rname : regex_names) {
      auto found = false;
      [&]<size_t... I>(std::index_sequence<I...>) {
        ((found = found || reflect_t::keys[I] == rname), ...);
      }(std::make_index_sequence<N>{});
      if (not found) {
        return std::unexpected{std::string{"unknown named capture: "} + rname};
      }
    }
  }

  T result{};
  std::optional<std::string> error{};

  /// 各フィールドを名前付きキャプチャから代入
  auto assign = [&]<size_t I>() {
    if (error) {
      return;
    }
    auto const& field_name = reflect_t::keys[I];
    auto const  cap_index  = detail::lookup_named_capture(mr.holder->code, field_name);

    if (cap_index < 0) {
      if constexpr (Opts.error_on_missing_keys) {
        error = std::string{"missing named capture: "} + std::string{field_name};
      }
      return;
    }

    using raw_field_t = std::decay_t<typename reflect_t::template type<I>>;
    auto& field_ref = glz::get_member(result, glz::get<I>(reflect_t::values));
    field_ref = detail::get_field_from_match<raw_field_t>(mr, field_name);
  };

  [&]<size_t... I>(std::index_sequence<I...>) {
    (assign.template operator()<I>(), ...);
  }(std::make_index_sequence<N>{});

  if (error) {
    return std::unexpected{std::move(*error)};
  }
  return result;
}

/**
 * @brief context で find した結果を glaze 集成体 T にマッピングする
 *
 * @tparam T glaze::meta で登録された集成体型
 * @tparam Opts glaze のオプション
 * @tparam UseJIT JIT コンパイルを使用するか
 */
template <glz::glaze_object_t T, glz::opts Opts = glz::opts{}, bool UseJIT = true>
auto find_as(context<UseJIT> const& ctx, std::string_view target,
             size_t start = 0uz, unsigned int option = 0) -> std::expected<T, std::string> {
  auto mr = ctx.find(target, start, option);
  if (not mr) {
    return std::unexpected{std::move(mr.error())};
  }
  return extract_as<T, Opts>(*mr);
}

/**
 * @brief NTTP 版 find + glaze 集成体マッピング
 *
 * テンプレート引数 Pattern で正規表現を指定し、マッチ結果を glaze meta を持つ
 * 集成体 T に名前付きキャプチャ経由で代入する。
 *
 * @tparam Pattern NTTP 正規表現パターン
 * @tparam T glaze::meta で登録された集成体型
 * @tparam Opts glaze のオプション
 * @tparam UseJIT JIT コンパイルを使用するか
 */
template <fixed_string Pattern, glz::glaze_object_t T,
          glz::opts Opts = glz::opts{}, bool UseJIT = true>
auto find_as(std::string_view target, size_t start = 0uz, unsigned int option = 0)
  -> std::expected<T, std::string> {
  context<UseJIT> const* ctx_ptr = nullptr;
  try {
    ctx_ptr = &detail::get_nttp_context<Pattern, UseJIT>();
  } catch (std::runtime_error const& e) {
    return std::unexpected{std::string{e.what()}};
  }
  return find_as<T, Opts>(*ctx_ptr, target, start, option);
}

/**
 * @brief NTTP 版 find_all + glaze 集成体マッピング
 *
 * 全マッチを検索し、各結果を glaze 集成体 T にマッピングして vector で返す。
 *
 * @tparam Pattern NTTP 正規表現パターン
 * @tparam T glaze::meta で登録された集成体型
 * @tparam Opts glaze のオプション
 * @tparam UseJIT JIT コンパイルを使用するか
 */
template <fixed_string Pattern, glz::glaze_object_t T,
          glz::opts Opts = glz::opts{}, bool UseJIT = true>
auto find_all_as(std::string_view target, unsigned int option = 0)
  -> std::expected<std::vector<T>, std::string> {
  context<UseJIT> const* ctx_ptr = nullptr;
  try {
    ctx_ptr = &detail::get_nttp_context<Pattern, UseJIT>();
  } catch (std::runtime_error const& e) {
    return std::unexpected{std::string{e.what()}};
  }

  std::vector<T> results;
  for (auto const& mr : ctx_ptr->find_all(target, option)) {
    auto r = extract_as<T, Opts>(mr);
    if (not r) {
      return std::unexpected{r.error()};
    }
    results.push_back(std::move(*r));
  }
  return results;
}

}  // namespace pcrepp
