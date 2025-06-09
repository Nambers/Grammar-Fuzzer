#!/bin/sh

set -e

COV_PATH=$(readlink -f cov)
BUILD_PATH=$COV_PATH/build

# do llvm-cov

llvm-profdata merge -sparse $(find ./ -type f -name "*.profraw") -o default.profdata
llvm-cov show $CPYTHON_LIB/libpython3.14.so.1.0 -instr-profile=default.profdata -o reports
llvm-cov export --format=text -summary-only $CPYTHON_LIB/libpython3.14.so.1.0 -instr-profile=default.profdata > cov.json
