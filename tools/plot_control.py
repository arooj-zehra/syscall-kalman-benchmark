#!/usr/bin/env python3
"""
plot_control.py - draw results/control_<os>.csv: independent processes (hardware ceiling)
against kf_master.

    python tools/plot_control.py --linux results/control_linux.csv --windows results/control_windows.csv \
           --cores 8 --physical 4 --out results/plots

Left: speedup (throughput relative to one process) of N independent kf_serial processes
(dashed, "what this CPU can do") and of kf_master with N workers (solid, "what the design
achieves"). Right: design efficiency = master / control; 1.0 means the design leaves
nothing on the table that the hardware would have given.
"""

import argparse
import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import pandas as pd  # noqa: E402

COLOR = {"linux": "#2a78d6", "windows": "#eb6834"}
LABEL = {"linux": "Linux (WSL2)", "windows": "Windows"}
SURFACE, INK, INK2, GRID, MUTED = "#fcfcfb", "#0b0b0b", "#52514e", "#e4e3df", "#8a8984"


def style(ax):
    ax.set_facecolor(SURFACE)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    for s in ("left", "bottom"):
        ax.spines[s].set_color(GRID)
    ax.tick_params(colors=INK2, labelsize=9, length=3)
    ax.xaxis.label.set_color(INK2)
    ax.yaxis.label.set_color(INK2)
    ax.grid(axis="y", color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)


def summarise(path):
    d = pd.read_csv(path)
    g = d.groupby("n").median(numeric_only=True)
    base = g.loc[g.index.min(), "control_mstep_s"]
    out = pd.DataFrame({"control": g["control_mstep_s"] / base, "master": g["master_mstep_s"] / base})
    out["design"] = out["master"] / out["control"]
    out["slowdown"] = g["control_ms_mean"] / g.loc[g.index.min(), "control_ms_mean"]
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--linux")
    ap.add_argument("--windows")
    ap.add_argument("--cores", type=int, default=0)
    ap.add_argument("--physical", type=int, default=0)
    ap.add_argument("--out", default="results/plots")
    a = ap.parse_args()
    res = {k: summarise(p) for k, p in (("linux", a.linux), ("windows", a.windows)) if p}
    if not res:
        ap.error("give --linux and/or --windows")
    os.makedirs(a.out, exist_ok=True)
    fig, (a1, a2) = plt.subplots(1, 2, figsize=(11, 4.6), facecolor=SURFACE)
    top = max(int(s.index.max()) for s in res.values())
    for k, s in res.items():
        a1.plot(s.index, s["control"], color=COLOR[k], linewidth=2, linestyle="--", marker="s", markersize=6,
                markeredgecolor=SURFACE, markeredgewidth=1.5, label=f"{LABEL[k]}: independent processes")
        a1.plot(s.index, s["master"], color=COLOR[k], linewidth=2, marker="o", markersize=6,
                markeredgecolor=SURFACE, markeredgewidth=1.5, label=f"{LABEL[k]}: kf_master")
        a2.plot(s.index, s["design"], color=COLOR[k], linewidth=2, marker="o", markersize=6,
                markeredgecolor=SURFACE, markeredgewidth=1.5, label=LABEL[k])
    ticks = sorted({int(n) for s in res.values() for n in s.index})
    cap = a.cores or top
    a1.plot([1, cap], [1, cap], color=MUTED, linestyle=":", linewidth=1.2, label="ideal")
    a2.axhline(1.0, color=MUTED, linestyle=":", linewidth=1.2)
    a2.set_ylim(0, 1.25)
    for ax, title, ylab in ((a1, "Speedup: hardware ceiling vs. the parallel filter", "throughput relative to one process"),
                            (a2, "Design efficiency = kf_master / independent processes", "ratio")):
        style(ax)
        ax.set_xticks(ticks)
        ax.set_xlabel("processes (workers)")
        ax.set_ylabel(ylab)
        ax.set_title(title, color=INK, fontsize=10.5, loc="left")
        for n, lab in ((a.physical, "physical"), (a.cores, "logical")):
            if n:
                ax.axvline(n, color=MUTED, linewidth=1, linestyle=":")
    a1.legend(frameon=False, fontsize=8, labelcolor=INK2, loc="lower right")
    a2.legend(frameon=False, fontsize=8, labelcolor=INK2, loc="lower left")
    fig.text(0.01, 0.005, "Median over sweeps. Dashed: N independent kf_serial processes (no IPC, no shared memory). "
             "Solid: kf_master with N workers. Same data, same machine.", fontsize=8, color=INK2, va="bottom")
    fig.tight_layout(rect=(0, 0.03, 1, 1))
    fig.savefig(os.path.join(a.out, "control_scaling.png"), dpi=160, facecolor=SURFACE)
    for k, s in res.items():
        print(k)
        print(s.round(3).to_string())


if __name__ == "__main__":
    main()
