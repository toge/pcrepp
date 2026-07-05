find_package(Catch2 REQUIRED CONFIG)
find_package(frozenchars CONFIG)   # REQUIRED を外す — CI 環境では不要

file(GLOB test_src test_*.cpp)
list(FILTER test_src EXCLUDE REGEX "test_glaze\\.cpp$")

if(EMSCRIPTEN)
  # Emscripten: vcpkg がインストールした Catch2 静的ライブラリは
  # -sNO_DISABLE_EXCEPTION_CATCHING なしでコンパイルされているため、
  # Catch2 の例外翻訳関数 (translateActiveException / handleExceptionMatchExpr) が
  # 正しく動作しない。ここでは Catch2 のソースをアマルガメーションファイル経由で
  # 再コンパイルすることで、例外サポートを有効にする。
  include(FetchContent)
  FetchContent_Declare(
    catch2_emscripten
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.15.1
    GIT_SHALLOW TRUE
    SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/catch2_emscripten-src
  )
  FetchContent_GetProperties(catch2_emscripten)
  if(NOT catch2_emscripten_POPULATED)
    FetchContent_Populate(catch2_emscripten)
  endif()
  add_library(catch2_emscripten_core STATIC
    "${catch2_emscripten_SOURCE_DIR}/extras/catch_amalgamated.cpp"
  )
  target_include_directories(catch2_emscripten_core
    BEFORE PUBLIC
      "${catch2_emscripten_SOURCE_DIR}/extras"
  )
  target_compile_options(catch2_emscripten_core
    PRIVATE "-sNO_DISABLE_EXCEPTION_CATCHING"
  )
  target_compile_features(catch2_emscripten_core PRIVATE cxx_std_17)
  target_link_options(catch2_emscripten_core
    INTERFACE "-sNO_DISABLE_EXCEPTION_CATCHING"
  )

  # Catch2::Catch2WithMain は Catch2::Catch2 に依存するため使えない。
  # 代わりに em_catch2_main.cpp が main() を提供する。
  list(APPEND test_src em_catch2_main.cpp)
  set(_catch2_targets catch2_emscripten_core)
else()
  set(_catch2_targets Catch2::Catch2WithMain)
endif()

add_executable(all_test ${test_src})

if(frozenchars_FOUND)
  target_link_libraries(all_test PRIVATE pcrepp ${_catch2_targets} frozenchars::frozenchars)
  target_compile_definitions(all_test PRIVATE PCREPP_TEST_HAS_FROZENCHARS=1)
else()
  target_link_libraries(all_test PRIVATE pcrepp ${_catch2_targets})
  target_compile_definitions(all_test PRIVATE PCREPP_TEST_HAS_FROZENCHARS=0)
endif()

if(EMSCRIPTEN)
  target_compile_options(all_test PRIVATE "-sNO_DISABLE_EXCEPTION_CATCHING")
endif()

target_include_directories(all_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(all_test PRIVATE cxx_std_23)
add_test(NAME all_test COMMAND all_test)

if(WITH_GLAZE)
  add_executable(test_glaze test_glaze.cpp)
  target_link_libraries(test_glaze PRIVATE pcrepp ${_catch2_targets} glaze::glaze)
  target_include_directories(test_glaze PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_features(test_glaze PRIVATE cxx_std_23)
  add_test(NAME test_glaze COMMAND test_glaze)
endif()
