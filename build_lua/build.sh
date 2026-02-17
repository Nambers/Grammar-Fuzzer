#!/bin/bash
set -e

SCRIPT_DIR=$(realpath "$(dirname $0)")
ROOT_DIR="$SCRIPT_DIR/.."
TGT_DIR="$ROOT_DIR/targets/Lua"
BUILD_PATH="$SCRIPT_DIR/build"
USING_CORE=$(( $(nproc) - 1 ))
CMAKE_ARG=""

if [ ! -f builtins.json ]; then
    lua "$SCRIPT_DIR/../targets/Lua/builtins_gen.lua" builtins.json
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

echo "[build_lua] Generating builtins.json..."
lua "$TGT_DIR/builtins_gen.lua" "$TGT_DIR/builtins.json"

echo "[build_lua] Building luaFuzzer..."
cmake -B "$BUILD_PATH" $CMAKE_ARG "$SCRIPT_DIR"
cmake --build "$BUILD_PATH" -j "$USING_CORE" --target luaFuzzer LuaTest LuaConvert LuaCov

echo "[build_lua] Done."
