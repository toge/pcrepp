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


正規表現をテンプレート引数（NTTP: Non-Type Template Parameter）で直接指定するヘルパー関数です。戻り値は **tuple-like な専用オブジェクト** で、構造化束縛と `get()` の両方を使えます。

#### `find<Pattern>(target, start = 0, option = 0)`

```cpp
auto result = pcrepp::find<R"((?<key>\w+):(?<value>\d+))">("age:30");
if (result) {
  // index 指定
  auto whole = pcrepp::get<1>(result);
  // 名前付きキャプチャ指定
  auto key = result.get<"key">();
  auto value = result.get<"value">();
}

// 構造化束縛にも対応
if (auto [matched, whole, key, value] = pcrepp::find<R"((\w+):(\d+))">("age:30"); matched) {
  // ...
}
```

- **戻り値**: `nttp_match_result<Pattern, ...>`（bool 変換可能）
- **要素順序**:
  - `pcrepp::get<0>(result)`: `bool` — マッチ成功フラグ
  - `pcrepp::get<1>(result)` 以降: `std::string_view` — 全体マッチと各キャプチャグループ
- **名前付き取得**: `result.get<"name">()`
- **マッチしない場合**: `bool` は `false` で、それ以外は空の `std::string_view`
- **エラー時**: `std::runtime_error` を送出

#### `find_all<Pattern>(target)`

すべてのマッチを取得します。

```cpp
auto all = pcrepp::find_all<R"((?<key>\w+):(?<value>\d+))">("age:30 height:180");
for (auto const& result : all) {
  if (not result) continue;
  auto key = result.get<"key">();
  auto value = result.get<"value">();
}
```

- **戻り値**: `std::vector<nttp_match_result<Pattern, ...>>`
- **エラー時**: `std::runtime_error` を送出
#### 利点

- **構造化束縛しやすい**: `auto [matched, ...] = find<...>(...)` のように直接展開可能
- **型安全**: タプル型がコンパイル時に確定するため、IDE 補完やコンパイラチェックが効きやすい
- **簡潔**: `context` や `std::expected` の分岐処理を省いて使える

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
  auto const all = pcrepp::find_all<R"((\w+):(\d+))">(target);

  std::cout << "--- NTTP find_all ---\n";
  for (auto const& [matched, whole, key, value] : all) {
    if (not matched) continue;
    std::cout << "Key: " << key << ", Value: " << value << "\n";
  }

  // NTTP 版の find で最初のマッチを取得
  if (auto const [matched, whole, key, value] = pcrepp::find<R"((\w+):(\d+))">(target); matched) {
    std::cout << "\nFirst match: " << key << " = " << value << "\n";
  }

  return 0;
}
```

## ライセンス

このプロジェクトは [MIT ライセンス](LICENSE) の下で公開されています。
