#!/usr/bin/env bash
set -euo pipefail

if [ "${QUICKJS_SHELL:-}" != "1" ]; then
    echo "plz run under quickjs-pkg.nix"
    exit 1
fi

SCRIPT_DIR=$(realpath "$(dirname $0)")
BUILD_COV_PATH="$SCRIPT_DIR/build_cov"

mkdir -p "$BUILD_COV_PATH"
cd "$BUILD_COV_PATH"

cmake "$SCRIPT_DIR" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_COVERAGE=ON

cmake --build . --target QuickJSCov -j "$(nproc)"
echo "[build_cov.sh] Coverage build complete at $BUILD_COV_PATH"
