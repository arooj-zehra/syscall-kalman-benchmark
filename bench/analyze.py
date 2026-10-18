#!/usr/bin/env python3
"""
analyze.py - turn benchmark CSV files into a summary table and comparison charts.

    python bench/analyze.py results/linux.csv results/windows.csv --out results/plots

Works with one CSV (one OS) or several. Rows are grouped by the `os` column, so
the same command handles two files or one file holding both systems.

Outputs (in --out, default results/plots):
    summary.csv                 one row per (os, call, size): the numbers behind every chart
    summary.md                  the same as readable tables per group, for the report
    <group>_compare.png         grouped horizontal bars, log time axis, one chart per group
    fileio_throughput.png       MB/s for read and write at each size
    memory_page_cost.png        cost of the first touch of one 4 KB page
    ratio_windows_over_linux.png  who is slower, per call (needs both systems)

How the numbers are summarised
    Each CSV row is one call measured in one run: its median over many samples.
    Across the repeated runs we report the MEDIAN of the per-run medians, and show
    the spread as the min..max of those medians (the line behind each dot in the charts).
    Medians are used because a single background hiccup can distort a mean.
"""

import argparse
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import matplotlib.ticker as mticker  # noqa: E402
import numpy as np  # noqa: E402
import pandas as pd  # noqa: E402

# Fixed colour per operating system (validated categorical slots 1 and 2).
OS_COLOR = {"linux": "#2a78d6", "windows": "#eb6834"}
OS_LABEL = {"linux": "Linux", "windows": "Windows"}
SURFACE = "#fcfcfb"
INK = "#0b0b0b"
INK2 = "#52514e"
GRID = "#e4e3df"

GROUP_TITLES = {
    "baseline": "Baseline: cost of the measurement itself",
    "fileio": "File I/O",
    "process": "Process creation",
    "memory": "Memory mapping",
    "ipc": "Pipes and shared memory",
    "perms": "File permissions",
    "linux": "Linux-only: fork()",
}

KEY = ["group", "call", "size"]

# On Windows these do not enter the kernel (GetCurrentProcessId reads a thread field,
# plat_umask only stores a number), so a ratio against the Linux system call says nothing.
NO_KERNEL_ENTRY_ON_WINDOWS = {"plat_getpid", "plat_umask"}
PAGE = 4096


# ---------------------------------------------------------------------------
# loading and summarising
# ---------------------------------------------------------------------------
def load(paths):
    frames = []
    for p in paths:
        df = pd.read_csv(p)
        df["source"] = os.path.basename(p)
        frames.append(df)
    df = pd.concat(frames, ignore_index=True)
    needed = {"os", "run", "group", "call", "size", "median_ns", "p95_ns", "ops",
              "cpu_user_us", "cpu_sys_us", "peak_kb", "peak_delta_kb"}
    missing = needed - set(df.columns)
    if missing:
        sys.exit(f"analyze: missing columns {sorted(missing)}")
    return df


def summarise(df):
    """One row per (os, group, call, size), aggregated over runs."""
    order = {k: i for i, k in enumerate(dict.fromkeys(map(tuple, df[KEY].values)))}
    rows = []
    for (osn, grp, call, size), g in df.groupby(["os", "group", "call", "size"], sort=False):
        cpu_total_us = (g.cpu_user_us + g.cpu_sys_us)
        ops = g.ops.iloc[0]
        rows.append({
            "os": osn, "group": grp, "call": call, "size": int(size),
            "runs": len(g),
            "median_ns": float(g.median_ns.median()),
            "run_min_ns": float(g.median_ns.min()),
            "run_max_ns": float(g.median_ns.max()),
            "spread_pct": float((g.median_ns.max() - g.median_ns.min()) / g.median_ns.median() * 100),
            "p95_ns": float(g.p95_ns.median()),
            "cpu_ns_per_call": float(cpu_total_us.median() * 1000.0 / ops),
            # Windows counts CPU time in scheduler ticks (about 15.6 ms), so a total
            # below ~150 ms is mostly rounding noise. Linux totals are exact.
            "cpu_reliable": bool(osn != "windows" or cpu_total_us.median() >= 150_000),
            "peak_kb": float(g.peak_kb.median()),
            "peak_delta_kb": float(g.peak_delta_kb.median()),
            "_order": order[(grp, call, int(size))],
        })
    s = pd.DataFrame(rows).sort_values(["_order", "os"]).drop(columns="_order")
    return s.reset_index(drop=True)


