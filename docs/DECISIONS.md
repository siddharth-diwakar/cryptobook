# DECISIONS — non-obvious design choices

Two-line entries per CLAUDE.md workflow rule. Newest first.

## Phase 3

- **DecisionRecord is integer-only (no microprice/doubles).** Floating point is not
  guaranteed bit-identical across compilers/machines, so the byte-compared decision
  stream is pure fixed-width integers (px/qty ticks, mid_x2). Microprice (double)
  is deliberately excluded from the determinism stream. When orders arrive (Phase 5)
  OrderCommands are integer too, so the stream stays deterministic.

- **Replay is single-threaded and clock-free.** Decisions derive ONLY from book
  state + recorded input, never NowNs(). ts_recv_ns lives in the recorded input
  (fixed in the file) but never enters a decision, so replay is wall-clock
  independent. The live Engine's stale detection (which uses NowNs) is not part of
  the decision stream.

- **Cross-machine acceptance = committed golden.** A golden decision stream
  (tests/data/replay_golden.dat) generated on arm64 dev is byte-compared by the CI
  test on x86_64. Green CI proves two architectures produce identical bytes — the
  "different machine" acceptance, in CI forever. (All targets are little-endian.)

- **Log record framing: 8-byte header (u32 type + u32 len) + raw POD payload**, with
  a file header (magic "ASML" + version + struct sizes) the reader validates to
  reject incompatible builds. Raw-POD writes are safe: fixed-layout, static_assert'd
  sizeof, little-endian only.

- **Log captures both inputs (MarketEvent) and decisions (DecisionRecord).** Replay
  only needs the inputs (decisions are reproducible), but logging both lets a run be
  audited live-vs-replay. Writer is buffered (1 MiB) + flushed every 5s; the hot
  path does memcpy-into-buffer only.

## Phase 2

- **OpenSSL is a SYSTEM dependency (find_package), not FetchContent.** Building
  OpenSSL from source is a Perl-based nightmare that doesn't integrate with
  FetchContent and is a security liability to self-pin. CI installs libssl-dev;
  macOS dev needs `brew install openssl@3` (CMake auto-hints OPENSSL_ROOT_DIR from
  brew). This is the one documented exception to the FetchContent-only convention.

- **Beast networking is hidden behind factory functions** (MakeBeastWsClient /
  MakeBeastRestClient returning IWsTransport/IRestClient). Consumers and their
  -Werror builds never see Boost/OpenSSL headers; the driver/engine test against
  in-memory fakes with zero network. Boost headers are marked SYSTEM in asmm_net.

- **Synchronous Beast reads + SO_RCVTIMEO** for dead-connection detection (not
  async io_context). depth@100ms guarantees >=10 msg/s, so any read timeout means
  a dead connection -> full reconnect. Far simpler than async; the MD thread does
  nothing else. Async is the documented fallback if false reconnects appear.

- **Snapshots flow through the SAME queue as diffs** as fragmented kSnapshot
  events (first flagged kFlagSnapshotStart -> engine Clear()s the book). Ordering
  is inherent and the engine stays the single writer.

- **Two independent gap checkers:** the MD-thread Reconstructor (authoritative,
  resyncs on gap) and the engine-side GapVerifier (redundant). The engine should
  never see a raw gap; undetected_gaps==0 over 24h is the acceptance proof.

- **Cross-check via ring-replay, on a separate thread.** A cross-check thread does
  the periodic REST fetch (immune to WS-thread stalls); the engine seeds a scratch
  book from the snapshot and replays its recent-event ring to align sequence ids
  before an exact top-N integer comparison (diff=0 is exact equality).

- **~200 lines of Beast/OpenSSL glue (ws_client/rest_client) are NOT CI-tested** —
  they need real network. Covered by a manual live smoke run (asmm --run) and the
  24h acceptance run. Everything else (protocol, gap detection, cross-check,
  backoff, engine) is CI-tested via fakes + the recorded fixture.

## Phase 1

- **Book bounded to top-1024 levels/side (was 5000).** An A-S maker only quotes
  near top-of-book, so tracking full depth is wasteful. Bounding caps the
  insert/delete/evict memmove. Discovered when the honest 30-min fixture bench
  showed p99 ~6.3us on a full 5000-level book. Top-1024 (±~$10 on BTC) is far more
  than an MM needs; best bid/ask are never evicted so all top-of-book acceptance
  tests are unaffected.

- **"Book update" acceptance metric = single-level Apply (p99 250 ns), not whole
  message.** A Binance depth message applies many level updates; timing the whole
  message (p99 ~7us) sums dozens of updates. The 5us target is the per-level
  primitive. The bench reports both numbers transparently.

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

- **Recorder uses the data.binance.vision public market-data mirror**
  (data-api / data-stream), NOT api.binance.com. Production api.binance.com
  returns HTTP 451 (geo-blocked) from US networks; the .vision mirror serves the
  same market data globally, read-only, no keys. ROADMAP Phase 2 allows production
  market data; golden rule #1 (orders = testnet) is intact. Phase 2's live book
  must use the same mirror for market data (orders still go to testnet REST).

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
