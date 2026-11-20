# Review Findings 1–9 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix all nine repository review findings while preserving JIT and compile-time NTTP replacement.

**Architecture:** Make PCRE2 own matching/iteration semantics, give `match_result` explicit state and shared compiled-code ownership, validate NTTP fast-path assumptions against runtime data, and make deferred errors observable through `expected`-bearing APIs. CMake changes isolate optional CTRE/bench/test dependencies from installed and subproject consumers.

**Tech Stack:** C++23 header-only library, PCRE2 10.49, CMake/vcpkg, Catch2, AddressSanitizer, Glaze, optional CTRE benchmark integration.

---

## File map

- Modify `include/pcrepp.hpp`: compiled-code ownership, match state, dispatch, limits, range iteration, NTTP parser/replace, split types, and tuple safety.
- Modify `include/pcrepp_glaze.hpp`: `JITFlags` propagation and delayed range-error propagation.
- Create `test/test_review_regressions.cpp`: focused regressions for findings 1–8 that do not require Glaze.
- Modify `test/test_coverage.cpp`, `test/test_basic.cpp`, `test/test_uncovered_api.cpp`,
  `test/test_zero_width.cpp`, `test/test_japanese.cpp`, `test/test_nttp_find.cpp`,
  and `test/smoke_wasi_minimal.cpp`: migrate every direct `split`/`split_view`
  caller to `expected`.
- Modify `test/test_glaze.cpp`: Glaze error propagation and `JITFlags` regressions.
- Modify `CMakeLists.txt`, `cmake/pcreppConfig.cmake.in`, `test/CMakeLists.txt`, and `bench/CMakeLists.txt`: namespaced options, benchmark-only CTRE, and consumer verification.
- Create `test/consumer/CMakeLists.txt` and `test/consumer/main.cpp`: real subproject consumer without Catch2/benchmark dependency.
- Create `test/run_consumer_smoke.cmake`: configure and build the consumer through CTest.
- Modify `README.md`, `benchmark.md`, and `AGENTS.md`: documented API migration, CMake switches, CTRE behavior, and verification commands.

### Task 1: Match result ownership and explicit state

**Files:**
- Modify: `include/pcrepp.hpp`
- Create: `test/test_review_regressions.cpp`

- [ ] **Step 1: Add the failing state/ownership tests**

Add:

```cpp
TEST_CASE("review: match_result state is not allocation state", "[review][match_result]") {
  auto ctx = pcrepp::context<>::create(R"((?<v>a))").value();
  pcrepp::match_result result{ctx};
  CHECK_FALSE(static_cast<bool>(result));
  CHECK(result.size() == 0uz);

  auto found = ctx.find("a", result);
  REQUIRE(found);
  CHECK(static_cast<bool>(result));
  CHECK(result["v"] == "a");

  found = ctx.find("z", result);
  REQUIRE(found);
  CHECK(*found == 0);
  CHECK_FALSE(static_cast<bool>(result));
  CHECK(result.size() == 0uz);
  CHECK(result["v"].empty());
}

TEST_CASE("review: match_result keeps compiled code alive", "[review][match_result][lifetime]") {
  std::string_view subject = "a";
  pcrepp::match_result result;
  {
    auto ctx = pcrepp::context<>::create(R"((?<v>a))").value();
    result = ctx.find(subject).value();
  }
  CHECK(static_cast<bool>(result));
  CHECK(result.get("v") == "a");
}

TEST_CASE("review: existing result cannot be reused across contexts", "[review][match_result]") {
  auto a = pcrepp::context<>::create("a").value();
  auto b = pcrepp::context<>::create("b").value();
  pcrepp::match_result result{a};
  REQUIRE(a.find("a", result));
  CHECK_FALSE(b.find("a", result));
}
```

- [ ] **Step 2: Run the focused test to verify it fails**

Run:

```sh
cmake --build build --target all_test --parallel
./build/test/all_test "[review][match_result]"
```

Expected: at least the untouched-state and cross-context assertions fail; the lifetime case is marked to be run under ASan in the final verification.

- [ ] **Step 3: Add shared compiled-code ownership and state**

