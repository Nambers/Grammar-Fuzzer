#!/usr/bin/env bash
# run_cov_time.sh
# Runs QuickJSCov --time-collect, which processes corpus/queue sorted by ms
# and dumps a cumulative time_<ms>.profraw after each timestamp group.
# Then reads each profraw directly (no merging needed — already cumulative)
# and writes one CSV row per snapshot.
#
# Output: cov_time.csv
#   ms, func_pct, line_pct, branch_pct

set -euo pipefail

if [ "${QUICKJS_SHELL:-}" != "1" ]; then
    echo "plz run under quickjs-pkg.nix"
    exit 1
fi

SCRIPT_DIR="$(realpath "$(dirname "$0")")"
BUILD_COV="$SCRIPT_DIR/build_cov/QuickJSCov"
TMP_PROFDATA="$SCRIPT_DIR/_time_snap.profdata"
TMP_JSON="$SCRIPT_DIR/_time_snap.json"
OUTPUT="$SCRIPT_DIR/cov_time.csv"

export ASAN_OPTIONS=allocator_may_return_null=1:detect_leaks=0

# ---------------------------------------------------------------------------
# Run the collector — produces time_<ms>.profraw files
# ---------------------------------------------------------------------------
# %p expands to the child PID so each forked snapshot gets a unique temp file.
LLVM_PROFILE_FILE="default_%p.profraw" "$BUILD_COV" --time-collect

# ---------------------------------------------------------------------------
# Process each snapshot in ms order
# ---------------------------------------------------------------------------
echo "ms,func_pct,line_pct,branch_pct" > "$OUTPUT"

find "$SCRIPT_DIR" -maxdepth 1 -name 'time_*.profraw' -printf '%f\n' \
| sort -t'_' -k2,2n \
| while read -r PROFRAW_NAME; do
    MS="${PROFRAW_NAME#time_}"
    MS="${MS%.profraw}"

    llvm-profdata merge -sparse "$SCRIPT_DIR/$PROFRAW_NAME" -o "$TMP_PROFDATA"

    llvm-cov export \
        --format=text \
        -summary-only \
        "$BUILD_COV" \
        -instr-profile="$TMP_PROFDATA" \
        > "$TMP_JSON"

    FUNC_PCT=$(grep -o '"functions":{"count":[0-9]*,"covered":[0-9]*,"percent":[0-9.]*}' "$TMP_JSON" \
               | tail -1 | grep -o '"percent":[0-9.]*' | cut -d: -f2)
    LINE_PCT=$(grep -o '"lines":{"count":[0-9]*,"covered":[0-9]*,"percent":[0-9.]*}' "$TMP_JSON" \
               | tail -1 | grep -o '"percent":[0-9.]*' | cut -d: -f2)
    BRANCH_PCT=$(grep -o '"branches":{"count":[0-9]*,"covered":[0-9]*,"notcovered":[0-9]*,"percent":[0-9.]*}' "$TMP_JSON" \
                 | tail -1 | grep -o '"percent":[0-9.]*' | cut -d: -f2)

    echo "$MS,$FUNC_PCT,$LINE_PCT,$BRANCH_PCT" >> "$OUTPUT"
    printf "  ms=%-8s  fn=%s%%  line=%s%%  branch=%s%%\n" "$MS" "$FUNC_PCT" "$LINE_PCT" "$BRANCH_PCT"
done

rm -f "$TMP_PROFDATA" "$TMP_JSON"
rm -f time_*.profraw

echo ""
echo "Done. Results written to: $OUTPUT"
