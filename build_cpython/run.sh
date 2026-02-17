#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(realpath "$(dirname $0)")
BUILD_PATH="$SCRIPT_DIR/build"

mkdir -p "$SCRIPT_DIR/corpus/tmp" "$SCRIPT_DIR/corpus/queue" "$SCRIPT_DIR/corpus/done" "$SCRIPT_DIR/corpus/saved"

echo "[run.sh] Cleaning old corpus..."
rm -f "$SCRIPT_DIR/corpus/tmp/"* "$SCRIPT_DIR/corpus/queue/"* "$SCRIPT_DIR/corpus/done/"*

export ASAN_OPTIONS=allocator_may_return_null=1:detect_leaks=0
export PYTHONWARNINGS=ignore
export PYTHONUNBUFFERED=x

clear

"$BUILD_PATH/pyFuzzer" -load-saved 2> errlog.txt; EXIT_CODE=$?

echo "[run.sh] Fuzzer exited with code $EXIT_CODE"
exit $EXIT_CODE
