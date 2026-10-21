#!/usr/bin/env python3
"""
plot_scaling.py - speedup, efficiency and time breakdown of the parallel Kalman filter.

    python tools/plot_scaling.py --linux results/scaling_linux.csv --windows results/scaling_windows.csv \
           --cores 8 --physical 4 --out results/plots

Either OS alone also works. Input: the CSV written by scripts/run_scaling.sh / .bat.
Each file is several sweeps; a sweep is one serial row followed by one row per worker
count, so rows are paired with the serial row that came just before them.

Definitions (also in docs/KALMAN_DESIGN.md)
    compute time   time from "all workers told to go" to "all workers done", the median
                   of the rounds inside one run (so process start-up is excluded)
    speedup(W)     serial compute time / compute time with W workers, same sweep
    efficiency(W)  speedup(W) / W
    Karp-Flatt     e(W) = (1/S - 1/W) / (1 - 1/W): the part of the run that behaved as if
                   it were serial. If e stays flat while W grows, the cost is a fixed serial
                   part (Amdahl); if e grows with W, parallelism itself adds cost (overhead,
                   shared memory bandwidth, oversubscription).
    end-to-end     serial total / parallel total for a run that reads and hashes but writes
                   nothing; it includes start-up, so it shows what the processes cost.
The reported number is the median over sweeps; shaded bands show min..max over sweeps.

Outputs (in --out): scaling_speedup.png, scaling_breakdown.png, scaling_summary.md, scaling_summary.csv
"""

import argparse
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
import pandas as pd  # noqa: E402

OS_COLOR = {"linux": "#2a78d6", "windows": "#eb6834"}
OS_LABEL = {"linux": "Linux (WSL2)", "windows": "Windows"}
SURFACE = "#fcfcfb"
INK = "#0b0b0b"
INK2 = "#52514e"
GRID = "#e4e3df"
MUTED = "#8a8984"


def load(path, expect_os):
    df = pd.read_csv(path)
    for col in ("workers", "compute_ms", "total_ms", "startup_ms", "hash_ms", "load_ms"):
        if col not in df.columns:
            sys.exit(f"{path}: no column '{col}' - was it written by the Phase 6 programs?")
    if (df["os"] != expect_os).any():
        print(f"warning: {path} has rows for {sorted(df['os'].unique())}, expected {expect_os}", file=sys.stderr)
    bad = df[(df["program"] == "master") & (df["verified"] != "yes")]
    if len(bad):
        sys.exit(f"{path}: {len(bad)} parallel rows are not hash-verified; refusing to plot them")
    df = df.reset_index(drop=True)
    df["sweep"] = (df["program"] == "serial").cumsum()
    return df


def summarise(df):
    """One row per worker count: medians and min..max over sweeps."""
    serial = df[df["program"] == "serial"].set_index("sweep")
    par = df[df["program"] == "master"].copy()
    par["serial_compute"] = par["sweep"].map(serial["compute_ms"])
    par["serial_total"] = par["sweep"].map(serial["total_ms"])
    par["speedup"] = par["serial_compute"] / par["compute_ms"]
    par["efficiency"] = par["speedup"] / par["workers"]
    par["end_to_end"] = par["serial_total"] / par["total_ms"]
    w = par["workers"].astype(float)
    with np.errstate(divide="ignore", invalid="ignore"):
        par["karp_flatt"] = np.where(w > 1, (1 / par["speedup"] - 1 / w) / (1 - 1 / w), np.nan)

    rows = []
    for k, g in par.groupby("workers"):
        row = {"workers": int(k), "runs": len(g)}
        for c in ("compute_ms", "startup_ms", "hash_ms", "total_ms", "cpu_ms", "peak_kb",
                  "master_peak_kb", "speedup", "efficiency", "end_to_end", "karp_flatt"):
            row[c] = float(g[c].median())
        for c in ("speedup", "efficiency"):
            row[c + "_min"] = float(g[c].min())
            row[c + "_max"] = float(g[c].max())
        rows.append(row)
    out = pd.DataFrame(rows).sort_values("workers")
    base = {
        "compute_ms": float(serial["compute_ms"].median()),
        "total_ms": float(serial["total_ms"].median()),
        "hash_ms": float(serial["hash_ms"].median()),
        "cpu_ms": float(serial["cpu_ms"].median()),
        "peak_kb": float(serial["peak_kb"].median()),
        "runs": len(serial),
        "tracks": int(serial["tracks"].iloc[0]),
        "steps": int(serial["steps"].iloc[0]),
    }
    return out, base


