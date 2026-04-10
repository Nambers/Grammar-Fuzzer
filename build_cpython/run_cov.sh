#!/bin/sh
set -e

if [ "${CPYTHON_COV_SHELL:-}" != "1" ]; then
    echo "plz run under cpython-cov.nix"
    exit 1
fi

SCRIPT_DIR=$(realpath "$(dirname $0)")
BUILD_COV_PATH="$SCRIPT_DIR/build_cov"

export ASAN_OPTIONS=allocator_may_return_null=1:detect_leaks=0
export PYTHONWARNINGS=ignore
export PYTHONUNBUFFERED=x

LLVM_PROFILE_FILE="default_%p.profraw" "$BUILD_COV_PATH/CPythonCov"

llvm-profdata merge -sparse $(find ./ -type f -name "*.profraw") $(find ./ -type f -name "*.profdata") -o default.profdata
llvm-cov show "$CPYTHON_LIB/libpython3.14.so.1.0" -instr-profile=default.profdata -o reports
llvm-cov export --format=text -summary-only "$CPYTHON_LIB/libpython3.14.so.1.0" -instr-profile=default.profdata > cov.json
# delete all profraw because it's already in default.profdata
rm -f *.profraw
