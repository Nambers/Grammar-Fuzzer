#!/usr/bin/env python3

import argparse
import json
from pathlib import Path

COV_JSON_PATH = "cov.json"
OUTPUT_PATH = "treemap"
DIFF_OUTPUT_PATH = "treemap-diff"
pl = None
px = None


def load_plot_deps():
    global pl, px
    if pl is None:
        import polars as polars

        pl = polars
    if px is None:
        import plotly.express as plotly_express

        px = plotly_express


def load_file_level_cov(json_path):
    load_plot_deps()

    with open(json_path, "r") as f:
        raw = json.load(f)

    rows = []
    for file in raw["data"][0]["files"]:
        path = file["filename"]
        if "summary" not in file:
            continue
        summary = file["summary"]
        total = summary["branches"]["count"]
        covered = summary["branches"]["covered"]

        if total == 0:
            continue

        name = path.split("/")[-1]
        dir_ = path.split("/")[-2] if "/" in path else ""

        rows.append(
            {
                "dir": dir_,
                "file": name,
                "Covered Percentage (%)": covered / total * 100,
                "branches": total,
            }
        )

    return pl.DataFrame(rows)


def svg_comment_safe(text):
    return text.replace("--", r"\u002d\u002d")


def write_svg_with_data(fig, output, df, metadata):
    svg_path = Path(output + ".svg")
    fig.write_image(str(svg_path))

    payload = {
        "metadata": metadata,
        "rows": df.to_dicts(),
    }
    comment = (
        "\n<!-- cov_map_data\n"
        + svg_comment_safe(json.dumps(payload, sort_keys=True))
        + "\n-->\n"
    )

    svg = svg_path.read_text()
    insert_at = svg.find("?>")
    if insert_at != -1:
        insert_at += 2
        svg = svg[:insert_at] + comment + svg[insert_at:]
    else:
        svg = comment + svg
    svg_path.write_text(svg)


def plot_treemap(df, output="treemap"):
    load_plot_deps()

    fig = px.treemap(
        df.to_dict(),
        path=["dir", "file"],
        values="branches",
        color="Covered Percentage (%)",
        color_continuous_scale="RdYlGn",
        color_continuous_midpoint=50,
        width=1000,
        height=700,
    )

    fig.update_layout(
        uniformtext=dict(minsize=12),
        margin=dict(t=30, l=0, r=0, b=0),
        coloraxis_colorbar_title="Branches Coverage %",
    )

    # fig.write_html(output + ".html")
    write_svg_with_data(
        fig,
        output,
        df,
        {
            "mode": "coverage",
            "metric": "branch coverage percentage",
        },
    )
    print(f"[+] Treemap saved to {output}")


def plot_treemap_diff(coverage_path, baseline_path, output="treemap-diff"):
    load_plot_deps()

    coverage = {
        (row["dir"], row["file"]): row
        for row in load_file_level_cov(coverage_path).to_dicts()
    }
    baseline = {
        (row["dir"], row["file"]): row
        for row in load_file_level_cov(baseline_path).to_dicts()
    }

    rows = []
    for dir_, file in sorted(set(coverage) | set(baseline)):
        coverage_row = coverage.get((dir_, file), {})
        baseline_row = baseline.get((dir_, file), {})
        branches = max(
            coverage_row.get("branches", 0),
            baseline_row.get("branches", 0),
        )
        if branches == 0:
            continue

        rows.append(
            {
                "dir": dir_,
                "file": file,
                "Coverage (%)": coverage_row.get("Covered Percentage (%)", 0),
                "Baseline Coverage (%)": baseline_row.get(
                    "Covered Percentage (%)", 0
                ),
                "Coverage Differential (%)": coverage_row.get(
                    "Covered Percentage (%)", 0
                )
                - baseline_row.get("Covered Percentage (%)", 0),
                "branches": branches,
            }
        )

    df = pl.DataFrame(rows)

    if df.is_empty():
        print("[!] No comparable file coverage data found")
        return

    better = df.filter(pl.col("Coverage Differential (%)") > 0).sort(
        "Coverage Differential (%)", descending=True
    )
    if better.is_empty():
        print("[diff] No files are better than baseline.")
    else:
        print("[diff] Files better than baseline:")
        for row in better.to_dicts():
            path = f"{row['dir']}/{row['file']}" if row["dir"] else row["file"]
            print(
                "[diff] "
                f"{path}: +{row['Coverage Differential (%)']:.2f}% "
                f"({row['Coverage (%)']:.2f}% vs "
                f"{row['Baseline Coverage (%)']:.2f}%, "
                f"{row['branches']} branches)"
            )

    fig = px.treemap(
        df.to_dict(),
        path=["dir", "file"],
        values="branches",
        color="Coverage Differential (%)",
        color_continuous_scale="RdYlGn",
        color_continuous_midpoint=0,
        width=1000,
        height=700,
    )

    fig.update_layout(
        uniformtext=dict(minsize=12),
        margin=dict(t=30, l=0, r=0, b=0),
        coloraxis_colorbar_title="Branch Coverage Diff %",
    )

    # fig.write_html(output + ".html")
    write_svg_with_data(
        fig,
        output,
        df,
        {
            "mode": "diff",
            "metric": "branch coverage percentage",
            "operation": "coverage - baseline",
        },
    )
    print(f"[+] Diff treemap saved to {output}.html and {output}.svg")


def parse_args():
    parser = argparse.ArgumentParser(
        description="Draw LLVM coverage treemaps from llvm-cov export JSON."
    )
    parser.add_argument(
        "coverage",
        nargs="?",
        default=COV_JSON_PATH,
        help=f"coverage JSON to plot, default: {COV_JSON_PATH}",
    )
    parser.add_argument(
        "--diff",
        metavar="BASELINE_JSON",
        help="plot coverage differential as COVERAGE - BASELINE_JSON",
    )
    parser.add_argument(
        "-o",
        "--output",
        help="output path without extension",
    )
    return parser.parse_args()


if __name__ == "__main__":
    args = parse_args()
    if args.diff:
        plot_treemap_diff(
            args.coverage,
            args.diff,
            args.output if args.output else DIFF_OUTPUT_PATH,
        )
    else:
        df = load_file_level_cov(args.coverage)
        if df.is_empty():
            print(f"[!] No file coverage data found in {args.coverage}")
        else:
            plot_treemap(df, args.output if args.output else OUTPUT_PATH)