# ---------------------------------------------------------------------------
# formatting helpers
# ---------------------------------------------------------------------------
def human_size(n):
    n = int(n)
    if n == 0:
        return ""
    for unit, div in (("MB", 1 << 20), ("KB", 1 << 10)):
        if n >= div and n % div == 0:
            return f"{n // div} {unit}"
    return f"{n} B"


def label_of(call, size):
    sz = human_size(size)
    return f"{call}  {sz}" if sz else call


def fmt_ns(ns):
    if ns != ns:  # NaN
        return "-"
    if ns < 1e3:
        return f"{ns:.1f} ns"
    if ns < 1e6:
        return f"{ns / 1e3:.2f} \u00b5s"
    if ns < 1e9:
        return f"{ns / 1e6:.2f} ms"
    return f"{ns / 1e9:.2f} s"


def time_axis(ax, axis="x"):
    """Log axis with unit-labelled ticks (ns, us, ms, s)."""
    def fmt(v, _pos):
        if v <= 0:
            return ""
        if v < 1e3:
            return f"{v:g} ns"
        if v < 1e6:
            return f"{v / 1e3:g} µs"
        if v < 1e9:
            return f"{v / 1e6:g} ms"
        return f"{v / 1e9:g} s"
    a = ax.xaxis if axis == "x" else ax.yaxis
    (ax.set_xscale if axis == "x" else ax.set_yscale)("log")
    a.set_major_formatter(mticker.FuncFormatter(fmt))
    a.set_minor_formatter(mticker.NullFormatter())


def style_axes(ax):
    ax.set_facecolor(SURFACE)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(GRID)
    ax.tick_params(colors=INK2, labelsize=9, length=3)
    ax.xaxis.label.set_color(INK2)
    ax.yaxis.label.set_color(INK2)
    ax.grid(axis="x", color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)


def new_figure(width, height):
    fig = plt.figure(figsize=(width, height), facecolor=SURFACE)
    return fig


def axes_in_inches(fig, width, height, left, right, top, bottom):
    """Axes rectangle from margins given in inches (keeps title, labels and footer apart)."""
    return fig.add_axes([left / width, bottom / height,
                         (width - left - right) / width, (height - top - bottom) / height])


def footer(fig, text):
    fig.text(0.01, 0.005, text, fontsize=8, color=INK2, ha="left", va="bottom")


def legend_for(ax, oses):
    from matplotlib.patches import Patch
    handles = [Patch(facecolor=OS_COLOR[o], label=OS_LABEL[o]) for o in oses]
    leg = ax.legend(handles=handles, loc="lower right", frameon=False, fontsize=9)
    for t in leg.get_texts():
        t.set_color(INK)


