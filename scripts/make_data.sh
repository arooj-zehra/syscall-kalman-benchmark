#!/usr/bin/env bash
# Generate a data set, run the serial filter on it and check the result (Linux / WSL).
# Usage: bash scripts/make_data.sh [small|medium|large]      (default: medium)
#   small   16 tracks x    2,000 steps  (0.5 MiB)
#   medium 128 tracks x   20,000 steps  (39 MiB)
#   large  256 tracks x  100,000 steps  (391 MiB input, 781 MiB output)
# Writes data/meas_<preset>.bin, data/est_serial_<preset>.bin and appends a timing
# row to results/serial_linux.csv. The data files are not committed (see .gitignore).
set -euo pipefail
cd "$(dirname "$0")/.."

PRESET="${1:-medium}"
BIN=build/bin
if [[ ! -x "$BIN/kfgen" ]]; then
    echo "ERROR: build first:  bash scripts/build.sh" >&2
    exit 1
fi
mkdir -p data results

echo "== generate ($PRESET)"
"$BIN/kfgen" --preset "$PRESET" --out "data/meas_$PRESET.bin"

echo "== serial filter"
"$BIN/kf_serial" --in "data/meas_$PRESET.bin" --out "data/est_serial_$PRESET.bin" \
    --io mmap --csv results/serial_linux.csv

echo "== evaluate"
"$BIN/kf_eval" --meas "data/meas_$PRESET.bin" --est "data/est_serial_$PRESET.bin" --check
