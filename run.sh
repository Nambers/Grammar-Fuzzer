#!/usr/bin/env bash
set -euo pipefail

BUILD_PATH=$(readlink -f build)
COV_PATH=$(readlink -f cov)
BUILD_COV_PATH=$COV_PATH/build

mkdir -p corpus/tmp corpus/queue corpus/done corpus/saved

echo "[run.sh] Cleaning old corpus..."
rm -f corpus/tmp/* corpus/queue/* corpus/done/* corpus/saved/*

cleanup() {
    echo ""
    echo "[run.sh] Caught SIGINT, shutting down fuzzer and cov..."

    echo "[run.sh] Killing fuzzer (PID=$FUZZ_PID)..."
    kill -s SIGINT "$FUZZ_PID" 2>/dev/null || true
    wait "$FUZZ_PID" 2>/dev/null || true

    echo "[run.sh] Waiting 1s before killing cov..."
    sleep 1

    echo "[run.sh] Killing cov (PID=$COV_PID)..."
    kill -s SIGINT "$COV_PID" 2>/dev/null || true
    wait "$COV_PID" 2>/dev/null || true

    echo "[run.sh] Clean exit."
    exit 0
}
trap cleanup SIGINT

export ASAN_OPTIONS=allocator_may_return_null=1:detect_leaks=0;

LLVM_PROFILE_FILE="default_%p.profraw" $BUILD_COV_PATH/CPythonCov &
COV_PID=$!

# -load-saved to load previously saved corpus
$BUILD_PATH/pyFuzzer -load-saved &
FUZZ_PID=$!

wait "$FUZZ_PID"
kill "$COV_PID" 2>/dev/null || true
wait "$COV_PID" 2>/dev/null || true

echo "[run.sh] Fuzzer finished, cov cleaned up."
