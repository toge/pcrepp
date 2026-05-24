# pcrepp

`pcrepp` は、PCRE2 (Perl Compatible Regular Expressions) を C++ で直感的に扱うためのモダンなヘッダーオンリーラッパーライブラリです。

## 特徴

- **モダンな C++ 設計**: C++20 以上の機能を活用（`std::expected`, `std::ranges`, `std::format` など）。
- **ヘッダーオンリー**: `pcrepp.hpp` をインクルードするだけで利用可能。
- **直感的な API**: 名前付きキャプチャグループへのアクセス、イテレータによる全マッチの列挙、ラムダ式を用いた動的置換などをサポート。
- **高性能**: 必要に応じて PCRE2 の JIT コンパイルをサポート。

## 動作要件

- **C++ コンパイラ**: C++20 以降をサポートするコンパイラ（GCC 11+, Clang 13+, MSVC 19.30+ 推奨）。
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

### `pcrepp::context<UseJIT>`
正規表現のコンパイルと検索・置換操作を管理するメインクラスです。

- **`static create(src, option)`**: `std::expected` を返すファクトリメソッド。
- **`compile(src, option)`**: パターンをコンパイルします。
- **`find(target, match_result, start, option)`**: 単一のマッチを検索し、既存の `match_result` に書き込みます。
- **`find(target, start, option)`**: 単一のマッチを検索し、`match_result` を戻り値で返します。
- **`match(target, match_result, option)`**: 完全一致を判定します。
- **`replace(target, replacement, option)`**: 文字列による全置換を行います。
- **`replace(target, callback)`**: ラムダ式を用いた動的置換を行います。
- **`find_all(target)`**: すべてのマッチを巡回するための Range を返します。
- **`split(target)`**: 正規表現を区切り文字として文字列を分割します。

### `pcrepp::find<"..."> / pcrepp::find_all<"...">` (NTTP API)

正規表現をテンプレート引数（NTTP: Non-Type Template Parameter）で直接指定するヘルパー関数です。キャプチャグループ数がコンパイル時に決定され、型安全な **タプル** で結果を返します。

#### `find<Pattern>(target, start = 0, option = 0)`

```cpp
auto res = pcrepp::find<R"((\w+):(\d+))">("age:30");
if (res) {
  auto const& tup = *res;
  auto matched = std::get<0>(tup);       // bool: マッチしたか
  auto whole = std::get<1>(tup);         // 全体マッチ (get<0> 相当)
  auto group1 = std::get<2>(tup);        // グループ 1
  auto group2 = std::get<3>(tup);        // グループ 2
  // ...
}
```

- **戻り値**: `std::expected<std::tuple<bool, std::string_view, ...>, std::string>`
- **タプル要素**:
  - `get<0>()`: `bool` — マッチ成功フラグ
  - `get<1>()` 以降: `std::string_view` — 全体マッチと各キャプチャグループ（順序は `context::find()` と同じ）
- **マッチしない場合**: `std::get<0>(tup) == false` で、それ以外は空の `std::string_view`

#### `find_all<Pattern>(target)`

すべてのマッチを取得します。

```cpp
auto res_all = pcrepp::find_all<R"((\w+):(\d+))">("age:30 height:180");
if (res_all) {
  for (auto const& tup : *res_all) {
    auto matched = std::get<0>(tup);
    auto whole = std::get<1>(tup);
    auto group1 = std::get<2>(tup);
    auto group2 = std::get<3>(tup);
    // ...
  }
}
```

- **戻り値**: `std::expected<std::vector<std::tuple<bool, std::string_view, ...>>, std::string>`

#### 利点

- **型安全**: タプル型がコンパイル時に確定するため、IDE 補完やコンパイラチェックが効きやすい
- **性能**: パターン文字列がコンパイル時に処理され、NTTP を活用した最適化が可能
- **簡潔**: `context` を明示的に管理する必要がなく、ワンライナー的な使用法が可能

### `pcrepp::match_result`
個別のマッチング結果を保持するクラスです。

- **`get<T = std::string_view>(index / name)`**: 指定したインデックスまたは名前のグループを取得します。`T` には `std::string_view`、`std::string`、`float`、`double`、各種整数型を指定でき、未対応型はコンパイルエラーになります。数値変換に失敗した場合はその型のデフォルト値を返します。
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
  std::cout << "Original: " << target << "\n";
  std::cout << "Replaced: " << dynamic_res << "\n";

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
  auto res_all = pcrepp::find_all<R"((\w+):(\d+))">(target);
  if (not res_all) {
    std::cerr << "Error: " << res_all.error() << "\n";
    return 1;
  }

  std::cout << "--- NTTP find_all ---\n";
  for (auto const& tup : *res_all) {
    auto matched = std::get<0>(tup);
    if (not matched) continue;

    auto whole = std::get<1>(tup);   // 全体マッチ
    auto key = std::get<2>(tup);     // グループ 1
    auto value = std::get<3>(tup);   // グループ 2

    std::cout << "Key: " << key << ", Value: " << value << "\n";
  }

  // NTTP 版の find で最初のマッチを取得
  auto res = pcrepp::find<R"((\w+):(\d+))">(target);
  if (res && std::get<0>(*res)) {
    auto key = std::get<2>(*res);
    auto value = std::get<3>(*res);
    std::cout << "\nFirst match: " << key << " = " << value << "\n";
  }

  return 0;
}
```

このプロジェクトは [MIT ライセンス](LICENSE) の下で公開されています。