In `include/pcrepp.hpp`:

- replace the raw `context::code` owner with `std::shared_ptr<pcre2_code>` using a deleter that calls `pcre2_code_free`;
- keep `get_code()` returning the raw pointer for the public API;
- make `match_result::data_holder` store that shared owner plus the raw pointer and a `bool matched`;
- make a fresh `pcre2_match_data` when snapshot-copying a `match_result`, copying only the public ovector and target;
- make `match_result` bool/size/access/position/group iteration depend on `matched`;
- clear matched state before each call to the low-level match function and mark it only for a positive result;
- reject `context::find(target, mr, ...)` if `mr` belongs to another compiled pattern; clear stale values before errors and no-match.

- [ ] **Step 4: Run the focused tests and ASan lifetime case**

Run:

```sh
cmake --build build --target all_test --parallel
./build/test/all_test "[review][match_result]"
cmake -S . -B build_asan -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$HOME/vm/vcpkg/scripts/buildsystems/vcpkg.cmake" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DPCREPP_BUILD_BENCHMARKS=OFF \
  -DCMAKE_CXX_FLAGS='-fsanitize=address -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address'
cmake --build build_asan --target all_test --parallel
ASAN_OPTIONS=detect_leaks=1 ./build_asan/test/all_test "[review][match_result][lifetime]"
```

Expected: PASS, no ASan report, zero failed assertions.

- [ ] **Step 5: Commit the ownership/state fix**

```sh
git add include/pcrepp.hpp test/test_review_regressions.cpp
git commit -m "fix: make match results stateful and owned" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

### Task 2: Correct JIT dispatch, full match, and resource limits

**Files:**
- Modify: `include/pcrepp.hpp`
- Modify: `test/test_review_regressions.cpp`

- [ ] **Step 1: Add failing JIT/limit tests**

Add tests for:

```cpp
auto jit = pcrepp::context<true>::create("b").value();
auto nojit = pcrepp::context<false>::create("b").value();
CHECK_FALSE(jit.find("ab", 0uz, PCRE2_ANCHORED).value());
CHECK(jit.match("ab").value() == nojit.match("ab").value());

auto utf_jit = pcrepp::context<true>::create("a", PCRE2_UTF).value();
CHECK_FALSE(utf_jit.find(std::string{char(0xff)}));

auto full_jit = pcrepp::context<true>::create("a|ab").value();
auto full_nojit = pcrepp::context<false>::create("a|ab").value();
CHECK(full_jit.match("ab").value());
CHECK(full_jit.match("ab").value() == full_nojit.match("ab").value());
```

Add an adversarial pattern compiled with both JIT modes:

```cpp
auto subject = std::string(80, 'a') + 'c';
auto setter = pcrepp::context<true>::create("(a+)+b").value();
setter.set_depth_limit(10u);
CHECK_FALSE(setter.find(subject));

auto inline_limit =
  pcrepp::context<true>::create("(*LIMIT_HEAP=1)(a+)+b").value();
CHECK_FALSE(inline_limit.find(subject));

auto replacement = pcrepp::context<true>::create("(*LIMIT_HEAP=1)(a+)+b").value();
CHECK_FALSE(replacement.replace(subject, "x"));
```

Use no assertions about the exact negative code across interpreters; assert error presence and JIT/interpreter equivalence.

- [ ] **Step 2: Run focused tests to verify current behavior fails**

```sh
cmake --build build --target all_test --parallel
./build/test/all_test "[review][jit]"
```

Expected: `ANCHORED`, full-match, and JIT invalid-UTF behavior fail, and pattern limit assertions may expose JIT dispatch instead of the expected limit.

- [ ] **Step 3: Route all matching through `pcre2_match`**

Implement in `include/pcrepp.hpp`:

```cpp
auto effective_match_options(unsigned int option) const noexcept
  -> unsigned int;
