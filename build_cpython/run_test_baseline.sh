#!/bin/sh
set -u

if [ "${CPYTHON_COV_SHELL:-}" != "1" ]; then
    echo "plz run under cpython-cov.nix"
    exit 1
fi

SCRIPT_DIR=$(realpath "$(dirname "$0")")
OUT_DIR="$SCRIPT_DIR/baseline_tests"
WORK_DIR="$OUT_DIR/work"
PROFRAW_DIR="$OUT_DIR/profraw"
PROFDATA="$OUT_DIR/baseline.profdata"
COV_JSON="$OUT_DIR/baseline_cov.json"

mkdir -p "$WORK_DIR" "$PROFRAW_DIR"
rm -f "$PROFRAW_DIR"/*.profraw
cd "$WORK_DIR"

export ASAN_OPTIONS=allocator_may_return_null=1:detect_leaks=0
export PYTHONWARNINGS=ignore
export PYTHONUNBUFFERED=x

test_status=0
LLVM_PROFILE_FILE="$PROFRAW_DIR/default_%p.profraw" python3 -m test "$@" || test_status=$?

profraw_files=$(find "$PROFRAW_DIR" -maxdepth 1 -type f -name '*.profraw' -print)
if [ -z "$profraw_files" ]; then
    echo "[baseline] no profraw files found in $PROFRAW_DIR"
    exit "$test_status"
fi

llvm-profdata merge -sparse $profraw_files -o "$PROFDATA"
llvm-cov export --format=text -summary-only \
    "$CPYTHON_LIB/libpython3.14.so.1.0" \
    -instr-profile="$PROFDATA" > "$COV_JSON"

echo "[baseline] wrote $PROFDATA"
echo "[baseline] wrote $COV_JSON"
exit "$test_status"
