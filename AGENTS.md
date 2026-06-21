# AGENTS.md

`pcrepp` は PCRE2 を C++20/23/26 で扱うための **ヘッダーオンリー** ラッパライブラリです。実装は `include/pcrepp.hpp` の 1 ファイルにすべて収まっています。

## エントリポイント

- ライブラリ本体: `include/pcrepp.hpp` (約 1897 行、Doxygen 日本語コメント)
- 公開 CMake ターゲット: `pcrepp::pcrepp` (INTERFACE ライブラリ)
- 依存: `PCRE2::8BIT`, `FastFloat::fast_float` (必須)、`frozenchars::frozenchars` (任意、NTTP 連携のため)
- ランタイム PCRE2 シンボル定義: `PCRE2_CODE_UNIT_WIDTH 8`

## 主要 API

- `pcrepp::context<UseJIT = true>`: コンパイル済み正規表現。`create()` は `std::expected<context, std::string>`、コンストラクタは失敗時に `std::runtime_error`。
- `pcrepp::match_result`: キャプチャ取得。`get<T>(index_or_name)` で `std::string_view` / `std::string` / 整数 / `float` / `double` を取得。数値変換失敗は `T{}` を返す。`try_get<T>()` は `std::optional<T>`。
- NTTP 版: `pcrepp::find<"...">`, `pcrepp::find_all<"...">`, `pcrepp::compile<"...">()`, `R"(...)"_re` リテラル。戻り値は tuple-like で構造化束縛可。
- 内部: `pcrepp::detail::count_capture_groups` (constexpr)、`pcrepp::detail::tls_match_data_cache` (TLS バッファ再利用)。

## ビルド

スクリプトは vcpkg を前提 (toolchain は `~/vm/vcpkg` を見にいく、編集は `build.sh` 内の `VCPKG_ROOT` パス)。

```sh
# Linux (デフォルト: shared、x64-linux-static triplet)
./build.sh
./build.sh static         # build_static/ に static ビルド

# クロスコンパイル
./build_mingw.sh          # MinGW64
./build_win64.sh          # Windows 64bit (mingw64-cmake)

# 手動構成
cmake -B build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

CMake は `c++26` → `c++23` → `c++20` の順にフォールバック。GCC/Clang では `-O3 -march=native`、`-Wall -Wextra -pedantic` がデフォルトで付く。

## テスト (Catch2)

ビルド成果物: `build/test/all_test`。CTest 上は `all_test` 1 件の登録。

```sh
# 一括実行
cd build && ctest -V            # = ./test.sh
./build/test/all_test           # Catch2 実行ファイル直接呼び出し

# タグで絞り込み
./build/test/all_test "[nttp_find]"
./build/test/all_test "[match_result]"
./build/test/all_test "[tls]"

# テスト一覧
./build/test/all_test --list-tests
```

主要タグ: `[basic]`, `[match_result]`, `[nttp_find]`, `[nttp]`, `[tls]`, `[zero_width]`, `[capture_count]`, `[find_all]`, `[options]`, `[frozenchars]`, `[nul_safety]`。

## コーディング規約

- `.clang-format`: LLVM ベース、**インデント 2 スペース・最大幅 200・左寄せポインタ (`int* p`)**、連続宣言/代入を揃える、`BreakBeforeBraces: Attach`。
- `.clang-tidy`: `cppcoreguidelines-*` / `bugprone-*` / `modernize-*` / `performance-*` を有効。識別子は **lower_case (変数・メンバ・関数 camelBack・定数 upper_case)** を強制 (`readability-identifier-naming`)。
- 言語機能: C++20 concept、`std::expected`、`std::ranges`、`std::format` を常用。
- コメント: **Doxygen 形式で日本語**。内部状態や分岐意図を 1〜数行で記述する流儀。新規 API 追加時はこのスタイルに揃える。
- ヘッダオンリーの都合で `inline` テンプレートメンバ関数のみ `.hpp` 末尾に定義が置かれている (例: `match_result::match_result(context const&)`)。

## 実装上の注意 / 落とし穴

- **`PCREPP_ENABLE_FROZENCHARS_NTTP_OVERLOADS`**: デフォルト 0。文字列リテラル NTTP (`find<"...">`) と `frozenchars::FrozenString` NTTP の曖昧性回避のため。`frozenchars::FrozenString` を直接 NTTP に渡したい TU では `pcrepp.hpp` を取り込む前に `#define PCREPP_ENABLE_FROZENCHARS_NTTP_OVERLOADS 1` する (`test/test_nttp_frozenchars.cpp` 参照)。
- **TLS バッファ**: `pcre2_match_data` を `thread_local` で再利用 (`detail::tls_match_data_cache`)。キャプチャ数が縮んでも `pcre2_match_data_create_from_pattern` で再確保するので安全。マルチスレッドで異なるパターンを使う場合はこのキャッシュが効く。
- **ゼロ長マッチ**: イテレータ (`iterator::operator++`) で `start == end` のとき必ず 1 文字進める。`replace(target, callback)` も同様に 1 進める。重複マッチ回避の核なので変更時は全テスト (`[zero_width]`, `[tls][stress]`) を確認。
- **`count_capture_groups`**: `constexpr` で完全実装。`(?#comment)` / `(*VERB)` / `\Q...\E` / `(?|...)` branch reset / 文字クラス内の括弧をすべて考慮。NTTP 戻り値のタプル長を確定させる唯一の情報源なので、ここを壊すと NTTP 系の全テストが静的アサートで落ちる。
- **NUL 終端**: `pcre2_substring_number_from_name` は NUL 終端を要求するため、名前付きキャプチャは内部で NUL 終端バッファにコピーする (`detail::lookup_named_capture`)。`std::string_view` を直接渡しても安全。
- **`std::format`**: `match_result` 用フォーマッタは `<format>` があるときのみ提供。GCC 14+ / Clang 18+ 想定。
- **CMake ターゲット名**: コンシューマは `pcrepp::pcrepp` を `PRIVATE` / `PUBLIC` リンクし、`#include "pcrepp.hpp"` するだけ。

## 検証チェックリスト

変更を加えたら、最低限以下を確認する。

```sh
cmake --build build --parallel
cd build && ctest --output-on-failure
./build/test/all_test --list-tests >/dev/null   # バイナリ健全性
```

NTTP 関連を触った場合は `count_capture_groups` 系の `static_assert` を含む `[capture_count]` テストと `[nttp_find]` を必ず通す。TLS/スレッド周りを触った場合は `[tls][stress]` テスト (5000 回イテレーション) を通す。
