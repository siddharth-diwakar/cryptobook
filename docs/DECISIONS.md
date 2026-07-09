# DECISIONS — non-obvious design choices

Two-line entries per CLAUDE.md workflow rule. Newest first.

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
