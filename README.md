# pcrepp

`pcrepp` は、PCRE2 (Perl Compatible Regular Expressions) を C++ で直感的に扱うためのモダンなヘッダーオンリーラッパーライブラリです。

## 特徴

- **モダンな C++ 設計**: C++23 以上の機能を活用（`std::expected`, `std::ranges`, `std::format`, `std::generator` など）。
- **ヘッダーオンリー**: `pcrepp.hpp` をインクルードするだけで利用可能。
- **直感的な API**: 名前付きキャプチャグループへのアクセス、イテレータによる全マッチの列挙、ラムダ式を用いた動的置換などをサポート。
- **高性能**: 必要に応じて PCRE2 の JIT コンパイルをサポート。

## 動作要件

- **C++ コンパイラ**: C++23 以降をサポートするコンパイラ（GCC 14+, Clang 18+, MSVC 19.36+ 推奨）。
- **依存ライブラリ**: PCRE2 (8-bit 版)。

## インストール方法

### vcpkg を使用する場合

`vcpkg.json` に以下の依存関係を追加してください。

```json
{
  "dependencies": [
    "pcrepp"
  ]
}
```

### CMake プロジェクトへの組み込み

`fetch_content` や `add_subdirectory` でプロジェクトに追加し、`pcrepp::pcrepp` ターゲットをリンクしてください。

```cmake
find_package(pcrepp CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE pcrepp::pcrepp)
```

## API リファレンス

### `pcrepp::context<UseJIT, JITFlags>`
正規表現のコンパイルと検索・置換操作を管理するメインクラスです。

- **`static create(src, option)`**: `std::expected` を返すファクトリメソッド。
- **`compile(src, option)`**: パターンをコンパイルします。
- **`find(target, match_result, start, option)`**: 単一のマッチを検索し、既存の `match_result` に書き込みます。
- **`find(target, start, option)`**: 単一のマッチを検索し、`match_result` を戻り値で返します。
- **`match(target, match_result, option)`** / **`match(target, option)`**: 完全一致を判定します。
- **`replace(target, replacement, option)`**: 文字列による全置換を行います。
- **`replace(target, callback)`**: ラムダ式を用いた動的置換を行います。
- **`find_all(target, option, start)`**: すべてのマッチを巡回するための Range を返します。
- **`split(target, option)`**: 正規表現を区切り文字として文字列を分割します。
- **`split_view(target, option)`**: `std::generator` による lazy 分割を返します（C++23）。
- **`capture_count()` / `named_captures()` / `pattern_size()` / `jit_size()` / `options()`**: パターン情報を取得します。
- **`set_match_limit()` / `set_depth_limit()` / `set_heap_limit()` / `set_offset_limit()`**: PCRE2 の match context 制限を設定します。

### `pcrepp::find<"..."> / pcrepp::find_all<"...">` (NTTP API)


正規表現をテンプレート引数（NTTP: Non-Type Template Parameter）で直接指定するヘルパー関数です。`find` は `std::expected` を返します。

#### `find<Pattern>(target, start = 0, option = 0)`

```cpp
auto result = pcrepp::find<R"((?<key>\w+):(?<value>\d+))">("age:30");
if (result && *result) {
  auto whole = pcrepp::get<1>(*result);
  auto key = result->get<"key">();
  auto value = result->get<"value">();
}

// 構造化束縛にも対応
if (auto r = pcrepp::find<R"((\w+):(\d+))">("age:30"); r && *r) {
  auto [matched, whole, key, value] = *r;
  (void)matched;
  (void)whole;
  (void)key;
  (void)value;
}
```

- **戻り値**: `std::expected<nttp_match_result<Pattern, ...>, std::string>`
- **要素順序**:
  - `find<Pattern>`: `[matched, whole, g1, ...]`（N+2 要素）
  - `find_all<Pattern>`: `[whole, g1, ...]`（N+1 要素）
- **名前付き取得**: `result.get<"name">()`
- **エラー時**: `expected::error()` で取得（`find_unchecked` は throw 版）
#### `find_all<Pattern>(target, option = 0)`

すべてのマッチを取得します。

```cpp
auto all = pcrepp::find_all<R"((?<key>\w+):(?<value>\d+))">("age:30 height:180", 0);
for (auto const& result : all) {
  if (not result) continue;
  auto key = result.get<"key">();
  auto value = result.get<"value">();
}
```

- **戻り値**: `std::vector<nttp_match_result<Pattern, ...>>`
- **エラー時**: `std::runtime_error` を送出

#### `compile<Pattern>() / "..."_re`

`nttp_regex<Pattern>` オブジェクトを生成し、以下を利用できます。