```

It must:

- preserve user options;
- add `PCRE2_NO_JIT` for `UseJIT == false`;
- add `PCRE2_NO_JIT` when a setter or inline heap/depth limit is active;
- be shared by match and replacement.

During `compile`, inspect `PCRE2_INFO_HEAPLIMIT` and `PCRE2_INFO_DEPTHLIMIT`; any successful query with a pattern limit sets the JIT-disabled-for-limits state. Replace direct `pcre2_jit_match` calls with:

```cpp
pcre2_match(code.get(), subject, length, start,
            effective_match_options(option), data, match_ctx);
```

Pass `match_ctx` and effective options into `pcre2_substitute`. Make `context::match` pass `PCRE2_ANCHORED | PCRE2_ENDANCHORED`.

- [ ] **Step 4: Run focused and existing option tests**

```sh
cmake --build build --target all_test --parallel
./build/test/all_test "[review][jit],[options],[e3],[e10]"
./build/test/all_test "[basic],[zero_width]"
```

Expected: all selected tests pass.

- [ ] **Step 5: Commit the dispatch/limit fix**

```sh
git add include/pcrepp.hpp test/test_review_regressions.cpp
git commit -m "fix: preserve PCRE2 matching semantics" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

### Task 3: PCRE2 global iteration, range errors, and `JITFlags`

**Files:**
- Modify: `include/pcrepp.hpp`
- Modify: `test/test_review_regressions.cpp`

- [ ] **Step 1: Add failing iteration/range tests**

Add:

```cpp
TEST_CASE("review: zero-width alternation follows PCRE2", "[review][iteration]") {
  auto ctx = pcrepp::context<false>::create("|a").value();
  auto callback = ctx.replace("a", [](auto const&) { return std::string{"X"}; });
  auto native = ctx.replace("a", "X");
  REQUIRE(callback);
  REQUIRE(native);
  CHECK(*callback == *native);
  CHECK(*callback == "XXX");
}

TEST_CASE("review: CRLF zero-width advancement follows PCRE2",
          "[review][iteration][crlf]") {
  auto ctx = pcrepp::context<false>::create(
    R"((?=\r?$))", PCRE2_MULTILINE).value();
  auto range = ctx.find_all("a\r\nb");
  std::vector<size_t> positions;
  for (auto const& match : range) {
    positions.push_back(match.start_pos());
  }
  CHECK(positions == std::vector<size_t>{1uz, 2uz, 4uz});
  CHECK(range);
}

TEST_CASE("review: UTF zero-width iteration is codepoint safe", "[review][iteration][utf]") {
  auto ctx = pcrepp::context<false>::create("", PCRE2_UTF).value();
  std::vector<size_t> starts;
  auto range = ctx.find_all("あ");
  for (auto const& match : range) starts.push_back(match.start_pos());
  CHECK(starts == std::vector<size_t>{0uz, 3uz});
  CHECK(range);
}

TEST_CASE("review: initial range errors are observable", "[review][iteration][error]") {
  auto ctx = pcrepp::context<>::create("a").value();
  auto range = ctx.find_all("a", 0u, 2uz);
  CHECK_FALSE(range);
  CHECK(range.error().find("offset") != std::string_view::npos);
  CHECK(range.begin() == range.end());

  auto copied = range;
  CHECK_FALSE(copied);
  CHECK(copied.error() == range.error());

  auto moved = std::move(copied);
  CHECK_FALSE(moved);
  CHECK(moved.error() == range.error());
}

TEST_CASE("review: non-default JIT flags retain iterator type", "[review][jitflags]") {
  using context_t = pcrepp::context<
    true, PCRE2_JIT_COMPLETE | PCRE2_JIT_PARTIAL_SOFT>;
  auto ctx = context_t::create("a").value();
  auto range = ctx.find_all("a");
  CHECK(std::ranges::distance(range) == 1);
}
```

Add a copied-iterator case:

```cpp
auto first = range.begin();
auto copy = first;
CHECK(copy++->get(0) == "a");
CHECK(++first != range.end());
CHECK(copy == first);
```

Use a two-match target and choose assertions that prove cached `next_match` state survives post-increment and copied iterators.

- [ ] **Step 2: Run the focused tests to verify failure**

```sh
cmake --build build --target all_test --parallel
./build/test/all_test "[review][iteration],[review][jitflags]"
```

