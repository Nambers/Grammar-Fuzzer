#!/usr/bin/env bash
set -euo pipefail

BUILD_PATH=$(readlink -f build)
BUILD_COV_PATH=$(readlink -f build_cov)

mkdir -p corpus/tmp corpus/queue corpus/done corpus/saved

echo "[run.sh] Cleaning old corpus..."
rm -f corpus/tmp/* corpus/queue/* corpus/done/*

cleanup() {
    echo ""
    echo "[run.sh] Caught SIGINT, shutting down fuzzer and cov..."

    echo "[run.sh] Killing fuzzer (PID=$FUZZ_PID)..."
    kill -s SIGINT "$FUZZ_PID" 2>/dev/null || true
    wait "$FUZZ_PID"

    # Try graceful shutdown of cov
    kill -s SIGINT "$COV_PID" 2>/dev/null || true

    # Wait up to 3s for cov to exit cleanly
    for i in {1..30}; do
        if ! kill -0 "$COV_PID" 2>/dev/null; then
            break # exited
        fi
        sleep 0.1
    done

    # Force kill if still alive
    if kill -0 "$COV_PID" 2>/dev/null; then
        echo "[run.sh] Force killing cov (PID=$COV_PID)..."
        kill -9 "$COV_PID" 2>/dev/null || true
        wait "$COV_PID" 2>/dev/null || true
    fi

    echo "[run.sh] Fuzzer finished, cov cleaned up."
    exit 0
}
trap cleanup SIGINT

export ASAN_OPTIONS=allocator_may_return_null=1:detect_leaks=0

LLVM_PROFILE_FILE="default_%p.profraw" $BUILD_COV_PATH/targets/CPython/CPythonCov &
COV_PID=$!

# -load-saved to load previously saved corpus
$BUILD_PATH/pyFuzzer -load-saved &
FUZZ_PID=$!

wait "$FUZZ_PID"

# Try graceful shutdown of cov
kill -s SIGINT "$COV_PID" 2>/dev/null || true

# Wait up to 3s for cov to exit cleanly
for i in {1..30}; do
    if ! kill -0 "$COV_PID" 2>/dev/null; then
        break # exited
    fi
    sleep 0.1
done

# Force kill if still alive
if kill -0 "$COV_PID" 2>/dev/null; then
    echo "[run.sh] Force killing cov (PID=$COV_PID)..."
    kill -9 "$COV_PID" 2>/dev/null || true
    wait "$COV_PID" 2>/dev/null || true
fi

echo "[run.sh] Fuzzer finished, cov cleaned up."
