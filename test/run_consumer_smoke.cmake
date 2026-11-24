if(NOT DEFINED VCPKG_TOOLCHAIN_FILE OR VCPKG_TOOLCHAIN_FILE STREQUAL "")
  message(FATAL_ERROR "VCPKG_TOOLCHAIN_FILE is required")
endif()

function(assert_cache_value cache_file variable_name expected_value)
  file(STRINGS "${cache_file}" cache_entry REGEX "^${variable_name}:[^=]*=")
  if(NOT cache_entry)
    message(FATAL_ERROR "Cache entry '${variable_name}' not found in ${cache_file}")
  endif()
  string(REGEX REPLACE "^[^=]*=" "" actual_value "${cache_entry}")
  if(NOT actual_value STREQUAL "${expected_value}")
    message(FATAL_ERROR "Cache entry '${variable_name}' expected '${expected_value}' but was '${actual_value}'")
  endif()
endfunction()

set(consumer_source_dir "${CMAKE_CURRENT_LIST_DIR}/consumer")
get_filename_component(project_source_dir "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(DEFINED CONSUMER_SMOKE_ROOT AND NOT CONSUMER_SMOKE_ROOT STREQUAL "")
  set(smoke_root "${CONSUMER_SMOKE_ROOT}")
else()
  set(smoke_root "${project_source_dir}/build")
endif()
string(SHA256 smoke_key "${project_source_dir}:${CMAKE_HOST_SYSTEM_NAME}")
set(subproject_build_dir "${smoke_root}/consumer-subproject-smoke-${smoke_key}")
set(install_prefix "${smoke_root}/consumer-install-prefix-${smoke_key}")
set(install_build_dir "${smoke_root}/consumer-install-build-${smoke_key}")
set(installed_build_dir "${smoke_root}/consumer-installed-smoke-${smoke_key}")
set(alias_case1_build_dir "${smoke_root}/alias-case1-${smoke_key}")
set(alias_case2_build_dir "${smoke_root}/alias-case2-${smoke_key}")
set(alias_stale_build_dir "${smoke_root}/alias-stale-${smoke_key}")
set(alias_subproject_source_dir "${smoke_root}/alias-subproject-src-${smoke_key}")
set(alias_subproject_build_dir "${smoke_root}/alias-subproject-build-${smoke_key}")

file(REMOVE_RECURSE
  "${subproject_build_dir}"
  "${install_prefix}"
  "${install_build_dir}"
  "${installed_build_dir}"
  "${alias_case1_build_dir}"
  "${alias_case2_build_dir}"
  "${alias_stale_build_dir}"
  "${alias_subproject_source_dir}"
  "${alias_subproject_build_dir}"
)

set(_common_configure_args
  -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_TOOLCHAIN_FILE=${VCPKG_TOOLCHAIN_FILE}
  -DVCPKG_MANIFEST_MODE=ON
  -DVCPKG_MANIFEST_DIR=${project_source_dir}
)

set(_consumer_configure_args
  ${_common_configure_args}
  -DCMAKE_DISABLE_FIND_PACKAGE_Catch2=TRUE
  -DCMAKE_DISABLE_FIND_PACKAGE_benchmark=TRUE
  -DCMAKE_DISABLE_FIND_PACKAGE_ctre=TRUE
)

execute_process(
  COMMAND ${CMAKE_COMMAND}
    -S ${project_source_dir}
    -B ${alias_case1_build_dir}
    -DPCREPP_BUILD_TESTS=OFF
    -DBUILD_TEST=ON
    -DPCREPP_BUILD_BENCHMARKS=OFF
    -DBUILD_BENCH=OFF
    ${_common_configure_args}
  COMMAND_ERROR_IS_FATAL ANY
)
assert_cache_value("${alias_case1_build_dir}/CMakeCache.txt" "PCREPP_BUILD_TESTS" "OFF")

execute_process(
  COMMAND ${CMAKE_COMMAND}
    -S ${project_source_dir}
    -B ${alias_case2_build_dir}
    -DPCREPP_BUILD_TESTS=ON
    -DBUILD_TEST=OFF
    -DPCREPP_BUILD_BENCHMARKS=OFF
    -DBUILD_BENCH=OFF
    ${_common_configure_args}
  COMMAND_ERROR_IS_FATAL ANY
)
assert_cache_value("${alias_case2_build_dir}/CMakeCache.txt" "PCREPP_BUILD_TESTS" "ON")

execute_process(
  COMMAND ${CMAKE_COMMAND}
    -S ${project_source_dir}
    -B ${alias_stale_build_dir}
    -DBUILD_TEST=ON
    -DPCREPP_BUILD_BENCHMARKS=OFF
    -DBUILD_BENCH=OFF
    ${_common_configure_args}
  COMMAND_ERROR_IS_FATAL ANY
)
execute_process(
  COMMAND ${CMAKE_COMMAND}
    -S ${project_source_dir}
    -B ${alias_stale_build_dir}
    -DPCREPP_BUILD_TESTS=OFF
    -DPCREPP_BUILD_BENCHMARKS=OFF
    -DBUILD_BENCH=OFF
    ${_common_configure_args}
  COMMAND_ERROR_IS_FATAL ANY
)
assert_cache_value("${alias_stale_build_dir}/CMakeCache.txt" "PCREPP_BUILD_TESTS" "OFF")

file(MAKE_DIRECTORY "${alias_subproject_source_dir}")
file(WRITE "${alias_subproject_source_dir}/CMakeLists.txt"
  "cmake_minimum_required(VERSION 3.25)\n"
  "project(pcrepp_subproject_alias_probe LANGUAGES CXX)\n"
  "add_subdirectory(\"${project_source_dir}\" pcrepp-src)\n"
)

execute_process(
  COMMAND ${CMAKE_COMMAND}
    -S ${alias_subproject_source_dir}
    -B ${alias_subproject_build_dir}
    -DBUILD_TEST=ON
    -DBUILD_BENCH=ON
    ${_consumer_configure_args}
  COMMAND_ERROR_IS_FATAL ANY
)
assert_cache_value("${alias_subproject_build_dir}/CMakeCache.txt" "PCREPP_BUILD_TESTS" "OFF")
assert_cache_value("${alias_subproject_build_dir}/CMakeCache.txt" "PCREPP_BUILD_BENCHMARKS" "OFF")
assert_cache_value("${alias_subproject_build_dir}/CMakeCache.txt" "BUILD_TEST" "ON")
assert_cache_value("${alias_subproject_build_dir}/CMakeCache.txt" "BUILD_BENCH" "ON")

execute_process(
  COMMAND ${CMAKE_COMMAND}
    -S ${consumer_source_dir}
    -B ${subproject_build_dir}
    -DPCREPP_SOURCE_DIR=${project_source_dir}
    -DPCREPP_CONSUMER_USE_INSTALLED=OFF
    -DPCREPP_BUILD_TESTS=OFF
    -DPCREPP_BUILD_BENCHMARKS=OFF
    ${_consumer_configure_args}
  COMMAND_ERROR_IS_FATAL ANY
)
execute_process(
  COMMAND ${CMAKE_COMMAND} --build ${subproject_build_dir} --target consumer_smoke
  COMMAND_ERROR_IS_FATAL ANY
)
execute_process(
  COMMAND ${subproject_build_dir}/consumer_smoke
  COMMAND_ERROR_IS_FATAL ANY
)

execute_process(
  COMMAND ${CMAKE_COMMAND}
    -S ${project_source_dir}
    -B ${install_build_dir}
    -DPCREPP_BUILD_TESTS=OFF
    -DPCREPP_BUILD_BENCHMARKS=OFF
    -DWITH_CTRE=OFF
    -DCMAKE_INSTALL_PREFIX=${install_prefix}
    ${_consumer_configure_args}
  COMMAND_ERROR_IS_FATAL ANY
)
execute_process(
  COMMAND ${CMAKE_COMMAND} --build ${install_build_dir} --target install
  COMMAND_ERROR_IS_FATAL ANY
)

execute_process(
  COMMAND ${CMAKE_COMMAND}
    -S ${consumer_source_dir}
    -B ${installed_build_dir}
    -DPCREPP_CONSUMER_USE_INSTALLED=ON
    -DCMAKE_PREFIX_PATH=${install_prefix}
    ${_common_configure_args}
  COMMAND_ERROR_IS_FATAL ANY
)
execute_process(
  COMMAND ${CMAKE_COMMAND} --build ${installed_build_dir} --target consumer_smoke
  COMMAND_ERROR_IS_FATAL ANY
)
execute_process(
  COMMAND ${installed_build_dir}/consumer_smoke
  COMMAND_ERROR_IS_FATAL ANY
)
