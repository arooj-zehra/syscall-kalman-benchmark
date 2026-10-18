#!/usr/bin/env bash
# Run the whole benchmark suite on Linux / WSL and write results/linux.csv.
#
# Usage: bash scripts/run_bench.sh [-n RUNS] [-o FILE] [--overwrite] [--quick]
#   -n RUNS      how many times to repeat the whole suite (default 5)
#   -o FILE      CSV to write (default results/linux.csv)
#   --overwrite  replace FILE if it already exists
#   --quick      tiny smoke-test sizes; the numbers are NOT valid measurements
#
# Before a real run: build in Release (scripts/build.sh), plug in the power, close
# browsers and other heavy programs, and keep the project inside the Linux file
# system (~/os-ccp), not under /mnt/c.
set -euo pipefail

cd "$(dirname "$0")/.."

RUNS=5
OUT=results/linux.csv
OVERWRITE=0
QUICK=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        -n) RUNS="$2"; shift 2 ;;
        -o) OUT="$2"; shift 2 ;;
        --overwrite) OVERWRITE=1; shift ;;
        --quick) QUICK="--quick"; shift ;;
        -h|--help) sed -n '2,13p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

BENCH=build/bin/bench
if [[ ! -x "$BENCH" ]]; then
    echo "ERROR: $BENCH not found. Build first:  bash scripts/build.sh" >&2
    exit 1
fi
if ! grep -q "CMAKE_BUILD_TYPE:STRING=Release" build/CMakeCache.txt 2>/dev/null; then
    echo "WARNING: build is not Release; timings will be pessimistic." >&2
fi
if [[ "$PWD" == /mnt/* ]]; then
    echo "WARNING: running under $PWD (the Windows drive); file I/O numbers will be distorted." >&2
fi
if [[ -e "$OUT" && $OVERWRITE -eq 0 ]]; then
    echo "ERROR: $OUT exists. Use --overwrite to replace it, or -o to pick another file." >&2
    exit 1
fi

mkdir -p "$(dirname "$OUT")" results/tmp
rm -f "$OUT" "$OUT.meta.txt"

# Extra machine details next to the CSV (best effort).
{
    echo "cpu_model: $(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2- | sed 's/^ *//')"
    echo "kernel:    $(uname -r)"
    echo "memory:    $(free -m 2>/dev/null | awk '/^Mem:/ {print $2 " MB"}')"
    echo "governor:  $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo n/a)"
} > "$OUT.extra.txt"

START=$(date +%s)
for ((r = 1; r <= RUNS; r++)); do
    echo "=============== run $r of $RUNS ==============="
    "$BENCH" --out "$OUT" --run "$r" --tmpdir results/tmp $QUICK
done
cat "$OUT.extra.txt" >> "$OUT.meta.txt"
rm -f "$OUT.extra.txt"
rmdir results/tmp 2>/dev/null || true

echo
echo "Done in $(( $(date +%s) - START )) s. Results: $OUT ($(($(wc -l < "$OUT") - 1)) rows)"
echo "Next: python3 bench/analyze.py $OUT [results/windows.csv] --out results/plots"
