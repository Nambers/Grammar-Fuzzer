#!/usr/bin/env bash
set -euo pipefail

BUILD_PATH=$(readlink -f build)
BUILD_COV_PATH=$(readlink -f build_cov)

mkdir -p corpus/tmp corpus/queue corpus/done corpus/saved

echo "[run.sh] Cleaning old corpus..."
rm -f corpus/tmp/* corpus/queue/* corpus/done/*

# global so cleanup can see them
FUZZ_PID=
COV_PID=

cleanup() {
    echo ""
    echo "[run.sh] Cleaning up..."

    if [[ -n "${FUZZ_PID:-}" && -e /proc/$FUZZ_PID ]]; then
        echo "[run.sh] Killing fuzzer (PID=$FUZZ_PID)..."
        kill -s SIGINT "$FUZZ_PID" 2>/dev/null || true
        wait "$FUZZ_PID" 2>/dev/null || true
    fi

    if [[ -n "${COV_PID:-}" && -e /proc/$COV_PID ]]; then
        echo "[run.sh] Killing cov (PID=$COV_PID)..."
        kill -s SIGINT "$COV_PID" 2>/dev/null || true

        for i in {1..30}; do
            if ! kill -0 "$COV_PID" 2>/dev/null; then
                break
            fi
            sleep 0.1
        done

        if kill -0 "$COV_PID" 2>/dev/null; then
            echo "[run.sh] Force killing cov (PID=$COV_PID)..."
            kill -9 "$COV_PID" 2>/dev/null || true
            wait "$COV_PID" 2>/dev/null || true
        fi
    fi

    echo "[run.sh] Fuzzer finished, cov cleaned up."
}

trap 'cleanup; exit 0' SIGINT

export ASAN_OPTIONS=allocator_may_return_null=1:detect_leaks=0
export PYTHONWARNINGS=ignore
export PYTHONUNBUFFERED=x

LLVM_PROFILE_FILE="default_%p.profraw" $BUILD_COV_PATH/targets/CPython/CPythonCov &
COV_PID=$!

clear

$BUILD_PATH/pyFuzzer -load-saved &
FUZZ_PID=$!

wait "$FUZZ_PID"
EXIT_CODE=$?

echo "[run.sh] Fuzzer exited with code $EXIT_CODE"
cleanup
exit $EXIT_CODE
