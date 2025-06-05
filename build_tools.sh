#!/bin/bash

set -e

COV_PATH=$(readlink -f tools)
BUILD_PATH=$COV_PATH/build
USING_CORE=$(( $(nproc) - 1 ))

cmake -B $BUILD_PATH $COV_PATH
cmake --build $BUILD_PATH -- -j $USING_CORE
