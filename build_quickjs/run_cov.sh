#!/bin/sh
set -e

if [ "${QUICKJS_SHELL:-}" != "1" ]; then
    echo "plz run under quickjs-pkg.nix"
    exit 1
fi

SCRIPT_DIR=$(realpath "$(dirname $0)")
BUILD_COV_PATH="$SCRIPT_DIR/build_cov"

export ASAN_OPTIONS=allocator_may_return_null=1:detect_leaks=0

LLVM_PROFILE_FILE="default_%p.profraw" "$BUILD_COV_PATH/QuickJSCov"

llvm-profdata merge -sparse *.profraw *.profdata -o default.profdata
llvm-cov show "$BUILD_COV_PATH/QuickJSCov" -instr-profile=default.profdata -o reports
llvm-cov export --format=text -summary-only "$BUILD_COV_PATH/QuickJSCov" -instr-profile=default.profdata > cov.json
rm -f *.profraw