# ---------------------------------------------------------------------------
# charts
# ---------------------------------------------------------------------------
def chart_group(s, group, oses, outdir, note):
    """Dot-and-range plot: one row per call/size, one dot per system, log time axis.

    A dot marks the median of the per-run medians; the line behind it spans the
    fastest to the slowest run. (Bars would need a baseline, and on a log axis
    the baseline is arbitrary, so position alone carries the value.)
    """
    g = s[s.group == group]
    if g.empty:
        return None
    keys = list(dict.fromkeys(zip(g.call, g["size"])))
    n = len(keys)
    present = [o for o in oses if o in set(g.os)]
    step = 0.28 if len(present) > 1 else 0.0

    width = 10.0
    height = 1.3 + 1.1 + 0.30 * n
    fig = new_figure(width, height)
    ax = axes_in_inches(fig, width, height, left=3.0, right=0.4, top=0.8, bottom=1.1)
    style_axes(ax)
    ax.grid(axis="y", color=GRID, linewidth=0.6)

    xs_all = []
    for j, osn in enumerate(present):
        sub = g[g.os == osn].set_index(["call", "size"])
        for i, (call, size) in enumerate(keys):
            if (call, size) not in sub.index:
                continue
            r = sub.loc[(call, size)]
            y = i + (j - (len(present) - 1) / 2) * step
            if r.runs > 1:
                ax.plot([r.run_min_ns, r.run_max_ns], [y, y], color=OS_COLOR[osn],
                        linewidth=2, alpha=0.45, solid_capstyle="round", zorder=3)
            ax.plot(r.median_ns, y, marker="o", markersize=8, color=OS_COLOR[osn],
                    markeredgecolor=SURFACE, markeredgewidth=1.5, linestyle="none", zorder=4)
            xs_all += [r.run_min_ns, r.run_max_ns, r.median_ns]

    ax.set_yticks(range(n))
    ax.set_yticklabels([label_of(c, z) for c, z in keys], fontsize=9, color=INK)
    ax.set_ylim(n - 0.5, -0.5)
    time_axis(ax, "x")
    if xs_all:
        ax.set_xlim(min(xs_all) / 1.8, max(xs_all) * 1.8)
    ax.set_xlabel("time per call (log scale, lower is faster)")
    fig.text(0.4 / width, 1 - 0.38 / height, GROUP_TITLES.get(group, group),
             fontsize=13, color=INK, ha="left", va="center")
    if len(present) >= 2:
        from matplotlib.lines import Line2D
        handles = [Line2D([0], [0], marker="o", linestyle="none", markersize=8,
                          color=OS_COLOR[o], label=OS_LABEL[o]) for o in present]
        leg = fig.legend(handles=handles, loc="center right", bbox_to_anchor=(1 - 0.4 / width, 1 - 0.38 / height),
                         frameon=False, fontsize=9.5, ncol=len(present))
        for t in leg.get_texts():
            t.set_color(INK)
    footer(fig, note)
    path = os.path.join(outdir, f"{group}_compare.png")
    fig.savefig(path, dpi=160, facecolor=SURFACE)
    plt.close(fig)
    return path


