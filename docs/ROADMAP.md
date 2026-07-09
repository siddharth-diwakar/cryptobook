# ROADMAP — Phases, each with a hard acceptance test

Rule: a phase is DONE only when its acceptance criteria pass and are committed.
Claude: check the current phase in CLAUDE.md before writing any code.

## Phase 0 — Scaffold (0.5 week)
Build: CMake project, deps via FetchContent, clang-format, spdlog wired,
GitHub Actions CI (build + ctest on push), `.env` loading, config TOML.
**Accept:** CI green on a hello-world Catch2 test; `--version` binary runs.

## Phase 1 — Book from recorded data (1 week)
Build: `MarketEvent` types, SPSC queue, L2 book apply logic.
Record 30 min of real Binance WS depth JSON to `tests/data/` with a tiny
Python script (this is allowed off hot-path tooling).
**Accept:** replaying the recording reproduces best bid/ask matching a REST
snapshot taken at the end of the recording; SPSC queue passes a 2-thread
stress test (1e8 messages, no loss/reorder); book update p99 < 5 µs in bench.

## Phase 2 — Live book + resync (0.5 week)
Build: Beast WS client, snapshot bootstrap per Binance protocol
(ARCHITECTURE §3), gap ⇒ RESYNC state machine, stale-book detection.
**Accept:** runs 24h against production *market data* (public, read-only —
allowed; only ORDERS are testnet-only) with zero undetected gaps; every
resync logged; periodic REST cross-check diff = 0.

## Phase 3 — Event log + deterministic replay (0.5 week)
Build: binary log writer, reader, replay harness that feeds a log through the
engine and captures decisions.
**Accept:** same log in ⇒ byte-identical decision stream out, on two runs and
on a different machine. This test stays in CI forever.

## Phase 4 — Strategy on paper (1 week)
Build: σ estimator, A-S quoting (MODEL.md), hysteresis, simulated fills against
our own book (fill when book crosses our quote) — paper mode, no orders sent.
**Accept:** model unit tests pass; 48h paper run produces sane behavior
(inventory mean-reverts around 0; quotes never cross; γ sweep documented).

## Phase 5 — Testnet orders (1 week)
Build: signed REST client (HMAC), user-data stream, order state machine
(NEW→ACK→PARTIAL→FILLED/CANCELED), inventory & PnL tracking with fees,
rate-limit token bucket.
**Accept:** 24h live testnet run: zero stuck orders, order-state machine never
desyncs from exchange (reconcile on reconnect), cancel-all works under kill.

## Phase 6 — Risk + the 14-day run (2+ weeks calendar, low effort)
Build: inventory limits, kill switch, systemd unit (auto-restart + resync),
daily log rotation, cheap VPS or always-on box deployment.
**Accept:** ≥ 14 days, < 1% downtime, all recoveries automatic. While it runs,
work on Phase 7 tooling and interview prep — don't babysit it.

## Phase 7 — Analysis + writeup (1 week, overlaps 6)
Build: `analysis/report.py`: PnL curve (fees included), daily Sharpe, fill rate,
markouts at 1s/5s/30s, inventory histogram, tick-to-quote latency histogram
(from log timestamps). Write POSTMORTEM.md and fill the README results table.
**Accept:** README table fully populated from one script run; POSTMORTEM
contains ≥ 3 specific failures/losses and their causes.

## Phase 8 — Stretch: OFI skew (only if time before apps)
Order-flow-imbalance signal shading the reservation price; A/B two weeks vs
baseline markouts.
**Accept:** measured markout delta reported with the A/B methodology stated.

## Timeline sanity (target: resume-ready before late-August apps)
Weeks 1–4: Phases 0–5. Weeks 5–6: Phase 6 running while doing 7.
If behind schedule: CUT Phase 8, never cut Phase 3 (determinism) or honest
fee accounting — those are the credibility of the whole project.
