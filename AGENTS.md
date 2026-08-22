# AGENTS.md

`pcrepp` は PCRE2 を C++20/23/26 で扱うための **ヘッダーオンリー** ラッパライブラリです。実装は `include/pcrepp.hpp` の 1 ファイルにすべて収まっています。

## エントリポイント

- ライブラリ本体: `include/pcrepp.hpp` (約 2300 行、Doxygen 日本語コメント)
- 公開 CMake ターゲット: `pcrepp::pcrepp` (INTERFACE ライブラリ)
- 依存: `PCRE2::8BIT`, `FastFloat::fast_float` (必須)、`frozenchars::frozenchars` (任意、NTTP 連携のため)、`ctre` (任意、`WITH_CTRE=ON` 時に必要)
- ランタイム PCRE2 シンボル定義: `PCRE2_CODE_UNIT_WIDTH 8`

## 主要 API

- `pcrepp::context<UseJIT = true>`: コンパイル済み正規表現。`create()` は `std::expected<context, std::string>`、コンストラクタは失敗時に `std::runtime_error`。
- `pcrepp::match_result`: キャプチャ取得。`get<T>(index_or_name)` で `std::string_view` / `std::string` / 整数 / `float` / `double` を取得。数値変換失敗は `T{}` を返す。`try_get<T>()` は `std::optional<T>`。
- NTTP 版: `pcrepp::find<"...">`, `pcrepp::find_all<"...">`, `pcrepp::compile<"...">()`, `R"(...)"_re` リテラル。戻り値は tuple-like で構造化束縛可。
- 内部: `pcrepp::detail::count_capture_groups` (constexpr)、`pcrepp::detail::tls_match_data_cache` (TLS バッファ再利用)。
- CTRE フォールバック: `WITH_CTRE=ON` で有効化。現在は可変長 lookbehind のみ CTRE に委譲（ネスト量化子の委譲は 2026-07 のベンチマーク結果により削除。PCRE2 の auto-possessify 最適化の方が高速のため）。後方参照・再帰を含むパターンは強制的に PCRE2 を使用。

## ビルド

スクリプトは vcpkg を前提 (toolchain は `~/vm/vcpkg` を見にいく、編集は `build.sh` 内の `VCPKG_ROOT` パス)。

```sh
# Linux (デフォルト: shared、x64-linux-static triplet)
./build.sh
./build.sh static         # build_static/ に static ビルド

# クロスコンパイル
./build_mingw.sh          # MinGW64
./build_win64.sh          # Windows 64bit (mingw64-cmake)

# CTRE フォールバック付き
cmake -B build_ctre -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake -DCMAKE_BUILD_TYPE=Release -DWITH_CTRE=ON
cmake --build build_ctre --parallel
```

C++ 標準は `cxx_std_23` 固定。GCC/Clang では `-O3 -march=native`、`-Wall -Wextra -pedantic` 等の最適化・警告フラグは **pcrepp をトップレベルプロジェクトとしてビルドする場合のみ** 付与され (コンシューマへの漏出防止)、`-g3` は GCC 時に付く。

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
  - clang-tidy 22 以降は値名を `UPPER_CASE` と表記する必要がある (旧 `upper_case` は無効値で警告のみ出力され、実質的に規約が効かない)。
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
- **`count_capture_groups` リファクタリングは保留中**: 状態変数を `parse_state` 構造体にまとめて可読性を上げる試みは、constexpr 関数内でローカル定義した `parse_state` のメンバが `std::array<br_state, 64>` へのインデックスアクセス時に「`array subscript value '0' is outside the bounds`」を発して、`test/test_capture_count.cpp` の branch reset 系 `static_assert` 6 件がコンパイルエラーになった (性能劣化ではなく機能破損のため修正を破棄)。可読性改善が必要であれば `parse_state` を `detail::` 名前空間の関数外で定義し、`std::array` のサイズを constexpr 評価可能な定数として外出しにしてから着手すること。

## 検証チェックリスト

変更を加えたら、最低限以下を確認する。

```sh
cmake --build build --parallel
cd build && ctest --output-on-failure
./build/test/all_test --list-tests >/dev/null   # バイナリ健全性
```

NTTP 関連を触った場合は `count_capture_groups` 系の `static_assert` を含む `[capture_count]` テストと `[nttp_find]` を必ず通す。TLS/スレッド周りを触った場合は `[tls][stress]` テスト (5000 回イテレーション) を通す。

CTRE フォールバックを触った場合:
```sh
cmake -B build_ctre -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake -DCMAKE_BUILD_TYPE=Release -DWITH_CTRE=ON
cmake --build build_ctre --parallel && cd build_ctre && ctest --output-on-failure
cd ..
```
`ctre_recommended` の constexpr 解析は `[capture_count]` と同様に静的アサートでテストされているため、変更時はコンパイルが通れば正常。

## ベンチマーク

`bench/` ディレクトリに Google Benchmark を使用したパフォーマンス計測があります。

```sh
cmake -B build -DBUILD_BENCH=ON [-DWITH_CTRE=ON] ...
cmake --build build --parallel
./build/bench/bench_main                                           # 全ベンチマーク
./build/bench/bench_main --benchmark_filter="BM_NttpFind|BM_Jit"   # 絞り込み
./build/bench/bench_main --benchmark_filter="BM_Adversarial"       # 敵対的パターンのみ
```

主なベンチマークグループ:
- `BM_NttpFind_*` / `BM_JitFind_*` / `BM_NoJitFind_*` — find (first match) の 3 経路比較
- `BM_NttpFindAll_*` / `BM_JitFindAll_*` / `BM_NoJitFindAll_*` — find_all 比較
- `BM_Adversarial_*` — `(a|aa|aaa)+[b-z]` の敵対的パターン（`WITH_CTRE=ON` 時は `BM_DirectCtre_*` で CTRE 直接呼び出しも計測）

`bench_compare.cpp` は CTRE 委譲の要否を判断するためのデータを生成する。新しい委譲条件を追加する際は、このファイルにベンチマークを追加し PCRE2-only と CTRE 有効の両方で比較すること。