def chart_ratio(s, oses, outdir, note):
    if not {"linux", "windows"} <= set(oses):
        return None
    p = s.pivot_table(index=["group", "call", "size"], columns="os", values="median_ns", sort=False)
    p = p.dropna(subset=["linux", "windows"])
    p = p[p.index.get_level_values("group") != "baseline"]
    p = p[~p.index.get_level_values("call").isin(NO_KERNEL_ENTRY_ON_WINDOWS)]
    if p.empty:
        return None
    p["ratio"] = p.windows / p.linux
    order = {k: i for i, k in enumerate(dict.fromkeys(map(tuple, s[KEY].values)))}
    p = p.assign(_o=[order[k] for k in p.index]).sort_values("_o")
    n = len(p)

    width = 10.0
    height = 1.3 + 1.1 + 0.24 * n
    fig = new_figure(width, height)
    ax = axes_in_inches(fig, width, height, left=3.0, right=0.5, top=0.8, bottom=1.1)
    style_axes(ax)
    y = np.arange(n)
    colors = [OS_COLOR["windows"] if r > 1 else OS_COLOR["linux"] for r in p.ratio]
    ax.barh(y, np.log10(p.ratio), left=0, height=0.7, color=colors, zorder=3)
    ax.axvline(0, color=INK, linewidth=1, zorder=4)
    ax.set_yticks(y)
    ax.set_yticklabels([label_of(c, z) for (_g, c, z) in p.index], fontsize=7.5, color=INK)
    ax.set_ylim(n - 0.5, -0.5)
    ticks = [-2, -1, 0, 1, 2]
    ax.set_xticks(ticks)
    ax.set_xticklabels([f"{10.0 ** t:g}x" for t in ticks])
    lim = max(1.0, float(np.abs(np.log10(p.ratio)).max()) * 1.1)
    ax.set_xlim(-lim, lim)
    for yi, r in zip(y, p.ratio):
        ax.text(np.log10(r) + (0.04 if r >= 1 else -0.04), yi, f"{r:.2g}x",
                ha="left" if r >= 1 else "right", va="center", fontsize=7, color=INK2)
    ax.set_xlabel("Windows time divided by Linux time (log scale)")
    fig.text(0.4 / width, 1 - 0.38 / height, "Which system is slower, per call",
             fontsize=13, color=INK, ha="left", va="center")
    from matplotlib.patches import Patch
    leg = fig.legend(handles=[Patch(facecolor=OS_COLOR["windows"], label="Windows slower (bar to the right)"),
                              Patch(facecolor=OS_COLOR["linux"], label="Linux slower (bar to the left)")],
                     loc="center right", bbox_to_anchor=(1 - 0.4 / width, 1 - 0.38 / height),
                     frameon=False, fontsize=9, ncol=2)
    for t in leg.get_texts():
        t.set_color(INK)
    footer(fig, note)
    path = os.path.join(outdir, "ratio_windows_over_linux.png")
    fig.savefig(path, dpi=160, facecolor=SURFACE)
    plt.close(fig)
    return path


def chart_throughput(s, oses, outdir, note):
    f = s[(s.group == "fileio") & s.call.isin(["plat_read", "plat_write"]) & (s["size"] > 0)].copy()
    if f.empty:
        return None
    f["mb_s"] = f["size"] / (f.median_ns / 1e9) / (1 << 20)
    sizes = sorted(f["size"].unique())
    present = [o for o in oses if o in set(f.os)]
    fig, axes = plt.subplots(1, 2, figsize=(10, 4.2), facecolor=SURFACE, sharey=True)
    fig.subplots_adjust(left=0.09, right=0.98, top=0.82, bottom=0.24, wspace=0.08)
    w = 0.8 / max(len(present), 1)
    for ax, call, title in zip(axes, ["plat_read", "plat_write"], ["Read (from file cache)", "Write (into file cache)"]):
        style_axes(ax)
        ax.grid(axis="y", color=GRID, linewidth=0.8)
        ax.grid(axis="x", visible=False)
        for j, osn in enumerate(present):
            vals = []
            for sz in sizes:
                r = f[(f.os == osn) & (f.call == call) & (f["size"] == sz)]
                vals.append(float(r.mb_s.iloc[0]) if len(r) else np.nan)
            x = np.arange(len(sizes)) + (j - (len(present) - 1) / 2) * w
            ax.bar(x, vals, width=w * 0.88, color=OS_COLOR[osn], zorder=3)
            for xi, v in zip(x, vals):
                if v == v:
                    ax.text(xi, v * 1.03, f"{v:,.0f}", ha="center", va="bottom", fontsize=8, color=INK2)
        ax.set_xticks(range(len(sizes)))
        ax.set_xticklabels([human_size(z) for z in sizes], color=INK)
        ax.set_xlabel("size of each call")
        ax.set_title(title, loc="left", fontsize=11, color=INK)
    axes[0].set_ylabel("throughput, MB/s (higher is faster)")
    axes[0].yaxis.set_major_formatter(mticker.FuncFormatter(lambda v, _p: f"{v:,.0f}"))
    if len(present) >= 2:
        legend_for(axes[1], present)
        axes[1].legend_.set_loc("upper left")
    footer(fig, note)
    path = os.path.join(outdir, "fileio_throughput.png")
    fig.savefig(path, dpi=160, facecolor=SURFACE)
    plt.close(fig)
    return path


