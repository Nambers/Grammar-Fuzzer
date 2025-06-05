#!/bin/bash
set -e

###
# build the pyFuzzer
###

if ! echo "$PATH" | grep -q '/nix/store'; then
    echo "This script is intended to be run inside a Nix shell."
    echo "Use build_wrapper.sh"
    exit 1
fi

pushd() {
    pushd "$@" > /dev/null
}

popd() {
    popd "$@" > /dev/null
}

WORK_DIR=$(readlink -f .)
SCRIPT_DIR=$(readlink -f ./scripts)
cd $WORK_DIR

CPYTHON_VERSION=3.13.0b3
BUILD_PATH=$(readlink -f build)
USING_CORE=$(( $(nproc) - 1 ))
CMAKE_ARG=""

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

cmake -B $BUILD_PATH $CMAKE_ARG .
cmake --build $BUILD_PATH -- -j $USING_CORE

python3 src/driver/python/builtins.py src/driver/python/builtins.json

