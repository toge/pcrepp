#! /bin/sh

if [ "$(uname -m)" = "aarch64" ]; then
    export CXXFLAG="-O3 -mcpu=native"
    export CFLAG="-O3 -mcpu=native"
else
    export CXXFLAG="-O3 -march=native"
    export CFLAG="-O3 -march=native"
fi


export CONAN_LOG_RUN_TO_OUTPUT=1
export CONAN_LOGGING_LEVEL=10
export CONAN_PRINT_RUN_COMMANDS=1
conan install -of build_win64 . --build=outdated -pr linux_to_win64
RET=$?
if [ $RET -ne 0 ]; then
    exit $RET;
fi

export CXX=x86_64-w64-mingw32-g++
export C=x86_64-w64-mingw32-gcc

mingw64-cmake -B build_win64 -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release "-DCMAKE_CXX_FLAGS=${CXXFLAG}" "-DCMAKE_C_FLAGS=${CFLAG}" -S .
RET=$?
if [ $RET -ne 0 ]; then
    exit $RET;
fi


cmake --build build_win64 --verbose --parallel 4
RET=$?
if [ $RET -ne 0 ]; then
    exit $RET;
fi