def chart_page_cost(s, oses, outdir, note):
    """(map+touch) - (map), per 4 KB page, for the sizes where it spans many pages."""
    rows = []
    for osn in oses:
        for kind, base, touch in (("anonymous", "plat_map_anon", "plat_map_anon_touch"),
                                  ("file (cached)", "plat_map_file", "plat_map_file_touch")):
            for size in sorted(s[s.call == touch]["size"].unique()):
                if size < 1 << 20:
                    continue
                a = s[(s.os == osn) & (s.call == base) & (s["size"] == size)]
                b = s[(s.os == osn) & (s.call == touch) & (s["size"] == size)]
                if len(a) and len(b):
                    pages = size / PAGE
                    rows.append((osn, kind, size, (b.median_ns.iloc[0] - a.median_ns.iloc[0]) / pages))
    if not rows:
        return None
    d = pd.DataFrame(rows, columns=["os", "kind", "size", "ns_page"])
    kinds = list(dict.fromkeys(d.kind))
    sizes = sorted(d["size"].unique())
    present = [o for o in oses if o in set(d.os)]
    fig, axes = plt.subplots(1, len(kinds), figsize=(10, 4.2), facecolor=SURFACE, sharey=True)
    axes = np.atleast_1d(axes)
    fig.subplots_adjust(left=0.09, right=0.98, top=0.82, bottom=0.24, wspace=0.08)
    w = 0.8 / max(len(present), 1)
    for ax, kind in zip(axes, kinds):
        style_axes(ax)
        ax.grid(axis="y", color=GRID, linewidth=0.8)
        ax.grid(axis="x", visible=False)
        for j, osn in enumerate(present):
            vals = []
            for sz in sizes:
                r = d[(d.os == osn) & (d.kind == kind) & (d["size"] == sz)]
                vals.append(float(r.ns_page.iloc[0]) if len(r) else np.nan)
            x = np.arange(len(sizes)) + (j - (len(present) - 1) / 2) * w
            ax.bar(x, vals, width=w * 0.88, color=OS_COLOR[osn], zorder=3)
            for xi, v in zip(x, vals):
                if v == v:
                    ax.text(xi, v * 1.03, f"{v:,.0f}", ha="center", va="bottom", fontsize=8, color=INK2)
        ax.set_xticks(range(len(sizes)))
        ax.set_xticklabels([human_size(z) for z in sizes], color=INK)
        ax.set_xlabel("mapping size")
        ax.set_title(f"{kind.capitalize()} memory", loc="left", fontsize=11, color=INK)
    axes[0].set_ylabel("nanoseconds per 4 KB page (lower is faster)")
    if len(present) >= 2:
        legend_for(axes[-1], present)
        axes[-1].legend_.set_loc("upper left")
    fig.suptitle("Cost of touching a mapped page for the first time", x=0.01, ha="left",
                 fontsize=12, color=INK)
    footer(fig, note)
    path = os.path.join(outdir, "memory_page_cost.png")
    fig.savefig(path, dpi=160, facecolor=SURFACE)
    plt.close(fig)
    return path


