# ARCHITECTURE

## 1. Thread model (three threads, one owner)
```
[WS market-data thread]  --SPSC-->  [ENGINE thread]  --SPSC-->  [Order gateway thread]
[WS user-data thread]    --SPSC-->       |
                                          v
                                   [event log file]
```
- **WS market-data thread**: Boost.Beast client on `btcusdt@depth@100ms`
  (+ `@trade` for σ estimation). Parses with simdjson into a fixed-size
  `MarketEvent`, pushes to SPSC queue A. Does nothing else.
- **WS user-data thread**: listens to the user-data stream (order updates,
  fills, balance). Same pattern, queue B.
- **ENGINE thread** (single writer): drains queues, applies deltas to the book,
  runs strategy, emits `OrderCommand`s to queue C, appends every in/out event
  to the log. Owns ALL mutable trading state. No locks anywhere in this thread.
- **Order gateway thread**: drains queue C, HMAC-signs and POSTs to testnet REST,
  handles 429/418 rate-limit backoff, pushes acks back via queue B path.

Rationale: this mirrors real exchange/trading-system design and lets the engine
be deterministic and replayable. It is also the interview story.

## 2. Event types (fixed-size PODs, no strings on hot path)
```cpp
struct MarketEvent { u64 seq; i64 ts_ns; EventKind kind; /* price/qty arrays for deltas */ };
struct OrderCommand { i64 ts_ns; CmdKind kind; Side side; i64 px_ticks; i64 qty_lots; u64 client_id; };
struct ExecEvent   { i64 ts_ns; u64 client_id; ExecKind kind; i64 px_ticks; i64 qty_lots; };
```
Prices and quantities are integers (ticks/lots) everywhere inside the engine.
Convert to/from decimal strings only at the exchange boundary, using the symbol's
tickSize/stepSize filters fetched at startup.

## 3. L2 book
- Two fixed-size arrays (bids, asks) of (px_ticks, qty_lots), sorted, pre-sized
  to N=5000 levels; updates via binary search + memmove (fast at this N and
  branch-predictable). This is deliberately simpler than the order-level book
  in the companion matching-engine project — L2 is what Binance gives us.
- Binance reconstruction protocol (implement exactly):
  1. Open WS stream, buffer events.
  2. GET `/api/v3/depth?limit=5000` snapshot; note `lastUpdateId`.
  3. Drop buffered events with `u <= lastUpdateId`.
  4. First applied event must satisfy `U <= lastUpdateId+1 <= u`.
  5. Thereafter require `U == prev_u + 1`; any gap ⇒ RESYNC state:
     stop quoting, cancel-all, rebuild from new snapshot, resume.
- Book exposes: best bid/ask, mid, microprice, depth-at-N-levels, last update ts.

## 4. Memory discipline
- All queues are pre-allocated ring buffers (power-of-two capacity, cached
  head/tail indices, single producer single consumer).
- A slab pool provides any transient objects; assert-in-debug that the global
  allocator is not hit on the hot path (override operator new in a test build
  and count).

## 5. Event log
- Binary, append-only, one file per session: `logs/events-YYYYMMDD-HHMMSS.bin`.
- Record = 8-byte length + type tag + POD payload, written by the engine thread
  via a large buffered writer flushed on timer (fsync every 5s; this is a
  research system, not a bank).
- The log contains BOTH inputs (market/exec events) and outputs (commands),
  so replay can verify decisions byte-for-byte. This is the determinism test
  and the only data source the Python analysis reads.

## 6. Strategy tick
On each drained batch (or 100ms timer):
1. Update σ estimate (EWMA of mid log-returns from trade stream).
2. Compute reservation price r and half-spread δ (docs/MODEL.md).
3. Clamp to risk limits; round to tick; skip if unchanged from working orders
   beyond a hysteresis threshold (avoid churn / rate limits).
4. Emit cancel/replace commands. Log everything.

## 7. Failure handling
- WS disconnect ⇒ RESYNC state (see §3) with exponential backoff reconnect.
- REST 429/418 ⇒ token-bucket throttle honoring `Retry-After`; drop to
  quote-widening mode if we can't keep up.
- Stale book (> 500ms without update) ⇒ pull quotes.
- SIGTERM/SIGINT or risk breach ⇒ kill switch: cancel-all, market-flatten
  inventory (bounded slippage), flush log, exit 0.

## 8. What we deliberately did NOT do (interview ammunition)
- No kernel bypass / io_uring / busy-poll pinning: network RTT to Binance
  (~10-50ms) dwarfs micro-optimizations; we optimized the part we control
  and measured it (bench/). Know this trade-off cold for interviews.
- No lock-free MPMC: three SPSC queues are simpler and sufficient by design.
