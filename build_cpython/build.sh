#!/bin/bash
set -e

###
# Build the pyFuzzer (CPython target)
###

if ! echo "$PATH" | grep -q '/nix/store'; then
    echo "This script is intended to be run inside a Nix shell."
    echo "Use build_wrapper.sh"
    exit 1
fi

SCRIPT_DIR=$(realpath "$(dirname $0)")
BUILD_PATH="$SCRIPT_DIR/build"
USING_CORE=$(( $(nproc) - 1 ))
CMAKE_ARG=""

if [ ! -f builtins.json ]; then
    python3 "$SCRIPT_DIR/../targets/CPython/builtins.py" builtins.json
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

cmake -B "$BUILD_PATH" $CMAKE_ARG "$SCRIPT_DIR"
cmake --build "$BUILD_PATH" -j "$USING_CORE" --target pyFuzzer CPythonTest CPythonConvert
