# レビュー指摘 1〜9 対応設計

## 目的

`pcrepp` の全体レビューで検出した 9 件の正しさ・メモリ安全性・配布上の問題を、単一の PR で修正する。

JIT と NTTP 高速 `replace` は維持する。既存 API の呼び出し形を可能な限り保つ一方、成功値でエラーを表現できない `split` API は `std::expected` を使う形へ変更する。

## 選定した方針

比較した案は次の 3 案である。

1. **PCRE2 の契約へ処理を統一する外科的修正（採用）**
   - 公開 API、JIT、NTTP 高速経路を保ち、PCRE2 の公式反復 API と dispatch を使う。
   - 所有権とマッチ状態を明示し、高速経路にランタイム検証を加える。
2. 全マッチを interpreter、全置換を `pcre2_substitute`、CTRE を無効化
   - 安全だが、主要な性能経路を失う。
3. 9 箇所へ個別ガードを追加
   - 変更量は少ないが、反復・所有権・エラー表現に不整合が残りやすい。

採用案は JIT/高速置換の利点を残したまま、正しさの判断を PCRE2 と実行時の実データへ寄せる。

## 所有権とマッチ状態

- `context` と `match_result` は、`pcre2_code_free` を deleter とする共有所有権で同じコンパイル済みコードを参照する。
- `match_result` は fresh な `pcre2_match_data` と、非所有の対象 `std::string_view` を保持する。
- コピー時は opaque な `pcre2_match_data` を `memcpy` せず、fresh な match data を作り、公開される ovector と必要な状態をスナップショットする。
- `matched` フラグを allocation の有無と分離する。
  - マッチ開始時は `false`。
  - `pcre2_match` が `rc > 0` のときだけ `true`。
  - no-match と error 後は false のまま。
- `bool`、`size()`、`get`/`try_get`、位置、group iterator は matched 状態を確認する。
- 別 context に属する `match_result` の再利用は明示的な error とする。
- 対象文字列は引き続き非所有であり、結果の利用中に対象文字列が生存している必要がある。この寿命条件を公開 API の文書とテストへ明記する。

## マッチング dispatch と resource limit

- 通常マッチは `pcre2_jit_match` を直接呼ばず `pcre2_match` を使う。
- PCRE2 がサポート済み JIT を選択できるときだけ JIT を使い、`PCRE2_ANCHORED`、`PCRE2_ENDANCHORED`、`PCRE2_NO_JIT`、UTF validation、offset validation をラッパーで失わない。
- 次の場合は共通 option 生成器が `PCRE2_NO_JIT` を追加する。
  - `UseJIT == false`
  - `set_heap_limit()` または `set_depth_limit()` を使用
  - pattern の `PCRE2_INFO_HEAPLIMIT` / `PCRE2_INFO_DEPTHLIMIT` が設定済み
- `pcre2_substitute` へ同じ match context と effective option を渡し、通常 match と文字列置換で limit/JIT policy を一致させる。
- 完全一致は `PCRE2_ANCHORED | PCRE2_ENDANCHORED` を渡す。`a|ab` のような先頭で成立する短い代替に対して誤って true を返さない。

## 全マッチ反復

- callback replace、NTTP 高速 replace、iterator は `pcre2_next_match` だけで次位置を決める。
- `pcre2_next_match` は直前の成功 match に使った同一 match data から呼ぶ。
- iterator は返された `next_offset`、追加 option、継続可否を公開前にキャッシュする。
- コピーされた iterator/range と後置 increment は、そのキャッシュを使って fresh な match data 上で次 match を行う。
- 1 byte 進める独自処理を削除し、空マッチ直後の非空マッチ、UTF code point、CRLF、末尾 zero-width match は PCRE2 の規則へ委譲する。

## range の初期エラー

- `match_range` は error storage を iterator より先に宣言・初期化する。
- `match_range(context, target, option, start)` コンストラクタから、最初の match 前に iterator へ `&m_error` を渡す。
- `{ iterator(...), iterator(...) }` を組み立てた後に pointer を付け直す方式は使わない。
- コピー/ムーブ後も両 iterator の error pointer を destination の storage へ再接続する。
- `start > target.size()`、invalid UTF、resource limit error は反復開始前から `range.error()` と bool で確認できる。
- no-match 自体は error ではなく、成功した空 range とする。

## NTTP キャプチャ解析と高速置換

### compile-time shape

- `count_capture_groups` は POSIX class (`[[:alpha:]]`、collating/equivalence class) の内部括弧をカウントしない。
- inline/scoped `PCRE2_EXTENDED` の `(?x)`、`(?-x)`、`(?x:...)` と comment 内括弧を追跡する。
- compile-time count は tuple 形状の補助情報にすぎず、メモリ安全性の根拠にはしない。
- structured NTTP `find` は compile 後の `capture_count() + 1` と `nttp_group_count_v` を比較する。不一致なら不正な shape で返す代わりに明示的な `unexpected` を返す。

