#!/usr/bin/env python3
"""Record Binance BTCUSDT L2 depth for the Phase 1 replay fixture.

Streams `btcusdt@depth@100ms` from Binance PRODUCTION public market data
(read-only; no keys; golden rule #1 concerns ORDERS only, and ROADMAP Phase 2
explicitly allows production market data). Writes:

  tests/data/depth_btcusdt.jsonl          raw WS frames, one JSON object per line
  tests/data/depth_btcusdt.snapshot.json  the aligned REST depth snapshot
  tests/data/depth_btcusdt.meta.json      scales + snapshot_last_update_id + stats

Crucially it SELF-VALIDATES before writing: it replays its own frames through a
dict book using the exact cut rule the C++ test uses (tests/test_book_replay.cpp)
and only finalizes once the reconstructed best bid/ask (price AND qty) equals the
snapshot. It retries with fresh snapshots to absorb the sub-100ms alignment
window, so the committed fixture is guaranteed to pass CI deterministically.

Usage:
    pip install -r analysis/requirements.txt
    python analysis/record_depth.py --minutes 30
"""
from __future__ import annotations

import argparse
import asyncio
import json
import os
import time
from decimal import Decimal

import requests
import websockets

SYMBOL = "BTCUSDT"
# Public market-data mirror (data.binance.vision). Not geo-restricted (production
# api.binance.com returns HTTP 451 from US networks), read-only, no keys — well
# within golden rule #1, which concerns ORDERS only.
WS_URL = "wss://data-stream.binance.vision/ws/btcusdt@depth@100ms"
REST = "https://data-api.binance.vision"
DATA_DIR = os.path.join(os.path.dirname(__file__), "..", "tests", "data")


def scale_of(size_str: str) -> int:
    """Decimal places implied by a tick/step size, e.g. '0.01000000' -> 2."""
    d = Decimal(size_str).normalize()
    exp = -d.as_tuple().exponent
    return max(exp, 0)


def to_scaled(s: str, scale: int) -> int:
    """Exact decimal string -> integer * 10^scale (mirrors C++ DecimalToScaled)."""
    return int((Decimal(s) * (10 ** scale)).to_integral_value())


def get_filters() -> tuple[int, int, str, str]:
    info = requests.get(
        f"{REST}/api/v3/exchangeInfo", params={"symbol": SYMBOL}, timeout=10
    ).json()
    filters = info["symbols"][0]["filters"]
    tick = next(f["tickSize"] for f in filters if f["filterType"] == "PRICE_FILTER")
    step = next(f["stepSize"] for f in filters if f["filterType"] == "LOT_SIZE")
    return scale_of(tick), scale_of(step), tick, step


def get_snapshot() -> dict:
    return requests.get(
        f"{REST}/api/v3/depth", params={"symbol": SYMBOL, "limit": 5000}, timeout=10
    ).json()


def reconstruct_top(frames: list[dict], snap_id: int, px_scale: int, qty_scale: int):
    """Replay frames through a dict book using the C++ cut rule; return (best_bid,
    best_ask) as (px_ticks, qty_lots), or None if the cut couldn't complete."""
    bids: dict[int, int] = {}
    asks: dict[int, int] = {}
    applied_straddle = False
    for fr in frames:
        U, u = fr["U"], fr["u"]
        if u <= snap_id:
            pass  # fully before the snapshot; apply
        elif U <= snap_id + 1 <= u:
            applied_straddle = True  # straddles the snapshot; apply then stop
        else:
            break  # past the snapshot
        for px_s, qty_s in fr["b"]:
            px, qty = to_scaled(px_s, px_scale), to_scaled(qty_s, qty_scale)
            if qty == 0:
                bids.pop(px, None)
            else:
                bids[px] = qty
        for px_s, qty_s in fr["a"]:
            px, qty = to_scaled(px_s, px_scale), to_scaled(qty_s, qty_scale)
            if qty == 0:
                asks.pop(px, None)
            else:
                asks[px] = qty
        if applied_straddle:
            break
    if not bids or not asks:
        return None
    bb = max(bids)
    ba = min(asks)
    return (bb, bids[bb]), (ba, asks[ba])