Expected: current code reports a UTF bad-offset error, differs from native `|a` replacement, loses initial range error, and the custom-JITFlags source does not compile.

- [ ] **Step 3: Make iteration state use `pcre2_next_match`**

In `include/pcrepp.hpp`:

- add `JITFlags` to `iterator` and `match_range` templates and all declarations/friends;
- make `match_range` member order place `m_error` before `first`/`last`;
- replace the iterator-pair constructor call in `context::find_all` with `match_range(ctx, target, option, start)`;
- after each successful match, call `pcre2_next_match` on the exact match data before exposing the iterator;
- cache `next_offset`, `next_options`, and `has_next` on the iterator;
- have `operator++` consume that cache, perform the next match, and cache the following transition;
- record both initial and later expected errors through `error_ref`.

Use the same transition helper in callback replace and fast replace rather than manual `end + 1` or character arithmetic.

- [ ] **Step 4: Run iteration, zero-width, TLS, and JIT flag tests**

```sh
cmake --build build --target all_test --parallel
./build/test/all_test "[review][iteration],[review][jitflags],[zero_width],[tls]"
```

Expected: all selected tests pass, including copied iterator/range checks.

- [ ] **Step 5: Commit the iteration/range fix**

```sh
git add include/pcrepp.hpp test/test_review_regressions.cpp
git commit -m "fix: delegate global matching to PCRE2" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

### Task 4: Make NTTP capture parsing and fast replacement memory-safe

**Files:**
- Modify: `include/pcrepp.hpp`
- Modify: `test/test_review_regressions.cpp`
- Modify: `test/test_capture_count.cpp`

- [ ] **Step 1: Add count, overflow, and ASan regressions**

Add:

```cpp
TEST_CASE("review: POSIX class cannot overread fast replacement ovector",
          "[review][nttp_replace][fast]") {
  auto fast = pcrepp::replace<R"([[:alpha:](a)])", "$1">("x");
  auto normal = pcrepp::replace<R"([[:alpha:](a)])">("x", "$1");
  CHECK(fast.has_value() == normal.has_value());
  if (fast && normal) CHECK(*fast == *normal);
}

TEST_CASE("review: overflowing replacement references fall back",
          "[review][nttp_replace][overflow]") {
  auto fast = pcrepp::replace<R"((a))", "$18446744073709551616">("a");
  auto normal = pcrepp::replace<R"((a))">(
    "a", "$18446744073709551616");
  CHECK(fast.has_value() == normal.has_value());
  CHECK_FALSE(fast);
}

  TEST_CASE("review: maximum replacement reference stays invalid or falls back",
            "[review][nttp_replace][overflow]") {
    auto fast = pcrepp::replace<R"((a))", "$18446744073709551615">("a");
    auto normal = pcrepp::replace<R"((a))">(
      "a", "$18446744073709551615");
    CHECK(fast.has_value() == normal.has_value());
    if (fast && normal) CHECK(*fast == *normal);
  }
```

If `fast` remains unsafe on the pre-fix build, do not run this test without ASan; the dedicated ASan test must establish the original failure.

- [ ] **Step 2: Run compile-time/static and ASan checks before implementation**

```sh
cmake --build build --target all_test --parallel
./build/test/all_test "[review][nttp_replace][overflow]"
cmake --build build_asan --target all_test --parallel
ASAN_OPTIONS=detect_leaks=0 \
  ./build_asan/test/all_test \
  "[review][nttp_replace][fast]"
