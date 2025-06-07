#!/bin/bash

set -e

BUILD_PATH=$(readlink -f build_cov)
USING_CORE=$(( $(nproc) - 1 ))

mkdir -p $BUILD_PATH

cmake -B $BUILD_PATH ./
cmake --build $BUILD_PATH --target CPythonCov -- -j $USING_CORE
