#!/usr/bin/env bash
set -euo pipefail

BUILD_PATH=$(readlink -f build)

mkdir -p corpus/tmp corpus/queue corpus/done corpus/saved

echo "[run.sh] Cleaning old corpus..."
rm -f corpus/tmp/* corpus/queue/* corpus/done/*

export ASAN_OPTIONS=allocator_may_return_null=1:detect_leaks=0
export PYTHONWARNINGS=ignore
export PYTHONUNBUFFERED=x

clear

$BUILD_PATH/pyFuzzer -load-saved 2> errlog.txt

echo "[run.sh] Fuzzer exited with code $EXIT_CODE"
exit $EXIT_CODE
