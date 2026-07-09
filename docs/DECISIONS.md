# DECISIONS — non-obvious design choices

Two-line entries per CLAUDE.md workflow rule. Newest first.

## Phase 1

- **L2 book stored worst-price-first** (bids ascending, asks descending; best is
  the last active element). Top-of-book churn then memmoves a near-zero tail; a
  best-first layout would memmove the whole ~80 KB tail per top insert (~3-8 us)
  and blow the p99 < 5 us target. Best bid/ask stay O(1).

- **Per-event level cap = 64**, shared bids+asks. Diffs exceeding it fragment into
  chained MarketEvents sharing U/u/ts, all but the last flagged kFlagContinuation
  — keeps the POD fixed-size (1080 B) without ever dropping levels.

- **Queries return std::optional / ApplyResult enum, not std::expected.**
  std::expected is C++23 and the dep list is closed; optional+enum gives the same
  no-exceptions-on-hot-path property with zero new machinery.

- **simdjson lives only in src/exchange/ and tests, never src/core/.** Preserves
  core's no-I/O rule; the replay test exercises the same parser Phase 2 uses live.

- **Hand-rolled percentile bench, no google-benchmark** (approved). Dep list stays
  closed; google-benchmark doesn't report p99 natively anyway. bench/run_bench.sh
  exits non-zero if p99 >= 5 us, making it a self-checking acceptance script.

- **Fixture committed as tests/data/depth_btcusdt.tar.gz**, extracted at CMake
  configure time (file(ARCHIVE_EXTRACT)); raw jsonl would be 20-60 MB in history.

- **Recorder uses production public market data** (stream.binance.com /
  api.binance.com, read-only, no keys). ROADMAP Phase 2 allows it; testnet depth
  is too thin to be a meaningful fixture. Golden rule #1 (orders = testnet) intact.

## Phase 0

- **toml++ added as a config dependency.** CLAUDE.md's dep list is closed, so
  this was an explicit ask (approved). Rationale: header-only, TOML 1.0-compliant,
  zero build cost, startup-only (never hot path); hand-rolled TOML risks silent
  config bugs, which "correctness before speed" forbids.

- **Boost fetched now (Phase 0), scoped to beast/asio/system.** User chose
  "FetchContent all". Scoped via `BOOST_INCLUDE_LIBRARIES` so we don't build all
  of Boost; a unit test includes `<boost/beast/version.hpp>` so the wiring is
  actually exercised even though no Beast code exists until Phase 2.

- **Config loader lives in `src/engine/`, not `src/core/`.** `src/core` is
  declared I/O-free (CLAUDE.md); config loading is file I/O, and `src/engine` is
  the "wiring" layer, so it belongs there.

- **`.env` precedence: real environment overrides file.** `LoadDotenv` uses
  `setenv(..., overwrite=0)` so process env vars win over `.env` entries — plays
  well with systemd `Environment=` in Phase 6.

- **Version git SHA is captured at CMake configure time**, not build time, so it
  can go stale between commits without reconfiguring. Acceptable for a research
  tool; the version string is informational only.

- **Warning flags applied to first-party targets only** via an INTERFACE target
  (`asmm_warnings`), never to FetchContent deps, so upstream warnings can't fail
  our `-Werror` build.

- **Dev on macOS, CI on Linux is the source of truth.** CLAUDE.md says Linux-only;
  local macOS builds are for convenience. Any Linux-specific issue must be caught
  by ubuntu CI, not assumed from a green local build.
