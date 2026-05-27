# TLS Match Data Buffer Reuse Implementation Plan

> **For Gemini:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement thread-local reuse of `pcre2_match_data` to minimize allocation overhead during pattern matching.

**Architecture:** 
1. Introduce a thread-local `tls_match_data_cache` in the `detail` namespace to manage reusable `pcre2_match_data`.
2. Add low-level matching methods in `context` that accept raw `pcre2_match_data*`.
3. Provide a `use_tls` option for users and optimize internal NTTP/replacement methods using the TLS cache.

**Tech Stack:** C++26, PCRE2

---

### Task 1: Infrastructure and Internal APIs

**Files:**
- Modify: `include/pcrepp.hpp`

**Step 1: Add necessary headers**

Add `#include <cstring>` at the top.

**Step 2: Implement `tls_match_data_cache`**

Add the cache manager in `namespace detail`.

```cpp
namespace detail {
struct tls_match_data_cache {
  pcre2_match_data* data = nullptr;
  uint32_t capture_count = 0;

  ~tls_match_data_cache() {
    if (data) pcre2_match_data_free(data);
  }

  auto get(pcre2_code const* code) -> pcre2_match_data* {
    uint32_t cc;
    pcre2_pattern_info(code, PCRE2_INFO_CAPTURECOUNT, &cc);
    if (!data || cc > capture_count) {
      if (data) pcre2_match_data_free(data);
      data = pcre2_match_data_create_from_pattern(code, nullptr);
      capture_count = cc;
    }
    return data;
  }
};

inline auto get_tls_match_data(pcre2_code const* code) -> pcre2_match_data* {
  static thread_local tls_match_data_cache cache;
  return cache.get(code);
}
}
```

**Step 3: Add `match_result` factory from raw data**

Add a private constructor to `match_result`.

```cpp
private:
  match_result(pcre2_code const* code, pcre2_match_data* src_data, std::string_view target) {
    if (code && src_data) {
      holder = std::make_shared<data_holder>(code);
      auto const count = pcre2_get_ovector_count(src_data);
      std::memcpy(holder->ovector, pcre2_get_ovector_pointer(src_data), sizeof(size_t) * count * 2uz);
      holder->target = target;
    }
  }
```

**Step 4: Refactor `context::find` to support raw `pcre2_match_data*`**

Update `context::find` implementations to reuse logic.

---

### Task 2: Optimization of NTTP and User Options

**Files:**
- Modify: `include/pcrepp.hpp`

**Step 1: Define `use_tls` tag**

```cpp
struct use_tls_t {};
inline constexpr use_tls_t use_tls{};
```

**Step 2: Implement `context::find(target, use_tls)`**

Returns a `match_result` that copies from the TLS buffer.

**Step 3: Optimize `pcrepp::find<Pattern>`**

Update to use TLS buffer directly for extracting results into `nttp_match_result`.

---

### Task 3: Verification

**Files:**
- Create: `test/test_tls_reuse.cpp`

**Step 1: Write tests for TLS reuse**

Verify that multiple matches using TLS work correctly and don't leak/interfere.

**Step 2: Run all tests**
