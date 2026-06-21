cmake_minimum_required(VERSION 3.25)
project(pcrepp VERSION 0.1.0 LANGUAGES CXX)

option(ENABLE_TESTS "Enable tests" ON)
option(BUILD_BENCH "Build benchmarks" OFF)

## add warning and optimization
if(CMAKE_COMPILER_IS_GNUCXX)
    add_compile_options(-Wall -Wextra -pedantic -g3 -O3 -march=native)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-Wall -Wextra -pedantic -O3 -march=native)
elseif(MSVC)
    if(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
        string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    else()
        add_compile_options(/W4 /Ox)
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

install(TARGETS pcrepp
    EXPORT pcreppTargets
)
install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pcrepp.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

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

if(ENABLE_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

if(BUILD_BENCH)
    add_subdirectory(bench)
endif()
