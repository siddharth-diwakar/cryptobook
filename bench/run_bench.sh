#!/usr/bin/env bash
# Reproduce every benchmark number the README quotes (golden rule #5).
# Builds Release with benchmarks enabled and runs them, printing machine info.
set -euo pipefail

cd "$(dirname "$0")/.."

echo "=== machine ==="
uname -a
if [ -f /proc/cpuinfo ]; then
  grep -m1 "model name" /proc/cpuinfo || true
elif command -v sysctl >/dev/null 2>&1; then
  sysctl -n machdep.cpu.brand_string || true
fi
echo

cmake -B build -DCMAKE_BUILD_TYPE=Release -DASMM_BUILD_BENCH=ON >/dev/null
cmake --build build -j --target bench_book bench_spsc >/dev/null

echo "=== bench_spsc ==="
./build/bench_spsc
echo
echo "=== bench_book (asserts p99 < 5us) ==="
./build/bench_book