### runtime fast path

- replacement 参照番号は、`number <= (max - digit) / 10` の条件で検証し、overflow を sentinel へ変換しない。
- compile-time または runtime の参照検証が不成立なら `pcre2_substitute` へ fallback する。
- 実キャプチャ数と `pcre2_get_ovector_count` を毎回確認する。
- group の pair 読み前に次を確認する。
  - `index < ovector_count`
  - `s/e` が `PCRE2_UNSET` でない
  - `s <= e <= target.size()`
- `get_view_unchecked` は削除するか同じ境界確認を含める。
- `$18446744073709551616` のような overflow、POSIX class 内括弧、extended comment は fast path と通常 path の結果が一致することをテストする。

## `split` と `split_view` のエラー表現

後続 match error を静かに成功値へ変えないため、次の API 変更を行う。

- `context::split()`:
  - `std::expected<std::vector<std::string_view>, std::string>`
  - range 終了時に error があれば部分 vector を破棄して `unexpected`
- `context::split_view()`:
  - `std::generator<std::expected<std::string_view, std::string>>`
  - 通常区間は expected の value
  - 後続 match error は terminal item の `unexpected`
- NTTP/free/regex API も同じ要素型へ合わせる。
- README の利用例とテストを、`expected` を処理する形へ更新する。
- Glaze `find_all_as` は range の完了後にも `range.error()` を確認し、部分 vector を `unexpected` へ置き換える。

## `JITFlags` の伝播

- `iterator` と `match_range` は `UseJIT` に加えて `JITFlags` を template parameter とする。
- `context::find_all`、forward/friend declaration、Glaze helper へ両 parameter を伝播する。
- default-flags の `context` への暗黙変換は行わず、非既定 JIT flag の `find_all` がコンパイル・実行できることをテストする。

## CTRE、CMake、依存関係

- 自動 CTRE fallback を `pcrepp.hpp` から削除する。
- `WITH_CTRE` は benchmark の直接比較専用とし、`pcrepp::pcrepp` の link/compile definition/install dependency に追加しない。
- bounded lookbehind は PCRE2 10.49 の実際の成功を利用する。PCRE2 が拒否する pattern は `expected` の compile error とする。
- namespaced option `PCREPP_BUILD_TESTS` / `PCREPP_BUILD_BENCHMARKS` を追加する。
  - subproject/FetchContent では既定 OFF。
  - top-level の tests は既定 ON。
  - generic `BUILD_TEST` / `BUILD_BENCH` を親 project から上書きしない。
- インストール済み config は CTRE を要求しない。
- セキュリティコメントから非該当 CVE と Onigmo の記述を削除し、PCRE2 10.49 要求の実際の理由と growable-JIT-stack API の適用条件を正確に記述する。

## 回帰テスト

最低限次の再現を追加する。

1. `[[:alpha:](a)]` + `$1` の高速置換で ASan OOB が発生せず、通常 path と一致する。
2. inline/scoped extended comment と POSIX class の capture shape が一致し、不一致時は error/fallback になる。
3. context 解放後も subject 生存中なら result の index/name access が可能。no-match/error 後の stale state と cross-context reuse を拒否する。
4. JIT/JIT disabled で `ANCHORED`、`NO_JIT`、`ENDANCHORED`、invalid UTF、offset/heap/depth limit、inline `LIMIT_*`、replace fallback が一致する。
5. `|a`、`x*`、UTF multibyte、CRLF、split/replace/iterator copy が PCRE2 の global iteration と一致する。
6. 最大サイズ境界・overflow replacement reference が正常 path と同じ結果になる。
7. initial/later range error、copy/move error storage、split、Glaze `find_all_as` が partial success を返さない。
8. bounded/unsupported lookbehind が CTRE delegate を通らず PCRE2 の実際の結果を返す。
9. consumer `add_subdirectory` で Catch2/benchmark を探さずに configure/build でき、install consumer に CTRE が不要である。
10. Glaze と非既定 `JITFlags` を同時にコンパイル・テストできる。

## 検証

- 新規 regression を修正前に実行し、再現に必要なケースが fail することを確認する。
- focused Catch2 tags
- ASan reproduction
- default CTest 全件
- CTRE 設定 CTest 全件
- `WITH_GLAZE` CTest 全件
- WASI no-exception smoke
- consumer subproject/install smoke
- 利用可能な GCC と Clang
- 既存環境で可能なら Emscripten build/test
- MSVC は CI 設定と source の互換性を確認するが、local MSVC 実行済みとは主張しない

全件が合格するまで PR を作成しない。

## PR の境界

変更は影響 API、tests、CMake、関連 README/AGENTS/comment に限定する。`.codegraph/` などの未追跡・無関係な変更には触れない。既存 commit は amend せず、feature branch へ通常 commit し、必要な Co-authored-by trailer を付ける。