def snapshot_top(snap: dict, px_scale: int, qty_scale: int):
    bids = {to_scaled(p, px_scale): to_scaled(q, qty_scale) for p, q in snap["bids"]}
    asks = {to_scaled(p, px_scale): to_scaled(q, qty_scale) for p, q in snap["asks"]}
    bb = max(bids)
    ba = min(asks)
    return (bb, bids[bb]), (ba, asks[ba])


async def record(minutes: float) -> None:
    px_scale, qty_scale, tick, step = get_filters()
    print(f"filters: tick={tick} (px_scale={px_scale}) step={step} (qty_scale={qty_scale})")

    frames: list[dict] = []
    deadline = time.time() + minutes * 60.0

    async with websockets.connect(WS_URL, max_size=None, ping_interval=180) as ws:
        print(f"recording {minutes} min from {WS_URL} ...")
        # Main recording window.
        while time.time() < deadline:
            msg = await ws.recv()
            fr = json.loads(msg)
            if fr.get("e") == "depthUpdate":
                frames.append(fr)
        print(f"recorded {len(frames)} events; aligning snapshot ...")

        # Alignment loop: fetch a snapshot, ensure we have a frame past it, and
        # verify the exact reconstruction matches. Retry to absorb the window.
        for attempt in range(1, 21):
            snap = get_snapshot()
            snap_id = snap["lastUpdateId"]
            # Keep recording until an event with u >= snap_id exists.
            while not frames or frames[-1]["u"] < snap_id:
                msg = await ws.recv()
                fr = json.loads(msg)
                if fr.get("e") == "depthUpdate":
                    frames.append(fr)

            recon = reconstruct_top(frames, snap_id, px_scale, qty_scale)
            snap_top = snapshot_top(snap, px_scale, qty_scale)
            if recon is not None and recon == snap_top:
                print(f"aligned on attempt {attempt}: top={recon}")
                _write(frames, snap, px_scale, qty_scale, tick, step, snap_id)
                return
            print(f"attempt {attempt}: mismatch recon={recon} snap={snap_top}; retrying")
            await asyncio.sleep(1.0)

    raise SystemExit("could not align snapshot after 20 attempts; re-run (market too volatile)")


def _write(frames, snap, px_scale, qty_scale, tick, step, snap_id) -> None:
    os.makedirs(DATA_DIR, exist_ok=True)
    jsonl = os.path.join(DATA_DIR, "depth_btcusdt.jsonl")
    with open(jsonl, "w") as f:
        for fr in frames:
            f.write(json.dumps(fr, separators=(",", ":")) + "\n")
    with open(os.path.join(DATA_DIR, "depth_btcusdt.snapshot.json"), "w") as f:
        json.dump(snap, f, separators=(",", ":"))
    meta = {
        "symbol": SYMBOL,
        "tick_size": tick,
        "step_size": step,
        "px_scale": px_scale,
        "qty_scale": qty_scale,
        "ws_url": WS_URL,
        "recorded_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "num_events": len(frames),
        "first_U": frames[0]["U"],
        "last_u": frames[-1]["u"],
        "snapshot_last_update_id": snap_id,
    }
    with open(os.path.join(DATA_DIR, "depth_btcusdt.meta.json"), "w") as f:
        json.dump(meta, f, indent=2)
    print(f"wrote {len(frames)} events to {jsonl}")
    print("next: compress for commit ->")
    print("  cd tests/data && tar czf depth_btcusdt.tar.gz depth_btcusdt.jsonl "
          "depth_btcusdt.snapshot.json depth_btcusdt.meta.json")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--minutes", type=float, default=30.0)
    args = ap.parse_args()
    asyncio.run(record(args.minutes))
