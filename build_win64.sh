#! /bin/sh

if [ "$(uname -m)" == "aarch64" ]; then
    export CXXFLAG="-O3 -mcpu=native"
    export CFLAG="-O3 -mcpu=native"
else
    export CXXFLAG="-O3 -march=native"
    export CFLAG="-O3 -march=native"
fi

export VCPKG_ROOT=$(readlink -f ~/vm/vcpkg)
export VCPKG_OVERLAY_PORTS="${VCPKG_ROOT}/ports"
export VCPKG_TARGET_TRIPLET=x64-mingw-static

export CXX=x86_64-w64-mingw32-g++
export CC=x86_64-w64-mingw32-gcc

cmake -B build_win64 \
    -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" \
    -DVCPKG_TARGET_TRIPLET=x64-mingw-static \
    -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
    -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
    -DCMAKE_BUILD_TYPE=Release \
    "-DCMAKE_CXX_FLAGS=${CXXFLAG}" \
    "-DCMAKE_C_FLAGS=${CFLAG}" \
    -S .
RET=$?
if [ $RET -ne 0 ]; then
    exit $RET;
fi

cmake --build build_win64 --verbose --parallel 4
RET=$?
if [ $RET -ne 0 ]; then
    exit $RET;
fi