```

Expected: overflow behavior diverges from the normal PCRE2 path, and ASan reports the ovector heap-buffer-overflow. Do not add the new static assertions in this step; a failing static assertion would prevent construction of the ASan reproducer.

- [ ] **Step 3: Harden capture counting and fast replacement**

In `include/pcrepp.hpp`:

- parse POSIX inner delimiters inside a character class without closing the outer class early;
- track inline/scoped extended mode and ignore whitespace/comment text for capture counting;
- in NTTP structured `find`, compare compile-time group count with actual `capture_count + 1` and return `unexpected` on mismatch;
- replace saturated decimal replacement parsing with:

```cpp
constexpr auto max = nttp_repl_token::saturated;
if (number > (max - digit) / 10uz) {
  reference_valid = false;
  break;
}
number = number * 10uz + digit;
```

- make the fast-plan validator fall back for actual-count mismatch, unknown name, invalid reference, or an index not below ovector pair count;
- bounds-check each pair before `target.substr` and never emit when validation is incomplete;
- either remove `get_view_unchecked` or convert it to the same safe range logic.

After the parser implementation exists, add these compile-time regressions to
`test/test_capture_count.cpp`:

```cpp
static_assert(count_capture_groups(R"([[:alpha:](a)])") == 0);
static_assert(count_capture_groups(R"((?x)# (ignored)
(a))") == 1);
static_assert(count_capture_groups(R"((?x:# (ignored)
(a)))") == 1);
```

- [ ] **Step 4: Run static, fast replace, and ASan tests**

```sh
cmake --build build --target all_test --parallel
./build/test/all_test "[capture_count],[nttp_replace],[review][nttp_replace]"
cmake --build build_asan --target all_test --parallel
ASAN_OPTIONS=detect_leaks=1 \
  ./build_asan/test/all_test \
  "[review][nttp_replace],[review][match_result][lifetime]"
```

Expected: all selected tests pass and ASan has no report.

- [ ] **Step 5: Commit the NTTP safety fix**

```sh
git add include/pcrepp.hpp test/test_capture_count.cpp test/test_review_regressions.cpp
git commit -m "fix: validate NTTP replacement captures" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

### Task 5: Error-bearing split and Glaze propagation

**Files:**
- Modify: `include/pcrepp.hpp`
- Modify: `include/pcrepp_glaze.hpp`
- Modify: `test/test_review_regressions.cpp`
- Modify: `test/test_basic.cpp`
- Modify: `test/test_coverage.cpp`
- Modify: `test/test_uncovered_api.cpp`
- Modify: `test/test_zero_width.cpp`
- Modify: `test/test_japanese.cpp`
- Modify: `test/test_nttp_find.cpp`
- Modify: `test/smoke_wasi_minimal.cpp`
- Modify: `test/test_glaze.cpp`
- Modify: `README.md`

- [ ] **Step 1: Add failing split/error tests and migrate compile sites**

Change direct eager split call sites to:

```cpp
auto split = ctx.split(target);
REQUIRE(split);
CHECK(split->size() == ...);
CHECK((*split)[0] == ...);
```

Change generator tests:

```cpp
for (auto part : ctx.split_view(target)) {
  REQUIRE(part);
  use(*part);
}
```

Add a late-error test with a pattern/subject that matches once, then exhausts a low match limit:

```cpp
auto ctx = pcrepp::context<false>::create(
  R"(z|(x+x+)+y)").value();
ctx.set_match_limit(100);
auto input = "z " + std::string(26, 'x') + " q";
auto result = ctx.split(input);
CHECK_FALSE(result);
```

For `split_view`, collect yielded values and require the last item to be `unexpected`. For Glaze `find_all_as`, use the same late-error setup and assert the call returns `unexpected` instead of a partial vector.

- [ ] **Step 2: Run the focused build to establish failure**

```sh
cmake --build build --target all_test --parallel
./build/test/all_test "[review][split]"
cmake -S . -B build_glaze_review -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$HOME/vm/vcpkg/scripts/buildsystems/vcpkg.cmake" \
  -DWITH_GLAZE=ON \
  -DPCREPP_BUILD_BENCHMARKS=OFF
cmake --build build_glaze_review --target test_glaze --parallel
./build_glaze_review/test/test_glaze "[review][split]"
```

Expected: existing API/tests cannot represent the expected-wrapped split or delayed Glaze error, and compilation fails at the migrated call sites.

- [ ] **Step 3: Implement expected-bearing split APIs**

In `include/pcrepp.hpp`:

- change eager split to `std::expected<std::vector<std::string_view>, std::string>`;
- if the range has an error after traversal, discard partial segments and return `unexpected{std::string{range.error()}}`;
- change `split_view` to `std::generator<std::expected<std::string_view, std::string>>`;
- yield one terminal `unexpected` for an initial or later range error;
- update NTTP/free/`nttp_regex` split and split_view to preserve these exact types.

In `include/pcrepp_glaze.hpp`:

- parameterize `find_as`/`find_all_as` over `UseJIT, JITFlags`;
- check range `error()` after `find_all_as` iteration and discard partial results;
- compile and test non-default JIT flags.

Where Glaze calls the NTTP cache, propagate `JITFlags` through
`try_get_nttp_context`, `get_nttp_context`, `find`, `find_all`, `match`,
`replace`, `split`, `compile_utf`, and `nttp_match_result`/`nttp_regex`. Give the
new template argument the existing `PCRE2_JIT_COMPLETE` default so old call
sites keep their types. Add one direct compile test for the NTTP Glaze helper,
not just the runtime-context helper.

Update README split examples and type descriptions in the same commit.

- [ ] **Step 4: Run split, Glaze, and general tests**

```sh
cmake --build build --target all_test --parallel
./build/test/all_test "[split],[review][split]"
cmake --build build_glaze_review --target test_glaze --parallel
./build_glaze_review/test/test_glaze "[review],[glaze]"
```

Expected: migrated tests pass and delayed matching errors are observed.

- [ ] **Step 5: Commit split/Glaze error propagation**

```sh
git add include/pcrepp.hpp include/pcrepp_glaze.hpp \
  test/test_review_regressions.cpp test/test_basic.cpp \
  test/test_coverage.cpp test/test_uncovered_api.cpp \
  test/test_zero_width.cpp test/test_japanese.cpp \
  test/test_nttp_find.cpp test/smoke_wasi_minimal.cpp \
  test/test_glaze.cpp README.md
git commit -m "fix: expose split and Glaze iteration errors" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

### Task 6: Isolate tests and CTRE from library consumers

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `cmake/pcreppConfig.cmake.in`
- Modify: `bench/CMakeLists.txt`
- Modify: `test/CMakeLists.txt`
- Create: `test/consumer/CMakeLists.txt`
- Create: `test/consumer/main.cpp`
- Create: `test/run_consumer_smoke.cmake`
- Modify: `test/test_review_regressions.cpp`
- Modify: `include/pcrepp.hpp`
- Modify: `AGENTS.md`
- Modify: `benchmark.md`

- [ ] **Step 1: Add consumer and lookbehind tests**

Create a minimal consumer that supports both a subproject and an installed
package. The consumer has no vcpkg manifest of its own; the smoke script passes
the parent repository with `VCPKG_MANIFEST_DIR` plus the same toolchain, so the
consumer sees the pcrepp manifest-installed `PCRE2` and `FastFloat` packages.

```cmake
cmake_minimum_required(VERSION 3.25)
project(pcrepp_consumer LANGUAGES CXX)
if(DEFINED PCREPP_INSTALL_PREFIX)
  find_package(pcrepp CONFIG REQUIRED
    PATHS "${PCREPP_INSTALL_PREFIX}" NO_DEFAULT_PATH)
else()
  add_subdirectory("${PCREPP_SOURCE_DIR}" pcrepp EXCLUDE_FROM_ALL)
endif()
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE pcrepp::pcrepp)
```

`main.cpp` compiles:

```cpp
#include "pcrepp.hpp"
int main() {
  auto result = pcrepp::find<"a+">("aaa");
  return result && *result ? 0 : 1;
}
```

`test/run_consumer_smoke.cmake` accepts `VCPKG_TOOLCHAIN_FILE` and
`PCREPP_INSTALL_PREFIX`. It configures the subproject case with:

```cmake
-DPCREPP_SOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/..
-DCMAKE_TOOLCHAIN_FILE=${VCPKG_TOOLCHAIN_FILE}
-DVCPKG_MANIFEST_DIR=${CMAKE_CURRENT_LIST_DIR}/..
-DCMAKE_DISABLE_FIND_PACKAGE_Catch2=TRUE
-DCMAKE_DISABLE_FIND_PACKAGE_benchmark=TRUE
-DCMAKE_DISABLE_FIND_PACKAGE_ctre=TRUE
```

It reads the generated cache and requires
`PCREPP_BUILD_TESTS:BOOL=OFF` and `PCREPP_BUILD_BENCHMARKS:BOOL=OFF`, then builds
the consumer and requires no `all_test`, `test_glaze`, or `bench_main` target.
If `PCREPP_INSTALL_PREFIX` is supplied, it configures a second independent build
with `-DPCREPP_INSTALL_PREFIX=...` and the same disabled package lookups, then
builds that executable. `find_package(pcrepp)` must therefore work when CTRE is
unavailable. The installed-package configure also receives the parent
`VCPKG_MANIFEST_DIR`, because it still needs the manifest-installed `PCRE2` and
`FastFloat`.

Add lookbehind tests that assert:

- PCRE2-accepted bounded variable lookbehind works with and without `WITH_CTRE`;
- PCRE2-rejected lookbehind returns `std::unexpected`, not a CTRE compilation error.

- [ ] **Step 2: Run the CMake smoke before changing configuration**

Run:

```sh
cmake \
  -DVCPKG_TOOLCHAIN_FILE="$HOME/vm/vcpkg/scripts/buildsystems/vcpkg.cmake" \
  -P test/run_consumer_smoke.cmake
