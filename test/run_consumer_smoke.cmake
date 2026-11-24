if(NOT DEFINED VCPKG_TOOLCHAIN_FILE OR VCPKG_TOOLCHAIN_FILE STREQUAL "")
  message(FATAL_ERROR "VCPKG_TOOLCHAIN_FILE is required")
endif()

set(consumer_source_dir "${CMAKE_CURRENT_LIST_DIR}/consumer")
get_filename_component(project_source_dir "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(subproject_build_dir "${project_source_dir}/build/consumer-subproject-smoke")
set(install_prefix "${project_source_dir}/build/consumer-install-prefix")
set(install_build_dir "${project_source_dir}/build/consumer-install-build")
set(installed_build_dir "${project_source_dir}/build/consumer-installed-smoke")

file(REMOVE_RECURSE
  "${subproject_build_dir}"
  "${install_prefix}"
  "${install_build_dir}"
  "${installed_build_dir}"
)

set(_common_configure_args
  -G Ninja
  -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_TOOLCHAIN_FILE=${VCPKG_TOOLCHAIN_FILE}
  -DVCPKG_MANIFEST_MODE=ON
  -DVCPKG_MANIFEST_DIR=${project_source_dir}
  -DCMAKE_DISABLE_FIND_PACKAGE_Catch2=TRUE
  -DCMAKE_DISABLE_FIND_PACKAGE_benchmark=TRUE
  -DCMAKE_DISABLE_FIND_PACKAGE_ctre=TRUE
)

execute_process(
  COMMAND ${CMAKE_COMMAND}
    -S ${consumer_source_dir}
    -B ${subproject_build_dir}
    -DPCREPP_SOURCE_DIR=${project_source_dir}
    -DPCREPP_CONSUMER_USE_INSTALLED=OFF
    -DPCREPP_BUILD_TESTS=OFF
    -DPCREPP_BUILD_BENCHMARKS=OFF
    ${_common_configure_args}
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
    ${_common_configure_args}
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
