cmake_minimum_required(VERSION 3.25)
project(pcrepp VERSION 0.1.0 LANGUAGES CXX)

set(PCREPP_BUILD_TESTS_DEFAULT OFF)
if(PROJECT_IS_TOP_LEVEL)
  set(PCREPP_BUILD_TESTS_DEFAULT ON)
endif()

option(PCREPP_BUILD_TESTS "Build tests" ${PCREPP_BUILD_TESTS_DEFAULT})
option(PCREPP_BUILD_BENCHMARKS "Build benchmarks" OFF)
option(WITH_CTRE "Enable CTRE in benchmark target only" OFF)
option(WITH_GLAZE "Enable Glaze reflection integration (requires glaze)" OFF)

if(PROJECT_IS_TOP_LEVEL)
  option(BUILD_TEST "Legacy alias of PCREPP_BUILD_TESTS" ${PCREPP_BUILD_TESTS})
  option(BUILD_BENCH "Legacy alias of PCREPP_BUILD_BENCHMARKS" ${PCREPP_BUILD_BENCHMARKS})

  if(NOT BUILD_TEST STREQUAL PCREPP_BUILD_TESTS)
    set(PCREPP_BUILD_TESTS ${BUILD_TEST} CACHE BOOL "Build tests" FORCE)
  endif()
  if(NOT BUILD_BENCH STREQUAL PCREPP_BUILD_BENCHMARKS)
    set(PCREPP_BUILD_BENCHMARKS ${BUILD_BENCH} CACHE BOOL "Build benchmarks" FORCE)
  endif()

  set(BUILD_TEST ${PCREPP_BUILD_TESTS} CACHE BOOL "Legacy alias of PCREPP_BUILD_TESTS" FORCE)
  set(BUILD_BENCH ${PCREPP_BUILD_BENCHMARKS} CACHE BOOL "Legacy alias of PCREPP_BUILD_BENCHMARKS" FORCE)
endif()

set(PCREPP_OPTIMIZATION_FLAG_REGEX "(^|[ \t])-O[^ \t]+")
set(PCREPP_CPU_TUNING_FLAG_REGEX "(^|[ \t])-(march|mcpu|mtune)=[^ \t]+")

macro(pcrepp_configure_default_native_flags language)
    # Emscripten / WASI はクロスターゲットのため、ホスト向けの
    # -O3 / -march=native 付与を行わない (wasm ターゲットでは不正オプションになる)。
    if(NOT EMSCRIPTEN AND NOT CMAKE_SYSTEM_NAME STREQUAL "WASI")
        set(_flags_var "CMAKE_${language}_FLAGS")
        if(${_flags_var} MATCHES "${PCREPP_OPTIMIZATION_FLAG_REGEX}")
            foreach(_config RELEASE RELWITHDEBINFO MINSIZEREL)
                set(_config_flags_var "CMAKE_${language}_FLAGS_${_config}")
                string(REGEX REPLACE "${PCREPP_OPTIMIZATION_FLAG_REGEX}" " " ${_config_flags_var} "${${_config_flags_var}}")
                string(STRIP "${${_config_flags_var}}" ${_config_flags_var})
            endforeach()
        else()
            add_compile_options($<$<COMPILE_LANGUAGE:${language}>:-O3>)
        endif()

        if(NOT ${_flags_var} MATCHES "${PCREPP_CPU_TUNING_FLAG_REGEX}")
            add_compile_options($<$<COMPILE_LANGUAGE:${language}>:-march=native>)
        endif()
    endif()
endmacro()

## add warning and optimization
## これらのフラグは pcrepp 自体をビルドする場合のみ適用する。
## add_subdirectory / FetchContent で取り込んだコンシューマのビルドには
## 漏出させない (-march=native 等が利用者環境でポータビリティを壊すため)
if(PROJECT_IS_TOP_LEVEL)
    if(CMAKE_COMPILER_IS_GNUCXX)
        add_compile_options(-Wall -Wextra -pedantic -g3)
        pcrepp_configure_default_native_flags(CXX)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        add_compile_options(-Wall -Wextra -pedantic)
        pcrepp_configure_default_native_flags(CXX)
    elseif(MSVC)
        if(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
            string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        else()
            add_compile_options(/W4 /Ox)
        endif()
    endif()
endif()

## other flag
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_package(PCRE2 CONFIG REQUIRED)
find_package(FastFloat CONFIG REQUIRED)

add_library(pcrepp INTERFACE)
add_library(pcrepp::pcrepp ALIAS pcrepp)

target_compile_features(pcrepp INTERFACE cxx_std_23)
target_link_libraries(pcrepp INTERFACE PCRE2::8BIT FastFloat::fast_float)
target_include_directories(pcrepp
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(WITH_GLAZE)
  find_package(glaze CONFIG REQUIRED)
  target_link_libraries(pcrepp INTERFACE glaze::glaze)
  target_compile_definitions(pcrepp INTERFACE PCREPP_GLAZE=1)
endif()

install(TARGETS pcrepp
    EXPORT pcreppTargets
)
install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pcrepp.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
if(WITH_GLAZE)
  install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/pcrepp_glaze.hpp
      DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()

if(WITH_GLAZE)
  set(WITH_GLAZE_FIND_DEP "find_dependency(glaze CONFIG)")
else()
  set(WITH_GLAZE_FIND_DEP "")
endif()

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/pcreppConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/pcreppConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pcrepp"
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/pcreppConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/pcreppConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/pcreppConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pcrepp"
)

install(EXPORT pcreppTargets
    FILE pcreppTargets.cmake
    NAMESPACE pcrepp::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pcrepp"
)

if(PCREPP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

if(PCREPP_BUILD_BENCHMARKS)
    add_subdirectory(bench)
endif()
