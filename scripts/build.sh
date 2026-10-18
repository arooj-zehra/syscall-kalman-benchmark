#!/usr/bin/env bash
# Configure, build, test and run the smoke test on Linux / WSL.
# Usage: bash scripts/build.sh [Release|Debug]
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD_TYPE="${1:-Release}"

if [[ "$PWD" == /mnt/* ]]; then
    echo "WARNING: this project is under $PWD (the Windows drive)." >&2
    echo "         File I/O across /mnt is much slower and would distort Linux benchmarks." >&2
    echo "         Clone it inside the Linux filesystem, e.g. ~/os-ccp." >&2
fi

if command -v ninja >/dev/null 2>&1; then
    GENERATOR="Ninja"
else
    GENERATOR="Unix Makefiles"
fi

echo "== Configure ($GENERATOR, $BUILD_TYPE)"
cmake -S . -B build -G "$GENERATOR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

echo "== Build"
cmake --build build

echo "== Test"
ctest --test-dir build --output-on-failure

echo "== Smoke test output"
./build/bin/hello build/hello.log