def style(ax):
    ax.set_facecolor(SURFACE)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(GRID)
    ax.tick_params(colors=INK2, labelsize=9, length=3)
    ax.xaxis.label.set_color(INK2)
    ax.yaxis.label.set_color(INK2)
    ax.grid(axis="y", color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)


def core_markers(ax, cores, physical, ymax):
    for n, label in ((physical, "physical cores"), (cores, "logical cores")):
        if n:
            ax.axvline(n, color=MUTED, linewidth=1, linestyle=":")
            ax.text(n - 0.12, ymax * 0.97, f"{n} {label}", color=INK2, fontsize=8, va="top", ha="right", rotation=90)


def plot_speedup(res, path, cores, physical):
    fig, (a1, a2) = plt.subplots(1, 2, figsize=(11, 4.6), facecolor=SURFACE)
    top = max(max(s["workers"].max(), 2) for s, _ in res.values())
    ticks = sorted({int(w) for s, _ in res.values() for w in s["workers"]})
    ymax = 0.0
    for os_name, (s, _) in res.items():
        c = OS_COLOR[os_name]
        a1.fill_between(s["workers"], s["speedup_min"], s["speedup_max"], color=c, alpha=0.18, linewidth=0)
        a1.plot(s["workers"], s["speedup"], color=c, linewidth=2, marker="o", markersize=6,
                markeredgecolor=SURFACE, markeredgewidth=1.5, label=OS_LABEL[os_name])
        a2.fill_between(s["workers"], s["efficiency_min"], s["efficiency_max"], color=c, alpha=0.18, linewidth=0)
        a2.plot(s["workers"], s["efficiency"], color=c, linewidth=2, marker="o", markersize=6,
                markeredgecolor=SURFACE, markeredgewidth=1.5, label=OS_LABEL[os_name])
        ymax = max(ymax, float(s["speedup_max"].max()))
    ideal_top = min(top, (cores or top))
    a1.plot([1, ideal_top + 1], [1, ideal_top + 1], color=MUTED, linestyle="--", linewidth=1.2,
            label="ideal (speedup = workers)")
    a1.set_ylim(0, max(ymax * 1.15, ideal_top + 1.5))
    a2.axhline(1.0, color=MUTED, linestyle="--", linewidth=1.2)
    a2.set_ylim(0, 1.15)
    for ax, ylab, title in ((a1, "speedup over the serial program", "Speedup"),
                            (a2, "speedup / workers", "Parallel efficiency")):
        style(ax)
        ax.set_xlabel("worker processes")
        ax.set_ylabel(ylab)
        ax.set_title(title, color=INK, fontsize=11, loc="left")
        ax.set_xlim(0.5, top + 0.5)
        ax.set_xticks(ticks)
    core_markers(a1, cores, physical, a1.get_ylim()[1])
    core_markers(a2, cores, physical, 1.15)
    a1.legend(frameon=False, fontsize=9, loc="lower right", bbox_to_anchor=(1.0, 0.27), labelcolor=INK2)
    fig.suptitle("Parallel Kalman filter: scaling with the number of worker processes",
                 color=INK, fontsize=12, x=0.01, ha="left")
    fig.text(0.01, 0.005, "Compute time only (start-up excluded). Dots: median over sweeps; band: min..max. "
             "Every point is hash-verified against the serial result.", fontsize=8, color=INK2, va="bottom")
    fig.tight_layout(rect=(0, 0.03, 1, 0.94))
    fig.savefig(path, dpi=160, facecolor=SURFACE)
    plt.close(fig)


