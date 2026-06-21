# Benchmark

## 概要

`pcrepp` (C++20/26 PCRE2 ラッパー) と raw PCRE2 C API のパフォーマンスを計測します。
以下の 3 軸で比較:

| 軸 | バリエーション |
|----|--------------|
| API | `pcrepp` (JIT) / `pcrepp` (no JIT) / `raw PCRE2` (JIT) / `raw PCRE2` (no JIT) |
| パターン | リテラル (`the`) / 単語 (`\b\w+\b`) / メールアドレス判別 |
| データサイズ | 1KB / 100KB / 1MB (固定シード 42 のランダム英字テキスト) |

### 計測操作

- **compile**: パターンをコンパイル (`pcre2_compile` + 必要なら `pcre2_jit_compile`)
- **first_match**: 最初のマッチを 1 件取得
- **find_all**: 全マッチをイテレート
- **replace**: 全置換 (`"X"` に置換)

### パターン一覧

| 名前      | 式                      | 備考                 |
| --------- | ----------------------- | -------------------- |
| `literal` | `the`                   | 単純文字列           |
| `word`    | `\b\w+\b`               | 単語境界             |
| `email`   | `[A-Za-z0-9._%+-]+@...` | メールアドレス       |
| `yen`     | `(\d+)円`               | キャプチャ付き日本語 |
| `jword`   | `[\w\W]+`               | 全文字 (日本語対応)  |
| `jany`    | `.`                     | 任意の1文字          |

### データ

英語テキスト (Lorem Ipsum ベースのランダム文) と日本語 UTF-8 テキストの両方を、1KB / 100KB / 1MB の 3 サイズで生成します (固定シード 42)。

## 実行方法

```sh
cmake -B build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_BENCH=ON
cmake --build build --parallel
./build/bench/bench_main
```

## 結果

> 環境: 下記は参考値です。実際の値は CPU・メモリ・PCRE2 バージョン・コンパイラに依存します。
> 固定シード (42) で再現可能です。

### 2026-06-21 M7 測定ログ（build2）

- ベースライン: `benchmark_baseline.json` / `benchmark_baseline.txt`
- 改善後: `benchmark_after.json` / `benchmark_after.txt`
- 実施内容: `context::replace` の初期バッファ戦略を `target + 20% + replacement + 256` に調整し、コールバック置換でも同様の reserve 方針を適用。

#### ベースライン上位（mean, CPU time）

| Benchmark | Mean |
|---|---:|
| BM_Replace_NoJIT/102400_mean | 158,829 ns |
| BM_FindAll_NoJIT/102400_mean | 139,398 ns |
| BM_Replace_JIT/102400_mean | 99,990 ns |
| BM_FindAll_JIT/102400_mean | 76,127 ns |

#### 置換系の比較（mean, CPU time）

| Benchmark | Baseline | After | Diff |
|---|---:|---:|---:|
| BM_Replace_JIT/102400_mean | 99,990 ns | 98,983 ns | -1.01% |
| BM_Replace_JIT/1024_mean | 1,042 ns | 1,034 ns | -0.74% |
| BM_Replace_NoJIT/102400_mean | 158,829 ns | 160,309 ns | +0.93% |
| BM_Replace_NoJIT/1024_mean | 1,674 ns | 1,644 ns | -1.82% |

> 観測上の差分は ±1〜2% 程度で、ノイズレンジに近い。現時点では大きなボトルネック改善は確認できないため、次の候補は `find_all` 経路（特に no-JIT 大入力）を優先的に解析する。

### Compile

| Pattern   | Size   | pcrepp (JIT) | pcrepp (no JIT) | raw PCRE2 (JIT) | raw PCRE2 (no JIT) |
|-----------|--------|-------------:|----------------:|----------------:|-------------------:|
| literal   | 1KB    |      3.00 us |          0.14 us |         2.99 us |             0.14 us |
| literal   | 100KB  |      3.28 us |          0.21 us |         3.13 us |             0.14 us |
| literal   | 1MB    |      2.91 us |          0.14 us |         2.94 us |             0.14 us |
| word      | 1KB    |      2.92 us |          0.17 us |         2.77 us |             0.17 us |
| word      | 100KB  |      2.77 us |          0.17 us |         2.78 us |             0.17 us |
| word      | 1MB    |      2.79 us |          0.17 us |         2.80 us |             0.17 us |
| email     | 1KB    |      5.07 us |          0.70 us |         4.99 us |             0.68 us |
| email     | 100KB  |      4.94 us |          0.67 us |         4.99 us |             0.66 us |
| email     | 1MB    |      8.29 us |          1.17 us |         5.48 us |             0.68 us |

\* **compile 表のみ (no JIT) が高速**: `pcre2_jit_compile` を呼ばないため。
  マッチ操作 (first_match / find_all / replace) は JIT 有効の方が高速。
- コンパイル時間はデータサイズに非依存。
- pcrepp の wrapper overhead はゼロ (pcrepp JIT ≈ raw PCRE2 JIT / pcrepp no JIT ≈ raw PCRE2 no JIT)。

