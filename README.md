# AS-MM — A live Avellaneda-Stoikov market maker for BTC/USDT

A C++20 market-making system that reconstructs the Binance L2 order book in real
time, quotes both sides using the Avellaneda-Stoikov inventory-control model, and
trades live on Binance Spot **testnet**. Every event is written to an append-only
binary log, enabling deterministic replay and honest post-hoc analysis.

> **Why this exists:** most student trading projects are backtests, and backtests
> lie (look-ahead, fantasy fills, zero adverse selection). This system runs live.
> The numbers below are measured, including the ugly ones.

## Results (live testnet run: <START_DATE> → <END_DATE>)
<!-- Filled in Phase 7. Keep the honest structure: -->
| Metric | Value |
|---|---|
| Days live / uptime | TBD |
| Quotes placed / fills | TBD |
| Fill rate | TBD |
| Net PnL (incl. fees) | TBD |
| Sharpe (daily) | TBD |
| Adverse selection (bps, 1s markout) | TBD |
| Max inventory / limit | TBD |
| Tick-to-quote latency p50 / p99 | TBD |

**What went wrong and why** — see [docs/POSTMORTEM.md](docs/POSTMORTEM.md)
(written after the run; the most interview-valuable file in this repo).

## Architecture (one paragraph)
A WS reader thread parses Binance depth deltas with simdjson and pushes fixed-size
events into a lock-free SPSC queue. A single engine thread owns all state: it
applies deltas to a pre-allocated L2 book, recomputes Avellaneda-Stoikov quotes,
diffs them against working orders, and emits order commands. An order-gateway
thread signs and sends REST requests. Everything the engine sees or emits is
appended to a binary event log used for replay tests and analysis.
Details: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## The model (one paragraph)
Avellaneda-Stoikov (2008) treats market making as stochastic control: the
reservation price shifts away from mid as inventory grows, and the optimal spread
widens with volatility and risk aversion. Parameters (γ, κ, σ estimation window)
and the exact equations implemented: [docs/MODEL.md](docs/MODEL.md).

## Build & run
```bash
# macOS only: OpenSSL isn't on CMake's default path (auto-hinted from brew if installed)
#   brew install openssl@3
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
ctest --test-dir build --output-on-failure          # unit + replay tests
./build/asmm --version                              # version + git sha
./build/asmm --run --config config/testnet.toml --log  # live book + resync + A-S paper strategy
./build/asmm --replay logs/events-*.bin             # deterministic offline replay (Phase 3)
scripts/gamma_sweep.sh                              # A-S gamma sweep -> docs/GAMMA_SWEEP.md (Phase 4)
python analysis/paper_report.py logs/events-*.bin   # paper-run inventory/PnL/fills report
python analysis/verify_24h.py run.log               # verify a 24h market-data run's acceptance
```
Linux needs `libssl-dev` (CI installs it). Benchmarks: `bench/run_bench.sh`.

## Repo map
| Path | What lives there |
|---|---|
| `src/core/` | book, SPSC queue, event types (no I/O) |
| `src/exchange/` | Binance WS + signed REST adapters |
| `src/strategy/` | A-S quoting, inventory, risk limits, kill switch |
| `src/engine/` | main loop, event-log writer |
| `tests/` | Catch2 unit tests + recorded-session replay tests |
| `bench/` | latency/throughput benchmarks backing every README number |
| `analysis/` | Python: PnL, markouts, fill-rate, plots |
| `docs/` | SPEC · ARCHITECTURE · MODEL · ROADMAP · POSTMORTEM |

## Disclaimers
Testnet only. This is a research/learning system, not financial advice, and the
strategy is expected to lose money in adverse regimes — measuring *how* is the point.