def plot_breakdown(res, path):
    n = len(res)
    fig, axes = plt.subplots(1, n, figsize=(5.8 * n + 0.6, 4.6), facecolor=SURFACE, squeeze=False)
    for ax, (os_name, (s, base)) in zip(axes[0], res.items()):
        style(ax)
        x = np.arange(len(s) + 1)
        labels = ["serial"] + [str(w) for w in s["workers"]]
        compute = np.r_[base["compute_ms"], s["compute_ms"]]
        hashed = np.r_[base["hash_ms"], s["hash_ms"]]
        startup = np.r_[0.0, s["startup_ms"]]
        edge = dict(edgecolor=SURFACE, linewidth=1.5)
        ax.bar(x, compute, color=OS_COLOR[os_name], width=0.72, label="filtering", **edge)
        ax.bar(x, hashed, bottom=compute, color=MUTED, width=0.72, label="hashing the result", **edge)
        ax.bar(x, startup, bottom=compute + hashed, color=INK, width=0.72,
               label="start-up (shared memory, pipes, processes)", **edge)
        ax.set_xticks(x)
        ax.set_xticklabels(labels)
        ax.set_xlabel("worker processes")
        ax.set_ylabel("milliseconds")
        ax.set_title(OS_LABEL[os_name], color=INK, fontsize=11, loc="left")
        ax.legend(frameon=False, fontsize=8, labelcolor=INK2, loc="upper right")
    fig.suptitle("Where the time goes (no result file written)", color=INK, fontsize=12, x=0.01, ha="left")
    fig.text(0.01, 0.005, "Median over sweeps. Hashing is done by one process, so it does not shrink with more workers.",
             fontsize=8, color=INK2, va="bottom")
    fig.tight_layout(rect=(0, 0.03, 1, 0.94))
    fig.savefig(path, dpi=160, facecolor=SURFACE)
    plt.close(fig)


def write_summary(res, out_dir, cores, physical):
    csv_rows = []
    lines = ["# Parallel scaling summary", ""]
    for os_name, (s, base) in res.items():
        lines += [f"## {OS_LABEL[os_name]}", "",
                  f"{base['tracks']} tracks x {base['steps']} steps; {int(base['runs'])} sweeps; "
                  f"serial: compute {base['compute_ms']:.1f} ms, hash {base['hash_ms']:.1f} ms, "
                  f"total {base['total_ms']:.1f} ms, CPU {base['cpu_ms']:.0f} ms, peak {base['peak_kb'] / 1024:.0f} MiB.", "",
                  "| workers | compute ms | speedup | min..max | efficiency | Karp-Flatt | end-to-end | start-up ms | CPU ms | worker peak MiB |",
                  "|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
        for _, r in s.iterrows():
            kf = "-" if np.isnan(r["karp_flatt"]) else f"{r['karp_flatt']:.3f}"
            lines.append(f"| {int(r['workers'])} | {r['compute_ms']:.1f} | {r['speedup']:.2f} | "
                         f"{r['speedup_min']:.2f}..{r['speedup_max']:.2f} | {r['efficiency']:.2f} | {kf} | "
                         f"{r['end_to_end']:.2f} | {r['startup_ms']:.1f} | {r['cpu_ms']:.0f} | {r['peak_kb'] / 1024:.0f} |")
            d = r.to_dict()
            d["os"] = os_name
            csv_rows.append(d)
        best = s.loc[s["speedup"].idxmax()]
        lines += ["", f"Best: {best['speedup']:.2f}x with {int(best['workers'])} workers "
                      f"(efficiency {best['efficiency']:.2f}).", ""]
        if physical or cores:
            lines += [f"Machine: {physical or '?'} physical / {cores or '?'} logical cores.", ""]
    with open(os.path.join(out_dir, "scaling_summary.md"), "w") as f:
        f.write("\n".join(lines))
    pd.DataFrame(csv_rows).to_csv(os.path.join(out_dir, "scaling_summary.csv"), index=False)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--linux")
    ap.add_argument("--windows")
    ap.add_argument("--cores", type=int, default=0, help="logical CPUs (marks them on the chart)")
    ap.add_argument("--physical", type=int, default=0, help="physical cores")
    ap.add_argument("--out", default="results/plots")
    a = ap.parse_args()
    if not a.linux and not a.windows:
        ap.error("give --linux and/or --windows")
    os.makedirs(a.out, exist_ok=True)
    res = {}
    for name, path in (("linux", a.linux), ("windows", a.windows)):
        if path:
            res[name] = summarise(load(path, name))
    plot_speedup(res, os.path.join(a.out, "scaling_speedup.png"), a.cores, a.physical)
    plot_breakdown(res, os.path.join(a.out, "scaling_breakdown.png"))
    write_summary(res, a.out, a.cores, a.physical)
    print(f"wrote scaling_speedup.png, scaling_breakdown.png, scaling_summary.md/.csv in {a.out}")


if __name__ == "__main__":
    main()
