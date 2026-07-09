# SPEC — Requirements & scope

## Goal
Run a market maker live on Binance Spot testnet for ≥ 14 consecutive days on
BTC/USDT, producing measured metrics suitable for a quant-dev resume and
interview discussion.

## In scope (v1)
- Single symbol (BTC/USDT), single venue (Binance Spot testnet).
- L2 book reconstruction from `depth@100ms` diff stream + REST snapshot,
  with sequence-gap detection and automatic resync.
- Avellaneda-Stoikov quoting: reservation price + optimal half-spread,
  re-quoted on book update or 100ms timer, whichever first.
- Order lifecycle: place/cancel/replace limit orders via signed REST;
  track acks, partial fills, full fills from the user-data WS stream.
- Inventory tracking in base units; realized + unrealized PnL incl. fees.
- Risk controls: max inventory, max open orders, max message rate,
  stale-book guard (no quotes if book older than 500ms), kill switch
  (flatten + cancel-all on signal or breach).
- Append-only binary event log of every input and output event.
- Replay harness: feed a recorded log through the engine and assert
  byte-identical decisions (determinism test).
- Python analysis: PnL curve, daily Sharpe, fill rate, 1s/5s/30s markouts
  (adverse selection), inventory histogram, tick-to-quote latency histogram.

## Out of scope (v1) — do not build these yet
- Multiple symbols or venues; smart order routing.
- Real-money trading of any kind.
- ML signals / alpha models (order-flow imbalance is a stretch goal, Phase 8).
- GUI dashboards (terminal + generated PNG plots only).
- Distributed deployment; one Linux box (or one always-on VPS) is fine.

## Non-functional requirements
- Tick-to-quote decision latency: p99 < 100 µs on the engine thread
  (network latency to Binance excluded — we don't control it and must say so).
- Zero heap allocations on the hot path after warmup (verified in bench).
- Engine is deterministic: same event log in → same decisions out.
- Survives WS disconnects, gaps, and REST rate-limit responses without
  human intervention; logs every recovery.
- 14-day run with < 1% downtime (systemd unit with restart + resync).

## Key definitions (write these once, use everywhere)
- **Mid**: (best_bid + best_ask) / 2 at decision time.
- **Markout (t)**: signed mid move t seconds after our fill, from the fill's
  perspective. Negative average markout = we are being adversely selected.
- **Fill rate**: fills / quotes that rested ≥ 1 tick-time. Define precisely in
  analysis code and keep the definition in the README table footnote.
- **Tick-to-quote latency**: steady_clock delta from "delta event dequeued"
  to "order command enqueued", engine thread only.

## Explicit honesty requirements
- Fees: apply Binance taker/maker fee schedule to PnL even on testnet
  (testnet fills are unrealistically generous; say so in README).
- Report losing days and the regime that caused them in POSTMORTEM.md.
- Never present testnet PnL as if it were production-achievable.
