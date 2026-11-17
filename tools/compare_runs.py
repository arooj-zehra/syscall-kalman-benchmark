#!/usr/bin/env python3
"""
compare_runs.py - A/B comparison of two benchmark CSVs from the SAME machine, for testing a hypothesis.

    python tools/compare_runs.py results/hyp_default.csv results/hyp_changed.csv \
           --a-name "Defender on" --b-name "folder excluded" [--group fileio]

Each file is written by `bench --out FILE --run N` (repeat it a few times with --run 1..N).
For every (call, size) it prints the median of the per-run medians for A and B, the ratio B/A and
whether the two sets of runs overlap (min..max). Overlapping ranges mean: no measurable difference,
whatever the ratio says. The verdict column follows that rule, which is the rule the report uses.
"""

import argparse

import pandas as pd


def summarise(path, group):
    d = pd.read_csv(path)
    if group:
        d = d[d["group"].isin(group.split(","))]
    g = d.groupby(["group", "call", "size"])["median_ns"]
    return pd.DataFrame({"med": g.median(), "lo": g.min(), "hi": g.max(), "runs": g.count()})


def fmt(ns):
    if ns >= 1e6:
        return f"{ns / 1e6:.2f} ms"
    if ns >= 1e3:
        return f"{ns / 1e3:.2f} us"
    return f"{ns:.1f} ns"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--a-name", default="A")
    ap.add_argument("--b-name", default="B")
    ap.add_argument("--group", help="comma-separated groups, e.g. fileio,process")
    ap.add_argument("--min-ratio", type=float, default=0.0, help="hide rows whose ratio is closer to 1 than this")
    a = ap.parse_args()
    A, B = summarise(a.a, a.group), summarise(a.b, a.group)
    j = A.join(B, lsuffix="_a", rsuffix="_b", how="inner")
    print(f"{'call':<26}{'size':>9}  {a.a_name:>14}  {a.b_name:>14}  {'B/A':>6}  verdict")
    for (grp, call, size), r in j.iterrows():
        ratio = r.med_b / r.med_a
        overlap = not (r.hi_a < r.lo_b or r.hi_b < r.lo_a)
        if abs(ratio - 1) < a.min_ratio:
            continue
        verdict = "no measurable difference (ranges overlap)" if overlap else ("B slower" if ratio > 1 else "B faster")
        print(f"{call:<26}{int(size):>9}  {fmt(r.med_a):>14}  {fmt(r.med_b):>14}  {ratio:>5.2f}x  {verdict}")
    print(f"\nruns per call: A {int(j.runs_a.min())}-{int(j.runs_a.max())}, B {int(j.runs_b.min())}-{int(j.runs_b.max())}")


if __name__ == "__main__":
    main()
