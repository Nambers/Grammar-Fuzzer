#!/bin/bash

set -e

BUILD_PATH=$(readlink -f build_cov)
USING_CORE=$(( $(nproc) - 1 ))

mkdir -p $BUILD_PATH

export CC=clang
export CXX=clang++

cmake -B $BUILD_PATH ./
cmake --build $BUILD_PATH -j $USING_CORE --target CPythonCov
