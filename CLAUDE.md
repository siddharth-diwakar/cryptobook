# CLAUDE.md — Project instructions for Claude Code

## What this project is
A live crypto market maker quoting BTC/USDT on Binance Spot **testnet**, using the
Avellaneda-Stoikov inventory-control model. Goal: run live for 2+ weeks and produce
honest, measured results (fill rate, PnL, adverse selection, tick-to-quote latency)
for a quant-dev resume. This is NOT a backtester-first project — live paper trading
is the deliverable.

## Golden rules
1. **Never touch real money.** All exchange endpoints must point at testnet
   (`testnet.binance.vision`). Refuse to write code that signs orders against
   production REST/WS endpoints. API keys live in `.env` (gitignored), never in code.
2. **Correctness before speed.** The book must be provably correct (sequence-gap
   detection + resync) before any strategy work. A fast wrong book is worthless.
3. **Single-writer principle.** One thread owns the order book and strategy state.
   Network I/O threads communicate with it only via the SPSC queue in
   `src/core/spsc_queue.hpp`. Never add locks around book state.
4. **Every phase ends with its acceptance test passing** (see docs/ROADMAP.md).
   Do not start phase N+1 with phase N's test red.
5. **Measure, don't claim.** Any performance number in README must be reproducible
   by a script in `bench/`.

## Tech stack (do not change without asking)
- C++20, CMake ≥ 3.22, single third-party deps via FetchContent:
  Boost.Beast (WebSocket/HTTP), simdjson (parsing), spdlog (logging),
  Catch2 (tests).
- Python 3.11 + pandas/matplotlib in `analysis/` for offline metrics & plots only.
  No Python in the trading path.
- Linux only. Build: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j`
- Tests: `ctest --test-dir build --output-on-failure`

## Repo layout
```
src/core/      # book, SPSC queue, event types, clock — no I/O allowed here
src/exchange/  # Binance WS/REST adapters, auth, symbol filters
src/strategy/  # Avellaneda-Stoikov quoting, inventory, risk limits
src/engine/    # main loop wiring core+exchange+strategy, event log writer
bench/         # latency & throughput benchmarks (google-benchmark style ok)
tests/         # Catch2 unit + replay tests; fixtures in tests/data/
analysis/      # Python notebooks/scripts reading the event log
docs/          # SPEC, ARCHITECTURE, MODEL, ROADMAP — read these first
```

## Coding conventions
- No heap allocation on the hot path (book update → quote decision → order send).
  Pre-size everything; use the slab pool pattern from docs/ARCHITECTURE.md §4.
- No exceptions on the hot path; use `expected<T, Error>`-style returns.
- All timestamps are `std::chrono::steady_clock` nanos internally; wall-clock
  (`system_clock`) only at the logging boundary.
- Every event (book delta, quote, order ack, fill) is appended to the binary
  event log — the log is the single source of truth for analysis and replay.
- clang-format (file in repo root) before every commit. Warnings are errors
  (`-Wall -Wextra -Werror`).

## Workflow expectations for Claude
- Read `docs/ROADMAP.md` to find the current phase before writing code.
- Work in plan mode first for any task > ~50 lines; show the plan, then implement.
- Write/extend the phase's tests BEFORE or WITH the implementation, never after
  as an afterthought.
- One phase = one or more small commits with descriptive messages
  (`phase2: sequence-gap detection + snapshot resync`).
- If a Binance API detail is uncertain (rate limits, symbol filters, WS payload
  shape), say so and check the official docs rather than guessing payload fields.
- Update `docs/DECISIONS.md` (create if absent) with a 2-line entry whenever we
  make a non-obvious design choice.

## Current status
- [x] Phase 0 — repo scaffold, CI, logging
- [x] Phase 1 — L2 book reconstruction from WS deltas
- [~] Phase 2 — gap detection, snapshot resync, book checksum vs REST (code complete + live smoke-tested; 24h acceptance run pending)
- [x] Phase 3 — event log + replay harness (byte-identical determinism; golden cross-machine test in CI)
- [ ] Phase 4 — Avellaneda-Stoikov quoting (paper, no orders yet)
- [ ] Phase 5 — testnet order placement, fills, inventory tracking
- [ ] Phase 6 — risk limits, kill switch, 2-week live run
- [ ] Phase 7 — analysis, plots, README results section
(Claude: tick these as phases complete.)
