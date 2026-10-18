#!/usr/bin/env python3
"""
plot_track.py - one picture of the filter at work, for the report and the slides.

    python tools/plot_track.py --meas data/meas_medium.bin --est data/est_serial_medium.bin \
           --track 0 --steps 400 --out results/plots/track_example.png

Left: the path of one target - noisy measurements, the true path and the filter's
estimate. Right: position error over time for the raw measurement and for the
filter, with the filter's predicted steady-state error (from its own covariance).
The true path is regenerated from the seed in the file header with the Python
mirror of the generator, so no truth file is needed.
"""

import argparse
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kf_reference as ref  # noqa: E402

# Same fixed colours as bench/analyze.py: raw measurement = orange, filter = blue.
RAW = "#eb6834"
FILTER = "#2a78d6"
SURFACE = "#fcfcfb"
INK = "#0b0b0b"
INK2 = "#52514e"
GRID = "#e4e3df"


def steady_state(par):
    """Predicted steady-state position error from the covariance recursion (data-independent)."""
    dt, q, r = par["dt"], par["sigma_a"] ** 2, par["sigma_r"] ** 2
    F = np.array([[1, 0, dt, 0], [0, 1, 0, dt], [0, 0, 1, 0], [0, 0, 0, 1.0]])
    H = np.array([[1.0, 0, 0, 0], [0, 1.0, 0, 0]])
    G = np.array([[dt * dt / 2, 0], [0, dt * dt / 2], [dt, 0], [0, dt]])
    Q = q * G @ G.T
    P = np.diag([par["sigma_p0"] ** 2] * 2 + [par["sigma_v0"] ** 2] * 2)
    for k in range(3000):
        if k:
            P = F @ P @ F.T + Q
        S = H @ P @ H.T + r * np.eye(2)
        K = P @ H.T @ np.linalg.inv(S)
        P = (np.eye(4) - K @ H) @ P
    return float(np.sqrt(P[0, 0] + P[1, 1]))


def style(ax):
    ax.set_facecolor(SURFACE)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(GRID)
    ax.tick_params(colors=INK2, labelsize=9)
    ax.grid(color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--meas", required=True)
    ap.add_argument("--est", required=True)
    ap.add_argument("--track", type=int, default=0)
    ap.add_argument("--steps", type=int, default=400)
    ap.add_argument("--out", default="results/plots/track_example.png")
    args = ap.parse_args()

    par, meas = ref.read_file(args.meas)
    _pe, est = ref.read_file(args.est)
    n = min(args.steps, par["n_steps"])
    truth, z_regen = ref.gen_track(par, args.track, n)
    z = np.asarray(meas[args.track][:n])
    if not np.array_equal(z, z_regen):
        sys.exit("plot_track: regenerated measurements differ from the file; wrong file pair?")
    e = np.asarray(est[args.track][:n])

    err_raw = np.hypot(z[:, 0] - truth[:, 0], z[:, 1] - truth[:, 1])
    err_flt = np.hypot(e[:, 0] - truth[:, 0], e[:, 1] - truth[:, 1])
    pred = steady_state(par)
    t = np.arange(n) * par["dt"]

    fig, (a, b) = plt.subplots(1, 2, figsize=(11, 4.6), facecolor=SURFACE,
                               gridspec_kw={"width_ratios": [1, 1.15]})
    fig.subplots_adjust(left=0.07, right=0.985, top=0.84, bottom=0.2, wspace=0.18)

    style(a)
    a.plot(z[:, 0], z[:, 1], linestyle="none", marker="o", markersize=3, color=RAW, alpha=0.45,
           markeredgewidth=0, label="measurements")
    a.plot(truth[:, 0], truth[:, 1], color=INK2, linewidth=1.2, label="true path")
    a.plot(e[:, 0], e[:, 1], color=FILTER, linewidth=1.6, label="filter estimate")
    a.set_aspect("equal", adjustable="datalim")
    a.set_xlabel("x (m)", color=INK2)
    a.set_ylabel("y (m)", color=INK2)
    a.set_title(f"Track {args.track}: first {n} steps", loc="left", fontsize=11, color=INK)
    leg = a.legend(frameon=False, fontsize=9, loc="best")
    for tx in leg.get_texts():
        tx.set_color(INK)

    style(b)
    b.plot(t, err_raw, color=RAW, linewidth=0.9, alpha=0.8, label="raw measurement")
    b.plot(t, err_flt, color=FILTER, linewidth=1.6, label="filter")
    b.axhline(pred, color=INK2, linestyle=(0, (4, 3)), linewidth=1.1, label=f"filter, predicted ({pred:.2f} m)")
    b.set_yscale("log")
    b.set_xlabel("time (s)", color=INK2)
    b.set_ylabel("position error (m, log scale)", color=INK2)
    b.set_title("Position error over time", loc="left", fontsize=11, color=INK)
    b.set_ylim(0.02, 300)
    leg = b.legend(frameon=False, fontsize=8.5, loc="upper center", ncol=3, columnspacing=1.2, handlelength=1.8)
    for tx in leg.get_texts():
        tx.set_color(INK)

    fig.text(0.01, 0.01, f"seed {par['seed']}, dt {par['dt']} s, measurement noise {par['sigma_r']} m, "
             f"acceleration noise {par['sigma_a']} m/s²", fontsize=8, color=INK2)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    fig.savefig(args.out, dpi=160, facecolor=SURFACE)
    print("plot_track: wrote", args.out)


if __name__ == "__main__":
    main()