- **`re.find(target, start, option)`**
- **`re.find_all(target, option)`**
- **`re.match(target, option)`** (`std::expected<bool, std::string>`)
- **`re.replace(target, replacement, option)`** / **`re.replace(target, callback)`**
- **`re.split(target, option)`**
- 置換オプションには PCRE2 定数を直接使用（`PCRE2_SUBSTITUTE_GLOBAL`, `PCRE2_SUBSTITUTE_EXTENDED` など）
#### 利点

- **構造化束縛しやすい**: `auto [matched, ...] = find<...>(...)` のように直接展開可能
- **型安全**: タプル型がコンパイル時に確定するため、IDE 補完やコンパイラチェックが効きやすい
- **簡潔**: `context` や `std::expected` の分岐処理を省いて使える

### `pcrepp::match_result`
個別のマッチング結果を保持するクラスです。

- **`get<T = std::string_view>(index / name)`**: 指定したインデックスまたは名前のグループを取得します。`T` には `std::string_view`、`std::string`、`float`、`double`、各種整数型を指定でき、未対応型はコンパイルエラーになります。数値変換に失敗した場合はその型のデフォルト値を返します。`name` は NUL 終端されている必要はなく、内部で NUL 終端バッファにコピーされます。
- **`try_get<T>(index / name)`**: `std::string_view` / `std::string` / 数値型に対応。変換失敗・範囲外・名前未マッチは `std::nullopt`。
- **`to_tuple<N>()`**: キャプチャを tuple 化します。
- **`operator[]`**: `get` のエイリアス。
- **`size()`**: キャプチャグループの数を返します。
- **`start_pos() / end_pos()`**: マッチした箇所の開始/終了位置を返します。
- **`begin() / end()`**: キャプチャグループを巡回するためのイテレータを返します。
- **`operator bool()`**: マッチに成功したかどうかを判定します。

## 使い方

```cpp
#include <iostream>
#include <format>
#include <string_view>
#include "pcrepp.hpp"

auto main() -> int {
  using namespace std::string_view_literals;

  // 1. 正規表現のコンパイル
  auto ctx_res = pcrepp::context<>::create(R"((?<name>\w+):\s*(?<value>\d+))");
  if (not ctx_res) {
    std::cerr << "Compile error: " << ctx_res.error() << "\n";
    return 1;
  }
  auto const& ctx = *ctx_res;

  auto const target = "Apple: 100, Banana: 200"sv;

  // 2. 検索とマッチ結果の取得
  std::cout << "--- match_result features ---\n";
  for (auto const& res : ctx.find_all(target)) {
    if (not res) continue;

    std::cout << "Match: " << res[0] << "\n"; // インデックスアクセス
    std::cout << "  Name:  " << res["name"] << "\n"; // 名前によるアクセス
    std::cout << "  Value: " << res.get<int>("value") << "\n"; // 型変換付きアクセス

    std::cout << "  All groups: ";
    for (auto const& group : res) {
      std::cout << "\"" << group << "\" ";
    }
    std::cout << "\n";
    std::cout << std::format("  Formatted: {}\n", res); // std::format 対応
  }

  auto first_res = ctx.find(target);
  if (first_res && *first_res) {
    std::cout << "\nFirst match via return value: " << (*first_res)["name"] << "\n";
  }

  // 3. ラムダ式を用いた動的置換
  std::cout << "\n--- Dynamic Replacement (Lambda) ---\n";
  auto dynamic_res = ctx.replace(target, [](auto const& res) {
    auto value = res.get<int>("value");
    return std::format("{}({} USD)", res["name"], value / 100);
  });
  if (not dynamic_res) {
    std::cerr << dynamic_res.error() << "\n";
    return 1;
  }
  std::cout << "Original: " << target << "\n";
  std::cout << "Replaced: " << *dynamic_res << "\n";

  return 0;
}
```

### NTTP API を使った簡潔な例

```cpp
#include <iostream>
#include <tuple>
#include <string_view>
#include "pcrepp.hpp"

auto main() -> int {
  using namespace std::string_view_literals;

  auto const target = "age:30 height:180"sv;

  // NTTP 版の find_all で全マッチを取得
  auto const all = pcrepp::find_all<R"((\w+):(\d+))">(target);

  std::cout << "--- NTTP find_all ---\n";
  // find_all の構造化束縛は [whole, g1, g2, ...] (matched を含まない)
  for (auto const& [whole, key, value] : all) {
    std::cout << "Key: " << key << ", Value: " << value << "\n";
  }

  // NTTP 版の find で最初のマッチを取得
  if (auto const first = pcrepp::find<R"((\w+):(\d+))">(target); first && *first) {
    auto const& [matched, whole, key, value] = *first;
    if (matched) {
      std::cout << "\nFirst match: " << key << " = " << value << "\n";
    }
  }

  return 0;
}
```

## ベンチマーク

pcrepp と raw PCRE2 のパフォーマンス比較は [benchmark.md](benchmark.md) を参照してください。

## ライセンス

このプロジェクトは [MIT ライセンス](LICENSE) の下で公開されています。
