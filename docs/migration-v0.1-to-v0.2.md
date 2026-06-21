# pcrepp v0.1 → v0.2 マイグレーションガイド

## 破壊的変更

### 1. C++ 標準フロアが C++23 に引き上げ
- 必要: GCC 14+ / Clang 18+ / MSVC 19.36+

### 2. `pcrepp::find<Pattern>()` が `std::expected` を返すよう変更

**旧 (v0.1):**
```cpp
auto [matched, whole, key] = pcrepp::find<R"((\w+))">("hello");
if (!matched) return;
```

**新 (v0.2):**
```cpp
auto r = pcrepp::find<R"((\w+))">("hello");
if (!r) { /* compile/match error: r.error() */ return; }
auto [matched, whole, key] = *r;
if (!matched) return;
```

**または throw 版 (旧コードに近い形):**
```cpp
auto [matched, whole, key] = pcrepp::find_unchecked<R"((\w+))">("hello");
if (!matched) return;
```

### 3. `context::replace(target, callback)` が `expected` を返すよう変更

**旧:**
```cpp
std::string result = ctx.replace(target, [](auto& m){ return "..."; });
```

**新:**
```cpp
auto r = ctx.replace(target, [](auto const& m){ return "..."; });
if (!r) { /* error */ return; }
std::string result = *r;
// または
std::string result = ctx.replace_unchecked(target, [](auto const& m){ return "..."; });
```

### 4. `nttp_regex::match()` が `expected` を返すよう変更

**旧:**
```cpp
bool matched = re.match(target);
```

**新:**
```cpp
auto r = re.match(target);
if (!r) { /* error */ return; }
bool matched = *r;
```

### 5. `match_result::operator[](int)` の負値が `std::out_of_range` を投げるよう変更

### 6. `context::match()` が終端位置手動検証に変更（JIT + PCRE2_ENDANCHORED 非互換対処）

内部実装変更。動作は同じ（完全一致判定）。

## 新規機能
- `find_all(target, option, start)` — start 引数
- `split(target, option)` — option 引数、`split_view()` — lazy 版
- `try_get<std::string/std::string_view>()` — 文字列版
- `context::match(target, option)` — match_result 不要版
- `match_result::to_tuple<N>()`
- `std::formatter<nttp_match_result>`
- `nttp_regex::replace / split`
- `substitute_flags::*`
- `context::capture_count/named_captures/pattern_size/jit_size/options`
- `context::set_*_limit / set_offset_limit`
- `context<UseJIT, JITFlags>`
