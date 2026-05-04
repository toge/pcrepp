cmake_minimum_required(VERSION 3.25)
project(pcrepp LANGUAGES CXX)

option(ENABLE_TESTS "Enable tests" ON)

## add stdc++ compiler flag
include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++26" COMPILER_SUPPORTS_CXX26)
CHECK_CXX_COMPILER_FLAG("-std=c++23" COMPILER_SUPPORTS_CXX23)
CHECK_CXX_COMPILER_FLAG("-std=c++20" COMPILER_SUPPORTS_CXX20)
if(COMPILER_SUPPORTS_CXX26)
    set(STD_CPP cxx_std_26)
elseif(COMPILER_SUPPORTS_CXX23)
    set(STD_CPP cxx_std_23)
elseif(COMPILER_SUPPORTS_CXX20)
    set(STD_CPP cxx_std_20)
else()
    message(FATAL_ERROR "The compiler ${CMAKE_CXX_COMPILER} has no C++20 support. Please use a different C++ compiler.")
endif()

## add warning
if (CMAKE_COMPILER_IS_GNUCXX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g3")
endif()

if (MSVC)
    if(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
        string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    else()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
    endif()
endif()

## optimization
if (CMAKE_COMPILER_IS_GNUCXX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native")
endif()
if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Ox")
endif()

## other flag
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_package(PCRE2 CONFIG REQUIRED)

add_library(pcrepp INTERFACE)
add_library(pcrepp::pcrepp ALIAS pcrepp)

target_compile_features(pcrepp INTERFACE ${STD_CPP})
target_link_libraries(pcrepp INTERFACE PCRE2::8BIT)
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
