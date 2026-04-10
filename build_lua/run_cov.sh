#!/bin/sh
set -e

if [ "${LUA_SHELL:-}" != "1" ]; then
    echo "plz run under lua-pkg.nix"
    exit 1
fi


SCRIPT_DIR=$(realpath "$(dirname $0)")
BUILD_COV_PATH="$SCRIPT_DIR/build_cov"

export ASAN_OPTIONS=allocator_may_return_null=1:detect_leaks=0

# Lua coverage binary links against the instrumented Lua static library,
# so we profile the fuzzer binary itself.
LLVM_PROFILE_FILE="default_%p.profraw" "$BUILD_COV_PATH/LuaCov"

llvm-profdata merge -sparse $(find ./ -type f -name "*.profraw") $(find ./ -type f -name "*.profdata") -o default.profdata
llvm-cov show "$BUILD_COV_PATH/LuaCov" -instr-profile=default.profdata -o reports
llvm-cov export --format=text -summary-only "$BUILD_COV_PATH/LuaCov" -instr-profile=default.profdata > cov.json
rm -f *.profraw