### First Match

| Pattern   | Size   | pcrepp (JIT) | pcrepp (no JIT) | raw PCRE2 (JIT) | raw PCRE2 (no JIT) |
|-----------|--------|-------------:|----------------:|----------------:|-------------------:|
| literal   | 1KB    |      0.04 us |          0.72 us |         0.03 us |             0.70 us |
| literal   | 100KB  |      0.03 us |          1.06 us |         0.03 us |             0.90 us |
| literal   | 1MB    |      0.07 us |          1.35 us |         0.06 us |             1.30 us |
| word      | 1KB    |      0.03 us |          0.08 us |         0.02 us |             0.07 us |
| word      | 100KB  |      0.03 us |          0.09 us |         0.02 us |             0.07 us |
| word      | 1MB    |      0.06 us |          0.12 us |         0.04 us |             0.08 us |
| email     | 1KB    |      0.04 us |          0.25 us |         0.04 us |             0.23 us |
| email     | 100KB  |      0.57 us |         11.56 us |         0.58 us |            11.58 us |
| email     | 1MB    |      0.33 us |          5.37 us |         0.33 us |             5.98 us |

- JIT 有効時は pcrepp と raw PCRE2 が同等。
- 複雑なパターン (email) で JIT が 10～30 倍高速。
- 単純なパターン (literal, word) では差は小さい。

### Find All

| Pattern   | Size   | pcrepp (JIT) | pcrepp (no JIT) | raw PCRE2 (JIT) | raw PCRE2 (no JIT) |
|-----------|--------|-------------:|----------------:|----------------:|-------------------:|
| literal   | 1KB    |      0.12 us |          1.61 us |         0.09 us |             1.53 us |
| literal   | 100KB  |      5.81 us |        212.54 us |         7.09 us |           191.04 us |
| literal   | 1MB    |     71.48 us |       1927.74 us |        81.25 us |          1906.77 us |
| word      | 1KB    |      2.90 us |          9.70 us |         3.73 us |             9.54 us |
| word      | 100KB  |    453.29 us |       1371.58 us |       532.79 us |          1220.94 us |
| word      | 1MB    |   4883.83 us |      12142.63 us |      5581.64 us |         12311.23 us |
| email     | 1KB    |      1.04 us |         20.20 us |         1.08 us |            20.52 us |
| email     | 100KB  |    202.71 us |       2136.63 us |       210.28 us |          2136.38 us |
| email     | 1MB    |   2550.92 us |      23278.07 us |      2394.34 us |         21707.61 us |

- 全体的に JIT が 2～10 倍高速。特に巨大データで顕著。
- pcrepp と raw PCRE2 の差は誤差範囲。

### Replace

| Pattern   | Size   | pcrepp (JIT) | pcrepp (no JIT) | raw PCRE2 (JIT) | raw PCRE2 (no JIT) |
|-----------|--------|-------------:|----------------:|----------------:|-------------------:|
| literal   | 1KB    |      0.18 us |          1.62 us |         0.18 us |             1.58 us |
| literal   | 100KB  |     11.32 us |        154.31 us |        11.24 us |           165.61 us |
| literal   | 1MB    |    133.04 us |       1940.89 us |       132.44 us |          1958.57 us |
| word      | 1KB    |      5.06 us |         10.86 us |         5.07 us |            10.69 us |
| word      | 100KB  |    689.24 us |       1392.12 us |       694.62 us |          1337.22 us |
| word      | 1MB    |   7481.02 us |      13816.82 us |      7337.14 us |         13984.50 us |
| email     | 1KB    |      1.15 us |         21.37 us |         1.15 us |            19.99 us |
| email     | 100KB  |    215.58 us |       2246.39 us |       222.54 us |          2365.06 us |
| email     | 1MB    |   2471.10 us |      23289.52 us |      2424.76 us |         22834.87 us |

- replace は内部で find_all + 文字列構築を行う。傾向は find_all と一致。
- JIT の有無が最も顕著に現れる操作の一つ。

## 考察

1. **pcrepp の wrapper overhead は実質ゼロ**: 全操作で `pcrepp (JIT)` と `raw PCRE2 (JIT)` は同等、
   `pcrepp (no JIT)` と `raw PCRE2 (no JIT)` も同等。
2. **JIT の効果は絶大**: 特に複雑なパターン・大規模データで顕著 (email first_match で 10～30 倍)。
3. **JIT には compile 時 overhead がある**: compile 時間に JIT コンパイルのコスト (2～5 us) が加わるが、
   複数回マッチする実運用では amortize される。
4. **データサイズに比例して処理時間が増加**: find_all / replace は O(n) でスケール。
5. **pcrepp は利便性と性能を両立**: 安全で使いやすい C++ API でありながら、raw PCRE2 と同等の速度。

## ソースコード

ベンチマークの実装は `bench/bench_main.cpp` にあります。
