#!/usr/bin/env bash
# Scaling experiment on Linux / WSL: serial vs. 1..N workers, written to results/scaling_linux.csv.
#
# Usage: bash scripts/run_scaling.sh [-p small|medium|large] [-n RUNS] [-r ROUNDS] [-w "1 2 4 8"] [-o FILE] [--overwrite]
#   -p PRESET    data set (default large: 256 tracks x 100,000 steps)
#   -n RUNS      how many times to repeat the whole sweep (default 5)
#   -r ROUNDS    filter passes inside one process; the median is reported (default 3)
#   -w LIST      worker counts (default "1 2 3 4 5 6 7 8 12 16")
#   -o FILE      CSV to write (default results/scaling_linux.csv)
#   --overwrite  replace FILE if it already exists
#
# Every row is one process run. Parallel rows are checked against the serial
# result (--expect-file) and are only written if the hash matches. No result file
# is written, so disk speed is not part of the measurement. Plug in the power and
# close other programs first; keep the project inside the Linux file system.
set -euo pipefail
cd "$(dirname "$0")/.."

PRESET=large
RUNS=5
ROUNDS=3
WORKERS="1 2 3 4 5 6 7 8 12 16"
OUT=results/scaling_linux.csv
OVERWRITE=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        -p) PRESET="$2"; shift 2 ;;
        -n) RUNS="$2"; shift 2 ;;
        -r) ROUNDS="$2"; shift 2 ;;
        -w) WORKERS="$2"; shift 2 ;;
        -o) OUT="$2"; shift 2 ;;
        --overwrite) OVERWRITE=1; shift ;;
        -h|--help) sed -n '2,17p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

BIN=build/bin
for exe in kfgen kf_serial kf_master kf_worker; do
    [[ -x "$BIN/$exe" ]] || { echo "ERROR: $BIN/$exe missing. Build first:  bash scripts/build.sh" >&2; exit 1; }
done
if ! grep -q "CMAKE_BUILD_TYPE:STRING=Release" build/CMakeCache.txt 2>/dev/null; then
    echo "WARNING: build is not Release; timings will be pessimistic." >&2
fi
if [[ "$PWD" == /mnt/* ]]; then
    echo "WARNING: running under $PWD (the Windows drive); file access will be slow and distorted." >&2
fi
if [[ -e "$OUT" && $OVERWRITE -eq 0 ]]; then
    echo "ERROR: $OUT exists. Use --overwrite to replace it, or -o to pick another file." >&2
    exit 1
fi

IN="data/meas_$PRESET.bin"
REF="data/est_serial_$PRESET.bin"
mkdir -p data results
if [[ ! -f "$IN" ]]; then
    echo "== generating $IN"
    "$BIN/kfgen" --preset "$PRESET" --out "$IN"
fi
if [[ ! -f "$REF" ]]; then
    echo "== computing the serial reference $REF"
    "$BIN/kf_serial" --in "$IN" --out "$REF" >/dev/null
fi
rm -f "$OUT"
{
    echo "cpu_model: $(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2- | sed 's/^ *//')"
    echo "logical_cpus: $(nproc)"
    echo "kernel:    $(uname -r)"
    echo "preset:    $PRESET   runs: $RUNS   rounds per run: $ROUNDS   workers: $WORKERS"
} > "$OUT.meta.txt"

echo "== warm-up (fills the page cache; not recorded)"
"$BIN/kf_serial" --in "$IN" >/dev/null

START=$(date +%s)
for run in $(seq 1 "$RUNS"); do
    echo "== run $run / $RUNS"
    "$BIN/kf_serial" --in "$IN" --repeat "$ROUNDS" --csv "$OUT" | grep -E "compute :"
    for w in $WORKERS; do
        printf "   %2d workers: " "$w"
        "$BIN/kf_master" --in "$IN" --workers "$w" --repeat "$ROUNDS" --expect-file "$REF" --csv "$OUT" \
            | grep -E "compute :"
    done
done
echo "== done in $(( $(date +%s) - START )) s -> $OUT"
echo "   next:  python3 tools/plot_scaling.py --linux $OUT --cores $(nproc)"