```

Expected: configure fails because subproject mode currently enables tests and tries `find_package(Catch2)`.

- [ ] **Step 3: Namespace options and isolate CTRE**

In `CMakeLists.txt`:

- add `PCREPP_BUILD_TESTS`, default ON only when `PROJECT_IS_TOP_LEVEL`;
- add `PCREPP_BUILD_BENCHMARKS`, default OFF;
- use these options for `add_subdirectory(test)` and `add_subdirectory(bench)`;
- accept `BUILD_TEST`/`BUILD_BENCH` only as documented top-level legacy aliases, without reading parent values as pcrepp defaults;
- find/link `ctre` only inside the benchmark when `WITH_CTRE=ON`;
- keep `ctre` off the public `pcrepp` target and remove its generated package dependency.
- remove the inaccurate security-version comments and replace them with the
  PCRE2 10.49 actual applicability note from the approved design.

Remove `#include <ctre.hpp>`, CTRE recommendation parsing, and automatic delegation from `include/pcrepp.hpp`. In `bench/CMakeLists.txt`, link `ctre::ctre` and define `WITH_CTRE=1` only for the benchmark target.

Add `consumer_smoke` as a top-level CTest test. Update AGENTS and benchmark notes to state that CTRE is a direct benchmark comparator, not an automatic fallback.

