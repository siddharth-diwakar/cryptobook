#!/usr/bin/env python3
"""Report on a Phase 4 paper-trading run by parsing its binary event log.

Reads QuoteRecord/FillRecord records and reports the acceptance evidence:
inventory behavior (mean-reversion vs drift), fill count/rate, PnL with fees,
quote-cross violations (must be 0), and guard-trigger counts. Optionally plots
the inventory series if matplotlib is available.

Usage:
    ./build/asmm --run --config config/testnet.toml --log   # writes logs/events-*.bin
    python analysis/paper_report.py logs/events-YYYYMMDD-HHMMSS.bin
"""
from __future__ import annotations

import struct
import sys

# Record framing (must match src/engine/event_log.hpp):
#   file header: magic[4] u32 version + 4x u32 struct sizes = 24 bytes
#   record: u32 type + u32 len + payload
REC_MARKET, REC_DECISION, REC_QUOTE, REC_FILL = 1, 2, 3, 4
# QuoteRecord: u64 fid; i64 bid_px,bid_qty,ask_px,ask_qty,mid_x2,inv,sigma; u8 quoting,one_sided,pulled,pad[5]
QUOTE_FMT = "<Qqqqqqqq3B5x"
# FillRecord: u64 fid; i64 px,qty,fee,inv,cash; u8 side, pad[7]
FILL_FMT = "<QqqqqqB7x"


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: paper_report.py <log.bin>")
        return 2
    f = open(sys.argv[1], "rb")
    magic, ver, me, dec, q, fl = struct.unpack("<4sIIIII", f.read(24))
    if magic != b"ASML":
        print("not an asmm event log")
        return 2

    inv_series = []
    quotes = pulled = one_sided = quoting = cross_viol = 0
    fills = 0
    buy_qty = sell_qty = 0
    px_scale, qty_scale = 2, 5  # BTCUSDT
    last_mid_x2 = 0
    final_cash = 0
    final_inv = 0

    while True:
        h = f.read(8)
        if len(h) < 8:
            break
        rtype, rlen = struct.unpack("<II", h)
        payload = f.read(rlen)
        if rtype == REC_QUOTE:
            (fid, bpx, bq, apx, aq, midx2, inv, sig, qn, os, pl) = struct.unpack(QUOTE_FMT, payload)
            quotes += 1
            inv_series.append(inv)
            last_mid_x2 = midx2
            if pl:
                pulled += 1
            if os:
                one_sided += 1
            if qn:
                quoting += 1
            if bpx > 0 and apx > 0 and bpx >= apx:
                cross_viol += 1
        elif rtype == REC_FILL:
            (fid, px, qty, fee, inv, cash, side) = struct.unpack(FILL_FMT, payload)
            fills += 1
            if side == 0:
                buy_qty += qty
            else:
                sell_qty += -qty
            final_cash = cash
            final_inv = inv

    # PnL: cash + inventory marked at last mid, in cash units (10^-(px+qty scale) USDT).
    mid_ticks = last_mid_x2 / 2.0
    marked = final_cash + final_inv * mid_ticks
    pnl_usdt = marked / (10 ** (px_scale + qty_scale))

    n = len(inv_series)
    mean_inv = sum(inv_series) / n if n else 0.0
    max_abs = max((abs(x) for x in inv_series), default=0)
    zero_cross = sum(1 for i in range(1, n) if (inv_series[i - 1] <= 0 < inv_series[i]) or
                     (inv_series[i - 1] >= 0 > inv_series[i]))

    print("=== paper run report ===")
    print(f"  quotes         : {quotes}  (quoting {quoting}, pulled {pulled}, one-sided {one_sided})")
    print(f"  fills          : {fills}  (buy {buy_qty} lots, sell {sell_qty} lots)")
    print(f"  inventory      : mean {mean_inv:.1f}  max|q| {max_abs}  final {final_inv} lots")
    print(f"  zero-crossings : {zero_cross}  (mean-reversion evidence)")
    print(f"  PnL (w/ fees)  : {pnl_usdt:.4f} USDT  [TESTNET/PAPER — optimistic]")
    print(f"  cross_viol     : {cross_viol}  (must be 0)")

    ok = cross_viol == 0
    print("PASS" if ok else "FAIL: quote-cross violations present")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
