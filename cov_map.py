#!/usr/bin/env python3

import json
import polars as pl
import plotly.express as px

COV_JSON_PATH = "cov.json"
OUTPUT_PATH = "treemap"


def load_file_level_cov(json_path):
    with open(json_path, "r") as f:
        raw = json.load(f)

    rows = []
    for file in raw["data"][0]["files"]:
        path = file["filename"]
        if "summary" not in file:
            continue
        summary = file["summary"]
        total = summary["regions"]["count"]
        covered = summary["regions"]["covered"]

        if total == 0:
            continue

        name = path.split("/")[-1]
        dir_ = path.split("/")[-2] if "/" in path else ""

        rows.append(
            {
                "dir": dir_,
                "file": name,
                "Covered Percentage (%)": covered / total * 100,
                "regions": total,
            }
        )

    return pl.DataFrame(rows)


def plot_treemap(df: pl.DataFrame, output="treemap"):
    fig = px.treemap(
        df.to_dict(),
        path=["dir", "file"],
        values="regions",
        color="Covered Percentage (%)",
        color_continuous_scale="RdYlGn",
        color_continuous_midpoint=50,
        width=1000,
        height=700,
    )

    fig.update_layout(
        uniformtext=dict(minsize=12),
        margin=dict(t=30, l=0, r=0, b=0),
        coloraxis_colorbar_title="Coverage %",
    )

    fig.write_html(output + ".html")
    fig.write_image(output + ".svg")
    print(f"[+] Treemap saved to {output}")


if __name__ == "__main__":
    df = load_file_level_cov(COV_JSON_PATH)
    if df.is_empty():
        print("[!] No file coverage data found in cov.json")
    else:
        plot_treemap(df, OUTPUT_PATH)