Register it without relying on shell environment inherited by a developer:

```cmake
add_test(
  NAME consumer_smoke
  COMMAND "${CMAKE_COMMAND}"
    "-DVCPKG_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/run_consumer_smoke.cmake")
```

The script must treat a missing toolchain path as a hard error instead of
silently configuring a consumer without the manifest dependencies.

- [ ] **Step 4: Run subproject, installed, default, and CTRE checks**

```sh
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$HOME/vm/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake -S . -B build_ctre \
  -DCMAKE_TOOLCHAIN_FILE="$HOME/vm/vcpkg/scripts/buildsystems/vcpkg.cmake" \
  -DWITH_CTRE=ON \
  -DPCREPP_BUILD_BENCHMARKS=ON
cmake --build build_ctre --parallel
ctest --test-dir build_ctre --output-on-failure
cmake --install build --prefix "$PWD/build/install"
cmake \
  -DVCPKG_TOOLCHAIN_FILE="$HOME/vm/vcpkg/scripts/buildsystems/vcpkg.cmake" \
  -DPCREPP_INSTALL_PREFIX="$PWD/build/install" \
  -P test/run_consumer_smoke.cmake
! grep -R "find_dependency(ctre" build/install
```

Expected: top-level tests remain, subproject tests/bench are absent, CTRE is absent from installed config, and lookbehind calls take PCRE2 behavior without CTRE template compilation.

- [ ] **Step 5: Commit CMake/CTRE isolation**