# ---------------------------------------------------------------------------
# tables
# ---------------------------------------------------------------------------
def write_markdown(s, oses, path, meta_lines):
    out = ["# Benchmark summary", ""]
    out += meta_lines
    out += ["", "Times are the median of the per-run medians; spread is (max - min) / median of the per-run "
            "medians. CPU is the user+system time of the benchmark process inside the timed calls, divided by "
            "the number of calls (a child's own CPU is not included); on Windows it is shown only when the "
            "case used at least 150 ms of CPU in total, because Windows counts CPU in ~15.6 ms ticks.", ""]
    for group in dict.fromkeys(s.group):
        g = s[s.group == group]
        out += [f"## {GROUP_TITLES.get(group, group)}", ""]
        head = ["call", "size"]
        for o in oses:
            head += [f"{OS_LABEL[o]} median", f"{OS_LABEL[o]} p95", f"{OS_LABEL[o]} spread", f"{OS_LABEL[o]} CPU/call"]
        if len(oses) == 2:
            head.append("Windows / Linux")
        out += ["| " + " | ".join(head) + " |", "|" + "---|" * len(head)]
        for call, size in dict.fromkeys(zip(g.call, g["size"])):
            row = [f"`{call}`", human_size(size) or "-"]
            med = {}
            for o in oses:
                r = g[(g.os == o) & (g.call == call) & (g["size"] == size)]
                if len(r):
                    r = r.iloc[0]
                    med[o] = r.median_ns
                    cpu = fmt_ns(r.cpu_ns_per_call) if r.cpu_reliable else "n/a"
                    row += [fmt_ns(r.median_ns), fmt_ns(r.p95_ns), f"{r.spread_pct:.0f}%", cpu]
                else:
                    row += ["-", "-", "-", "-"]
            if len(oses) == 2:
                if call in NO_KERNEL_ENTRY_ON_WINDOWS:
                    row.append("n/a \u2020")
                else:
                    row.append(f"{med['windows'] / med['linux']:.2f}x" if len(med) == 2 else "-")
            out.append("| " + " | ".join(row) + " |")
        out.append("")
    if len(oses) == 2:
        out += ["\u2020 On Windows this call does not enter the kernel (`plat_getpid` reads a thread field, "
                "`plat_umask` stores a number), so a ratio against the Linux system call is not meaningful.", ""]
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")


def read_meta(paths):
    lines = ["Machines:", ""]
    for p in paths:
        m = p + ".meta.txt"
        if os.path.exists(m):
            with open(m, encoding="utf-8") as f:
                body = [ln.rstrip() for ln in f if ln.strip()]
            lines.append(f"- `{os.path.basename(p)}`: " + "; ".join(body))
    return lines if len(lines) > 2 else []


# ---------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv", nargs="+", help="one or more benchmark CSV files")
    ap.add_argument("--out", default="results/plots", help="output folder (default results/plots)")
    ap.add_argument("--note", default="", help="extra text for the chart footer, e.g. 'Linux = WSL2'")
    args = ap.parse_args()

    df = load(args.csv)
    quick = [c for c in args.csv if os.path.exists(c + ".meta.txt") and "quick:     yes" in open(c + ".meta.txt").read()]
    if quick:
        print(f"analyze: WARNING {quick} came from --quick and is not a real measurement", file=sys.stderr)

    os.makedirs(args.out, exist_ok=True)
    s = summarise(df)
    oses = [o for o in ("linux", "windows") if o in set(s.os)] + sorted(set(s.os) - {"linux", "windows"})
    runs = int(df.groupby("os").run.nunique().min())
    extra = f"; {args.note}" if args.note else ""
    note = f"median of per-run medians over {runs} run(s){extra}"
    note_dot = f"dot = median of per-run medians; line = fastest to slowest of {runs} run(s){extra}"

    s.drop(columns=[]).to_csv(os.path.join(args.out, "summary.csv"), index=False, float_format="%.3f")
    write_markdown(s, oses, os.path.join(args.out, "summary.md"), read_meta(args.csv))

    made = []
    for group in dict.fromkeys(s.group):
        made.append(chart_group(s, group, oses, args.out, note_dot))
    made.append(chart_throughput(s, oses, args.out, note))
    made.append(chart_page_cost(s, oses, args.out, note))
    made.append(chart_ratio(s, oses, args.out, note))
    made = [m for m in made if m]

    print(f"analyze: {len(df)} rows, systems: {', '.join(oses)}, runs per system: {runs}")
    print(f"analyze: wrote summary.csv, summary.md and {len(made)} charts to {args.out}")
    for m in made:
        print("  ", m)


if __name__ == "__main__":
    main()
