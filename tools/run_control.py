#!/usr/bin/env python3
"""
run_control.py - how much of the hardware's parallelism does the parallel filter use?

    python tools/run_control.py [--preset medium] [--workers 1 2 3 4 6 8 12] [--sweeps 5] [--repeat 15]

The scaling sweep (scripts/run_scaling.*) shows the speedup of kf_master. It cannot say
whether a speedup of 3.4 on 4 cores / 8 threads is a weakness of the design or simply
all this CPU can do (hyper-threads share a core, the clock drops when more cores are
busy, caches and memory are shared). This script measures the ceiling directly:

  control   N completely independent kf_serial processes started at the same time:
            no shared memory, no pipes, no master. Whatever slowdown each one suffers
            is the hardware's (and the OS scheduler's), not the design's.
  master    kf_master with N workers on the same data.

For each N it reports
  slowdown          time of one control process with N running / time with 1 running
  control speedup   N independent processes' combined throughput / throughput of one
  master speedup    kf_master throughput with N workers / throughput of kf_serial
  design efficiency master throughput / control throughput  (1.0 = the design loses nothing
                    the hardware would not have taken anyway)

Medium preset by default: N copies of the large preset would need N x 780 MB of memory.
Writes results/control_<os>.csv (one row per process group per sweep).
"""

import argparse
import csv
import os
import re
import statistics
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OS = "windows" if sys.platform.startswith("win") else "linux"
EXE = ".exe" if OS == "windows" else ""
COMPUTE = re.compile(r"compute :\s+([0-9.]+) ms")


def exe(name):
    path = os.path.join(ROOT, "build", "bin", name + EXE)
    if not os.path.exists(path):
        sys.exit(f"{path} not found - build first")
    return path


def compute_ms(text, who):
    m = COMPUTE.search(text)
    if not m:
        sys.exit(f"no 'compute :' line in the output of {who}:\n{text}")
    return float(m.group(1))


def run_control(n, data, repeat):
    """N independent serial processes at once; returns their compute times."""
    cmd = [exe("kf_serial"), "--in", data, "--repeat", str(repeat)]
    procs = [subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, cwd=ROOT)
             for _ in range(n)]
    times = []
    for i, p in enumerate(procs):
        out, _ = p.communicate()
        if p.returncode != 0:
            sys.exit(f"control process {i} failed:\n{out}")
        times.append(compute_ms(out, f"control process {i}"))
    return times


def run_master(n, data, ref, repeat):
    cmd = [exe("kf_master"), "--in", data, "--workers", str(n), "--repeat", str(repeat), "--expect-file", ref]
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, cwd=ROOT)
    if p.returncode != 0 or "verified: yes" not in p.stdout:
        sys.exit(f"kf_master with {n} workers failed or was not verified:\n{p.stdout}")
    return compute_ms(p.stdout, f"kf_master {n}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--preset", default="medium")
    ap.add_argument("--workers", type=int, nargs="+", default=[1, 2, 3, 4, 6, 8, 12])
    ap.add_argument("--sweeps", type=int, default=5)
    ap.add_argument("--repeat", type=int, default=15, help="filter passes inside each process (the median is used)")
    ap.add_argument("--out", default=os.path.join(ROOT, "results", f"control_{OS}.csv"))
    ap.add_argument("--overwrite", action="store_true")
    a = ap.parse_args()

    data = os.path.join(ROOT, "data", f"meas_{a.preset}.bin")
    ref = os.path.join(ROOT, "data", f"est_serial_{a.preset}.bin")
    for f in (data, ref):
        if not os.path.exists(f):
            sys.exit(f"{f} missing - run scripts/make_data first with the same preset")
    if os.path.exists(a.out) and not a.overwrite:
        sys.exit(f"{a.out} exists; use --overwrite")
    os.makedirs(os.path.dirname(a.out), exist_ok=True)

    # tracks x steps from the file name's preset: read it from kf_serial's own output
    probe = subprocess.run([exe("kf_serial"), "--in", data], stdout=subprocess.PIPE, text=True, cwd=ROOT).stdout
    m = re.search(r"(\d+) tracks x (\d+) steps", probe)
    steps_total = int(m.group(1)) * int(m.group(2))

    rows = []
    for sweep in range(1, a.sweeps + 1):
        print(f"== sweep {sweep} / {a.sweeps}", flush=True)
        for n in a.workers:
            t = run_control(n, data, a.repeat)
            ctl_mean = statistics.mean(t)
            ctl_tp = sum(steps_total / (x / 1e3) for x in t) / 1e6   # million steps per second, all processes
            mas = run_master(n, data, ref, a.repeat)
            mas_tp = steps_total / (mas / 1e3) / 1e6
            print(f"   N={n:2d}  control: {ctl_mean:8.1f} ms each ({ctl_tp:6.2f} Mstep/s total)   "
                  f"master: {mas:8.1f} ms ({mas_tp:6.2f} Mstep/s)", flush=True)
            rows.append(dict(os=OS, sweep=sweep, n=n, control_ms_mean=ctl_mean, control_ms_max=max(t),
                             control_mstep_s=ctl_tp, master_ms=mas, master_mstep_s=mas_tp))
    with open(a.out, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)

    # summary: median over sweeps, relative to N = 1
    base = {k: statistics.median(r[k] for r in rows if r["n"] == a.workers[0])
            for k in ("control_ms_mean", "control_mstep_s", "master_mstep_s")}
    print(f"\n{OS}: {a.preset} preset, {a.sweeps} sweeps (median)  -> {a.out}")
    print(f"{'N':>3} {'slowdown':>9} {'ctl speedup':>12} {'master speedup':>15} {'design eff.':>12}")
    ref_tp = base["control_mstep_s"]
    for n in a.workers:
        g = [r for r in rows if r["n"] == n]
        slow = statistics.median(r["control_ms_mean"] for r in g) / base["control_ms_mean"]
        cs = statistics.median(r["control_mstep_s"] for r in g) / ref_tp
        ms = statistics.median(r["master_mstep_s"] for r in g) / ref_tp
        print(f"{n:>3} {slow:>8.2f}x {cs:>12.2f} {ms:>15.2f} {ms / cs:>12.2f}")


if __name__ == "__main__":
    main()