```sh
git add CMakeLists.txt cmake/pcreppConfig.cmake.in bench/CMakeLists.txt \
  test/CMakeLists.txt test/consumer test/run_consumer_smoke.cmake \
  include/pcrepp.hpp test/test_review_regressions.cpp AGENTS.md benchmark.md
git commit -m "build: isolate optional tests and CTRE" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

### Task 7: Final integration, review, and PR

**Files:**
- Modify only where final verification exposes a defect
- Review all files changed by Tasks 1–6

- [ ] **Step 1: Run focused regression and all required configurations**

```sh
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --build build_ctre --parallel
ctest --test-dir build_ctre --output-on-failure
cmake --build build_asan --target all_test --parallel
ASAN_OPTIONS=detect_leaks=1 ./build_asan/test/all_test "[review]"
cmake --build build --target smoke_wasi_minimal
./build/test/smoke_wasi_minimal
./build/test/all_test --list-tests >/dev/null
```

For an existing Emscripten/WASI environment, run the repository-equivalent cross build. Do not claim a local MSVC run unless one is actually performed.

Configure and run dedicated Glaze and Clang trees:

```sh
cmake -S . -B build_glaze_review -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$HOME/vm/vcpkg/scripts/buildsystems/vcpkg.cmake" \
  -DWITH_GLAZE=ON \
  -DPCREPP_BUILD_BENCHMARKS=OFF
cmake --build build_glaze_review --parallel
ctest --test-dir build_glaze_review --output-on-failure

cmake -S . -B build_clang_review -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$HOME/vm/vcpkg/scripts/buildsystems/vcpkg.cmake" \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DWITH_GLAZE=ON \
  -DPCREPP_BUILD_BENCHMARKS=OFF
cmake --build build_clang_review --parallel
ctest --test-dir build_clang_review --output-on-failure
```

- [ ] **Step 2: Inspect the complete diff and rerun failures**

```sh
git --no-pager diff --check
git --no-pager diff --stat
git --no-pager diff main...HEAD
git --no-pager status --short
```

Map each changed hunk to exactly one of findings 1–9. If a final test fails, return to the owning task, add/adjust the smallest regression, fix, and rerun that task’s complete selector before continuing.

- [ ] **Step 3: Run an independent code review**

Use `superpowers:requesting-code-review`/the code-review agent against `main...HEAD` with the approved specification and this plan. Fix only substantiated blockers and rerun affected verification.

- [ ] **Step 4: Push the feature branch**

```sh
git push -u origin fix/review-findings-1-9
```

- [ ] **Step 5: Create the PR**

Create a PR with:

- title: `fix: address repository review findings`
- summary grouped into memory/state, PCRE2 semantics, replacement safety, and build/package isolation;
- test commands and results;
- explicit note that `split` and `split_view` now expose errors through `expected`;
- no claim of platforms not actually executed.

Use:

```sh
cat > build/pcrepp-review-pr.md <<'EOF'
## Summary

- add explicit match-result ownership/state and remove stale match behavior
- preserve PCRE2 JIT, UTF, anchoring, limits, and global-iteration semantics
- harden NTTP fast replacement and expose deferred split/Glaze errors
- isolate tests/benchmarks/CTRE from installed and subproject consumers

## Verification

- default CTest
- CTRE CTest
- Glaze CTest
- GCC and Clang builds
- ASan review regressions
- no-exception smoke and subproject/install consumer smoke

## Compatibility

`split` and `split_view` now expose matching errors with `expected`.
Only platforms listed in the actual final verification are claimed here.
EOF

gh pr create --base main --head fix/review-findings-1-9 \
  --title "fix: address repository review findings" \
  --body-file build/pcrepp-review-pr.md
```

`build/` is ignored and remains outside the commit. Remove the body file after
`gh pr create` only if it is no longer needed.

Then inspect the created PR and final branch/worktree state.

---

## Stop criteria

Do not start PR creation until:

- all new `[review]` regressions pass in normal and ASan configurations;
- default and CTRE CTest report zero failures;
- no-exception smoke and consumer smoke pass;
- installed/subproject consumers do not require Catch2, benchmark, or CTRE implicitly;
- the independent review has no unresolved blocker;
- `git diff --check` passes and only intended tracked files are changed.
