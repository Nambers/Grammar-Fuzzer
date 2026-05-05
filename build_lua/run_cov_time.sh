#!/usr/bin/env bash
# run_cov_time.sh
# Runs LuaCov --time-collect, which processes corpus/queue sorted by ms
# and dumps a cumulative time_<ms>.profraw after each timestamp group.
# Then reads each profraw directly (no merging needed — already cumulative)
# and writes one CSV row per snapshot.
#
# Output: cov_time.csv
#   ms, func_covered, func_total, func_pct,
#       line_covered, line_total, line_pct,
#       branch_covered, branch_total, branch_pct

set -euo pipefail

if [ "${LUA_SHELL:-}" != "1" ]; then
    echo "plz run under lua-pkg.nix"
    exit 1
fi

SCRIPT_DIR="$(realpath "$(dirname "$0")")"
BUILD_COV="$SCRIPT_DIR/build_cov/LuaCov"
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
echo "ms,func_covered,func_total,func_pct,line_covered,line_total,line_pct,branch_covered,branch_total,branch_pct" > "$OUTPUT"

extract_metric() {
    local name="$1"
    local field="$2"
    local metric

    metric=$(grep -Eo "\"${name}\":\{\"count\":[0-9]+,\"covered\":[0-9]+(,\"notcovered\":[0-9]+)?,\"percent\":[0-9.]+\}" "$TMP_JSON" \
             | tail -1)
    printf '%s\n' "$metric" | sed -E "s/.*\"${field}\":([0-9.]+).*/\1/"
}

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

    FUNC_TOTAL=$(extract_metric functions count)
    FUNC_COVERED=$(extract_metric functions covered)
    FUNC_PCT=$(extract_metric functions percent)
    LINE_TOTAL=$(extract_metric lines count)
    LINE_COVERED=$(extract_metric lines covered)
    LINE_PCT=$(extract_metric lines percent)
    BRANCH_TOTAL=$(extract_metric branches count)
    BRANCH_COVERED=$(extract_metric branches covered)
    BRANCH_PCT=$(extract_metric branches percent)

    echo "$MS,$FUNC_COVERED,$FUNC_TOTAL,$FUNC_PCT,$LINE_COVERED,$LINE_TOTAL,$LINE_PCT,$BRANCH_COVERED,$BRANCH_TOTAL,$BRANCH_PCT" >> "$OUTPUT"
    printf "  ms=%-8s  fn=%s/%s (%s%%)  line=%s/%s (%s%%)  branch=%s/%s (%s%%)\n" \
        "$MS" \
        "$FUNC_COVERED" "$FUNC_TOTAL" "$FUNC_PCT" \
        "$LINE_COVERED" "$LINE_TOTAL" "$LINE_PCT" \
        "$BRANCH_COVERED" "$BRANCH_TOTAL" "$BRANCH_PCT"
done

rm -f "$TMP_PROFDATA" "$TMP_JSON"
rm -f time_*.profraw

echo ""
echo "Done. Results written to: $OUTPUT"
