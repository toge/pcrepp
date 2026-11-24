cmake_minimum_required(VERSION 3.25)
project(pcrepp_consumer_smoke LANGUAGES CXX)

option(PCREPP_CONSUMER_USE_INSTALLED "Use find_package(pcrepp)" OFF)

if(PCREPP_CONSUMER_USE_INSTALLED)
  find_package(pcrepp CONFIG REQUIRED)
else()
  if(NOT DEFINED PCREPP_SOURCE_DIR)
    message(FATAL_ERROR "PCREPP_SOURCE_DIR is required when PCREPP_CONSUMER_USE_INSTALLED=OFF")
  endif()
  add_subdirectory(${PCREPP_SOURCE_DIR} pcrepp-subproject)
endif()

add_executable(consumer_smoke main.cpp)
target_link_libraries(consumer_smoke PRIVATE pcrepp::pcrepp)
target_compile_features(consumer_smoke PRIVATE cxx_std_23)

