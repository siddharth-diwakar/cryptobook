#!/usr/bin/env bash
# Reproduce the Avellaneda-Stoikov gamma sweep (docs/GAMMA_SWEEP.md).
# Replays the recorded fixture (or a given log) through the strategy for a grid
# of gamma values. Deterministic, offline, no network.
#
# Usage: scripts/gamma_sweep.sh [log.bin]   (default: build from the committed fixture)
set -euo pipefail
cd "$(dirname "$0")/.."

LOG=${1:-}
if [ -z "$LOG" ]; then
  JSONL=tests/data/depth_btcusdt.jsonl
  [ -f "$JSONL" ] || JSONL=build/testdata/depth_btcusdt.jsonl
  LOG=/tmp/asmm_sweep.bin
  ./build/asmm --jsonl-to-log "$JSONL" "$LOG" >/dev/null
fi

echo "| gamma | quotes | fills | final_q | max|q| | mean_q | cross_viol |"
echo "|---|---|---|---|---|---|---|"
for G in 1.0 0.1 0.01 1e-4 1e-6 1e-8 1e-9 1e-10; do
  L=$(./build/asmm --replay "$LOG" --config config/testnet.toml --gamma "$G" 2>&1 | grep "strategy gamma")
  q=$(echo "$L"  | grep -oE 'quotes=[0-9]+' | cut -d= -f2)
  fl=$(echo "$L" | grep -oE 'fills=[0-9]+' | cut -d= -f2)
  fq=$(echo "$L" | grep -oE 'final_q=-?[0-9]+' | cut -d= -f2)
  mq=$(echo "$L" | grep -oE 'max\|q\|=[0-9]+' | cut -d= -f2)
  mn=$(echo "$L" | grep -oE 'mean_q=-?[0-9.]+' | cut -d= -f2)
  cv=$(echo "$L" | grep -oE 'cross_viol=[0-9]+' | cut -d= -f2)
  echo "| $G | $q | $fl | $fq | $mq | $mn | $cv |"
done
