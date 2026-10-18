#!/usr/bin/env python3
"""
kf_reference.py - an independent implementation used to check the C code.

Two things are mirrored here, written from the specification in
docs/KALMAN_DESIGN.md and not copied from the C source's structure:

  1. The data generator (splitmix64, xoshiro256**, Irwin-Hall normals, the
     truth and measurement recurrences). Python floats are IEEE doubles and the
     operations are written in the same order, so the result must equal the C
     output BIT FOR BIT.
  2. A textbook Kalman filter with numpy (standard covariance update, matrix
     inverse). It is a different algorithm from the C Joseph-form code, so the
     two agree only to rounding error (about 1e-12 relative).

Usage
    python tools/kf_reference.py check --meas M.bin --est E.bin [--tracks 4] [--tol 1e-6]
    python tools/kf_reference.py hash  FILE.bin
    python tools/kf_reference.py pins            # values pinned in src/kalman/test_kalman.c

`check` passes (exit 0) only if, for the first --tracks tracks,
  a) the measurements regenerated here equal the file's measurements exactly,
  b) the textbook filter run on those measurements equals the C estimates to --tol,
  c) the header hash equals the hash of the data (when the file is small enough).
"""

import argparse
import struct
import sys

import numpy as np

MASK = (1 << 64) - 1
HEADER_BYTES = 4096
FNV_INIT = 14695981039346656037
FNV_PRIME = 1099511628211

# header layout, see src/kalman/kfio.h
HEADER_FMT = "<8sIIQQQddddd Q".replace(" ", "")


# ---------------------------------------------------------------- generator
def splitmix64(state):
    state = (state + 0x9E3779B97F4A7C15) & MASK
    z = state
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK
    return state, z ^ (z >> 31)


def rotl(x, k):
    return ((x << k) | (x >> (64 - k))) & MASK


class Rng:
    def __init__(self, seed, track):
        sm, first = splitmix64(seed & MASK)
        sm = first ^ ((track * 0xD1B54A32D192ED03) & MASK)
        s = []
        for _ in range(4):
            sm, v = splitmix64(sm)
            s.append(v)
        self.s = s

    def next(self):
        s = self.s
        result = (rotl((s[1] * 5) & MASK, 7) * 9) & MASK
        t = (s[1] << 17) & MASK
        s[2] ^= s[0]
        s[3] ^= s[1]
        s[1] ^= s[2]
        s[0] ^= s[3]
        s[2] ^= t
        s[3] = rotl(s[3], 45)
        return result

    def uniform(self):
        return (self.next() >> 11) * (1.0 / 9007199254740992.0)

    def normal(self):
        total = 0.0
        for _ in range(12):
            total += self.uniform()
        return total - 6.0


def gen_track(par, track, n_steps):
    """Return (truth[n,4], meas[n,2]) as float64 arrays. par: dict of header parameters."""
    rng = Rng(par["seed"], track)
    dt = par["dt"]
    half_dt2 = 0.5 * dt * dt
    sa, sr = par["sigma_a"], par["sigma_r"]
    px = par["sigma_p0"] * rng.normal()
    py = par["sigma_p0"] * rng.normal()
    vx = par["sigma_v0"] * rng.normal()
    vy = par["sigma_v0"] * rng.normal()
    truth = np.empty((n_steps, 4))
    meas = np.empty((n_steps, 2))
    for k in range(n_steps):
        if k > 0:
            ax = sa * rng.normal()
            ay = sa * rng.normal()
            px = px + vx * dt + ax * half_dt2
            py = py + vy * dt + ay * half_dt2
            vx = vx + ax * dt
            vy = vy + ay * dt
        meas[k, 0] = px + sr * rng.normal()
        meas[k, 1] = py + sr * rng.normal()
        truth[k] = (px, py, vx, vy)
    return truth, meas


# ------------------------------------------------------------------- filter
def filter_track(par, meas):
    """Textbook Kalman filter (standard covariance update, explicit inverse)."""
    dt = par["dt"]
    q = par["sigma_a"] ** 2
    r = par["sigma_r"] ** 2
    F = np.array([[1, 0, dt, 0], [0, 1, 0, dt], [0, 0, 1, 0], [0, 0, 0, 1.0]])
    H = np.array([[1.0, 0, 0, 0], [0, 1.0, 0, 0]])
    G = np.array([[dt * dt / 2, 0], [0, dt * dt / 2], [dt, 0], [0, dt]])
    Q = q * G @ G.T
    R = r * np.eye(2)
    x = np.zeros(4)
    P = np.diag([par["sigma_p0"] ** 2] * 2 + [par["sigma_v0"] ** 2] * 2)
    est = np.empty((len(meas), 4))
    for k, z in enumerate(meas):
        if k > 0:
            x = F @ x
            P = F @ P @ F.T + Q
        S = H @ P @ H.T + R
        K = P @ H.T @ np.linalg.inv(S)
        x = x + K @ (z - H @ x)
        P = (np.eye(4) - K @ H) @ P
        est[k] = x
    return est


