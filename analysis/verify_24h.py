#!/usr/bin/env python3
"""Verify a Phase 2 live run log meets the acceptance criteria.

Parses the status/shutdown lines emitted by `asmm --run` and asserts:
  - undetected_gaps == 0   (the engine's redundant verifier never fired)
  - xcheck_fail == 0       (every REST cross-check that ran matched)
  - run span >= 24h        (warns, does not fail, if shorter)
Prints a summary and exits non-zero on any violation (self-checking, like
bench/run_bench.sh).

Usage:
    ./build/asmm --run --config config/testnet.toml > run.log 2>&1
    python analysis/verify_24h.py run.log
"""
from __future__ import annotations

import argparse
import re
import sys
from datetime import datetime

TS = re.compile(r"\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})")
KV = re.compile(r"(\w+)=(-?\d+)")


def parse(path: str):
    first_ts = last_ts = None
    last_counters: dict[str, int] = {}
    resync_logs = 0
    for line in open(path):
        m = TS.search(line)
        if m:
            ts = datetime.strptime(m.group(1), "%Y-%m-%d %H:%M:%S")
            first_ts = first_ts or ts
            last_ts = ts
        if "resyncing" in line or "reconnect" in line.lower():
            resync_logs += 1
        if "status " in line or "shutdown:" in line:
            for k, v in KV.findall(line):
                last_counters[k] = int(v)
    return first_ts, last_ts, last_counters, resync_logs


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("logfile")
    args = ap.parse_args()

    first_ts, last_ts, c, resync_logs = parse(args.logfile)
    if not first_ts or not c:
        print("no status/shutdown lines found — is this an asmm run log?")
        return 2

    span_h = (last_ts - first_ts).total_seconds() / 3600.0
    undetected = c.get("undetected_gaps", -1)
    xfail = c.get("xcheck_fail", -1)

    print("=== 24h run verification ===")
    print(f"  span            : {span_h:.2f} h")
    print(f"  events applied  : {c.get('applied', '?')}")
    print(f"  snapshots       : {c.get('snapshots', c.get('snaps', '?'))}")
    print(f"  gaps detected   : {c.get('gaps', '?')}  (resyncs: {c.get('resyncs', '?')})")
    print(f"  ws reconnects   : {c.get('reconnects', '?')}")
    print(f"  crosscheck ok   : {c.get('xcheck_ok', '?')}  fail: {xfail}")
    print(f"  undetected gaps : {undetected}")

    ok = True
    if undetected != 0:
        print(f"FAIL: undetected_gaps = {undetected} (must be 0)")
        ok = False
    if xfail != 0:
        print(f"FAIL: xcheck_fail = {xfail} (must be 0)")
        ok = False
    if span_h < 24.0:
        print(f"WARN: run span {span_h:.2f} h < 24 h (acceptance needs >= 24 h)")

    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
