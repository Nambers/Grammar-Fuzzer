#!/bin/bash
set -e

if [ "${CPYTHON_COV_SHELL:-}" != "1" ]; then
    echo "plz run under cpython-cov.nix, or use build_wrapper.sh"
    exit 1
fi

SCRIPT_DIR=$(realpath "$(dirname $0)")
BUILD_PATH="$SCRIPT_DIR/build_cov"
USING_CORE=$(( $(nproc) - 1 ))

mkdir -p "$BUILD_PATH"

export CC=clang
export CXX=clang++

cmake -B "$BUILD_PATH" "$SCRIPT_DIR"
cmake --build "$BUILD_PATH" -j "$USING_CORE" --target CPythonCov
