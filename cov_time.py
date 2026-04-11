#!/usr/bin/env python3
"""
Plot LLVM coverage (function / line / branch) over fuzzing time.
Run from build_cpython/:  python ../cov_time.py
Reads:  ./cov_time.csv
Writes: ./cov_time.svg
"""

import csv
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker as mticker

# ── academia rcParams ────────────────────────────────────────────────────────
plt.rcParams.update(
    {
        # figure
        "figure.figsize": (15.0, 5.0),
        "figure.dpi": 200,
        # axes
        "axes.linewidth": 0.8,
        "axes.spines.top": False,
        "axes.spines.right": False,
        "axes.grid": True,
        "axes.axisbelow": True,
        # grid
        "grid.color": "#cccccc",
        "grid.linewidth": 0.5,
        "grid.linestyle": "--",
        # lines
        "lines.linewidth": 1.6,
        "lines.markersize": 3,
        # font
        "font.family": "serif",
        "font.size": 11,
        "axes.labelsize": 12,
        "axes.titlesize": 12,
        "legend.fontsize": 10,
        "legend.framealpha": 0.9,
        "legend.edgecolor": "#aaaaaa",
        # ticks
        "xtick.direction": "in",
        "ytick.direction": "in",
        "xtick.major.size": 3.5,
        "ytick.major.size": 3.5,
        "xtick.labelsize": 10,
        "ytick.labelsize": 10,
    }
)

# get target name from current folder
TARGET = os.path.basename(os.getcwd()).split("_")[1]

# ── load data ────────────────────────────────────────────────────────────────
csv_file = "cov_time.csv"
if not os.path.exists(csv_file):
    sys.exit(f"[error] {csv_file} not found — run from build_cpython/")

ms_vals, func_pct, line_pct, branch_pct = [], [], [], []
with open(csv_file, newline="") as f:
    for row in csv.DictReader(f):
        ms_vals.append(int(row["ms"]))
        func_pct.append(float(row["func_pct"]))
        line_pct.append(float(row["line_pct"]))
        branch_pct.append(float(row["branch_pct"]))

if not ms_vals:
    sys.exit("[error] cov_time.csv is empty")

n = len(ms_vals)

# ── time axis: auto-scale ms → human unit ────────────────────────────────────
max_ms = max(ms_vals)
if max_ms < 90_000:  # < 1.5 min  → seconds
    t = [x / 1_000 for x in ms_vals]
    xlabel = "Time (s)"
elif max_ms < 5_400_000:  # < 1.5 hr   → minutes
    t = [x / 60_000 for x in ms_vals]
    xlabel = "Time (min)"
else:  # ≥ 1.5 hr   → hours
    t = [x / 3_600_000 for x in ms_vals]
    xlabel = "Time (h)"

# ── marker density: ≤ 15 markers per line regardless of point count ──────────
every = max(1, n // 15)

# ── colorblind-safe palette (Paul Tol's bright) ───────────────────────────────
C_FUNC = "#4477AA"  # blue
C_LINE = "#EE6677"  # red
C_BRANCH = "#228833"  # green

# ── plot ─────────────────────────────────────────────────────────────────────
fig, ax = plt.subplots()

ax.plot(t, func_pct, color=C_FUNC, marker="o", label="Function")
ax.plot(t, line_pct, color=C_LINE, marker="s", label="Line")
ax.plot(t, branch_pct, color=C_BRANCH, marker="^", label="Branch")

# y-axis: start just below the minimum to show growth clearly
all_vals = func_pct + line_pct + branch_pct
y_lo = max(0.0, min(all_vals) * 0.92)
y_hi = max(all_vals) * 1.06
ax.set_ylim(y_lo, y_hi)
ax.yaxis.set_major_formatter(mticker.FuncFormatter(lambda v, _: f"{v:.1f}%"))

ax.set_xlabel(xlabel)
ax.set_ylabel("Coverage")
ax.set_title(f"{TARGET.capitalize()} Coverage Growth over Fuzzing Time", pad=8)

ax.legend(title="Metric", loc="lower right")
fig.tight_layout(pad=0.5)

# ── save ─────────────────────────────────────────────────────────────────────
out = f"cov_time_{TARGET}.svg"
fig.savefig(out, bbox_inches="tight", dpi=200)
print(f"saved: {out}")