# --------------------------------------------------------------------- files
def read_file(path, mmap=True):
    with open(path, "rb") as f:
        head = f.read(HEADER_BYTES)
    magic, version, vps, n_tracks, n_steps, seed, dt, sa, sr, sp0, sv0, dhash = struct.unpack_from(
        HEADER_FMT, head
    )
    par = dict(magic=magic.decode(), version=version, values_per_step=vps, n_tracks=n_tracks,
               n_steps=n_steps, seed=seed, dt=dt, sigma_a=sa, sigma_r=sr, sigma_p0=sp0,
               sigma_v0=sv0, data_hash=dhash)
    data = np.memmap(path, dtype="<f8", mode="r", offset=HEADER_BYTES,
                     shape=(n_tracks, n_steps, vps))
    return par, data


def fnv_words(array):
    words = np.ascontiguousarray(array).view("<u8").ravel()
    h = FNV_INIT
    for w in words.tolist():
        h = ((h ^ w) * FNV_PRIME) & MASK
    return h


# ------------------------------------------------------------------ commands
def cmd_check(args):
    par_m, meas = read_file(args.meas)
    par_e, est = read_file(args.est)
    ok = True
    n_tr = min(args.tracks, par_m["n_tracks"])
    n_st = par_m["n_steps"]

    if par_m["magic"] != "SKKFMEAS" or par_e["magic"] != "SKKFEST0":
        print("FAIL: wrong magic in one of the files")
        return 1
    for key in ("n_tracks", "n_steps", "seed"):
        if par_m[key] != par_e[key]:
            print(f"FAIL: header field {key} differs between the files")
            return 1

    for t in range(n_tr):
        _truth, z = gen_track(par_m, t, n_st)
        if not np.array_equal(z, np.asarray(meas[t])):
            bad = int(np.argmax(np.any(z != np.asarray(meas[t]), axis=1)))
            print(f"FAIL: track {t}: generator output differs from the file (first at step {bad})")
            ok = False
        else:
            print(f"pass: track {t}: regenerated measurements are bit-identical to the C output")

        ref = filter_track(par_m, np.asarray(meas[t]))
        diff = float(np.max(np.abs(ref - np.asarray(est[t]))))
        scale = float(np.max(np.abs(ref))) or 1.0
        if diff > args.tol:
            print(f"FAIL: track {t}: textbook filter differs from the C filter by {diff:.3e}")
            ok = False
        else:
            print(f"pass: track {t}: textbook filter agrees with the C filter "
                  f"(max difference {diff:.2e}, values up to {scale:.0f})")

    for name, par, data in (("measurement", par_m, meas), ("estimate", par_e, est)):
        if data.size <= 4_000_000:
            h = fnv_words(np.asarray(data))
            if h == par["data_hash"]:
                print(f"pass: {name} file hash 0x{h:016x} matches its header")
            else:
                print(f"FAIL: {name} file hash 0x{h:016x} != header 0x{par['data_hash']:016x}")
                ok = False
        else:
            print(f"skip: {name} file too large to hash in Python")

    print("REFERENCE CHECK PASSED" if ok else "REFERENCE CHECK FAILED")
    return 0 if ok else 1


def cmd_hash(args):
    par, data = read_file(args.file)
    print(f"0x{fnv_words(np.asarray(data)):016x}  (header: 0x{par['data_hash']:016x})")
    return 0


def cmd_pins(_args):
    par = dict(seed=1, dt=0.1, sigma_a=0.5, sigma_r=5.0, sigma_p0=1000.0, sigma_v0=10.0)
    _t, z = gen_track(par, 0, 4)
    flat = z.ravel()
    for idx in (0, 7):
        bits = struct.unpack("<Q", struct.pack("<d", float(flat[idx])))[0]
        print(f"meas[{idx}] = {flat[idx]!r}  bits 0x{bits:016x}ull")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("check")
    c.add_argument("--meas", required=True)
    c.add_argument("--est", required=True)
    c.add_argument("--tracks", type=int, default=4)
    c.add_argument("--tol", type=float, default=1e-6)
    c.set_defaults(fn=cmd_check)
    h = sub.add_parser("hash")
    h.add_argument("file")
    h.set_defaults(fn=cmd_hash)
    p = sub.add_parser("pins")
    p.set_defaults(fn=cmd_pins)
    args = ap.parse_args()
    sys.exit(args.fn(args))


if __name__ == "__main__":
    main()
