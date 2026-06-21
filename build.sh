#! /bin/sh

BUILD_DIR=build
if [ $# -ne 0 ] && [ "$1" = "static" ]; then
    BUILD_DIR=build_static
fi

if [ "$(uname -m)" = "aarch64" ]; then
    export CXXFLAG="-O3 -mcpu=native"
    export CFLAG="-O3 -mcpu=native"
else
    export CXXFLAG="-O3 -march=native"
    export CFLAG="-O3 -march=native"
fi

if [ -e "conanfile.py" ]; then
    export CONAN_LOG_RUN_TO_OUTPUT=1
    export CONAN_LOGGING_LEVEL=10
    export CONAN_PRINT_RUN_COMMANDS=1
    conan install -of ${BUILD_DIR} . --build=missing
    RET=$?
    if [ $RET -ne 0 ]; then
        exit $RET;
    fi

    cmake -B ${BUILD_DIR} -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release "-DCMAKE_CXX_FLAGS=${CXXFLAG}" "-DCMAKE_C_FLAGS=${CFLAG}" -S .
else
    export VCPKG_ROOT=$(readlink -f ~/vm/vcpkg)
    # export VCPKG_ROOT=$(readlink -f ~/src/github-vcpkg)
    export VCPKG_OVERLAY_PORTS="${VCPKG_ROOT}/ports"
    export VCPKG_TARGET_TRIPLET=x64-linux-static
    # export VCPKG_TARGET_TRIPLET=arm64-osx-static
    cmake -B ${BUILD_DIR} -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake -DCMAKE_BUILD_TYPE=Release "-DCMAKE_CXX_FLAGS=${CXXFLAG}" "-DCMAKE_C_FLAGS=${CFLAG}" -S .
fi

RET=$?
if [ $RET -ne 0 ]; then
    exit $RET;
fi

cmake --build ${BUILD_DIR} --verbose --parallel 4
RET=$?
if [ $RET -ne 0 ]; then
    exit $RET;
fi
