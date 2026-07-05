cmake_minimum_required(VERSION 3.25)
project(pcrepp VERSION 0.1.0 LANGUAGES CXX)

option(BUILD_TEST "Enable tests" ON)
option(BUILD_BENCH "Build benchmarks" OFF)
option(WITH_CTRE "Enable automatic fallback to CTRE for PCRE2-unfriendly patterns" OFF)
option(WITH_GLAZE "Enable Glaze reflection integration (requires glaze)" OFF)

set(PCREPP_OPTIMIZATION_FLAG_REGEX "(^|[ \t])-O[^ \t]+")
set(PCREPP_CPU_TUNING_FLAG_REGEX "(^|[ \t])-(march|mcpu|mtune)=[^ \t]+")

macro(pcrepp_configure_default_native_flags language)
    if(NOT EMSCRIPTEN)
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

## other flag
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_package(PCRE2 CONFIG REQUIRED)
find_package(FastFloat CONFIG REQUIRED)

if(WITH_CTRE)
  find_package(ctre CONFIG REQUIRED)
endif()

add_library(pcrepp INTERFACE)
add_library(pcrepp::pcrepp ALIAS pcrepp)

target_compile_features(pcrepp INTERFACE cxx_std_23)
target_link_libraries(pcrepp INTERFACE PCRE2::8BIT FastFloat::fast_float)
target_include_directories(pcrepp
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(WITH_CTRE)
  target_link_libraries(pcrepp INTERFACE ctre::ctre)
  target_compile_definitions(pcrepp INTERFACE WITH_CTRE=1)
endif()

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
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pcrepp_glaze.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

if(WITH_CTRE)
  set(WITH_CTRE_FIND_DEP "find_dependency(ctre CONFIG)")
else()
  set(WITH_CTRE_FIND_DEP "")
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

if(BUILD_TEST)
    enable_testing()
    add_subdirectory(test)
endif()

if(BUILD_BENCH)
    add_subdirectory(bench)
endif()
