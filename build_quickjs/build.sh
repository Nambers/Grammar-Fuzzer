#!/bin/bash
set -e

if [ "${QUICKJS_SHELL:-}" != "1" ]; then
    echo "plz run under quickjs-pkg.nix"
    exit 1
fi

SCRIPT_DIR=$(realpath "$(dirname $0)")
ROOT_DIR="$SCRIPT_DIR/.."
TGT_DIR="$ROOT_DIR/targets/QuickJS"
BUILD_PATH="$SCRIPT_DIR/build"
USING_CORE=$(( $(nproc) - 1 ))
CMAKE_ARG=""

if [ ! -f builtins.json ]; then
    node "$TGT_DIR/builtins_gen.js" builtins.json
fi

while [ "$1" != "" ]; do
    case $1 in
    -dd | --disable-debug-output)
        CMAKE_ARG="$CMAKE_ARG -DDISABLE_DEBUG_OUTPUT=ON"
        ;;
    -di | --disable-info-output)
        CMAKE_ARG="$CMAKE_ARG -DDISABLE_INFO_OUTPUT=ON"
        ;;
    *)
        echo "Invalid argument $1"
        exit
        ;;
    esac
    shift
done

export CC=clang
export CXX=clang++

echo "[build_quickjs] Building quickjsFuzzer..."
cmake -B "$BUILD_PATH" $CMAKE_ARG "$SCRIPT_DIR"
cmake --build "$BUILD_PATH" -j "$USING_CORE" --target quickjsFuzzer QuickJSTest QuickJSConvert QuickJSCov

echo "[build_quickjs] Done."
