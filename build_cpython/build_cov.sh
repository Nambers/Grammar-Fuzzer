#!/bin/bash
set -e

SCRIPT_DIR=$(realpath "$(dirname $0)")
BUILD_PATH="$SCRIPT_DIR/build_cov"
USING_CORE=$(( $(nproc) - 1 ))

mkdir -p "$BUILD_PATH"

export CC=clang
export CXX=clang++

cmake -B "$BUILD_PATH" "$SCRIPT_DIR"
cmake --build "$BUILD_PATH" -j "$USING_CORE" --target CPythonCov
