#pragma once

#include <string>

#include "core/types.hpp"

namespace asmm {

// [market_data] — live book / resync parameters (Phase 2).
struct MarketDataConfig {
  std::string snapshot_rest_url =
      "https://data-api.binance.vision";  // public mirror, NEVER api.binance.com
  std::string depth_stream = "depth@100ms";
  int depth_snapshot_limit = 5000;
  int px_scale = 2;   // BTCUSDT tickSize 0.01
  int qty_scale = 5;  // stepSize 0.00001
  i64 stale_threshold_ms = 500;
  i64 backoff_initial_ms = 500;
  i64 backoff_max_ms = 30000;
  double backoff_multiplier = 2.0;
  int read_timeout_s = 10;
  int crosscheck_interval_s = 60;
  int crosscheck_depth_limit = 100;
  int crosscheck_levels = 20;
};

// Runtime configuration loaded at startup from a TOML file plus a .env file.
// Startup-only state; it never appears on the hot path.
struct AppConfig {
  // [exchange]
  std::string symbol;         // e.g. "BTCUSDT"
  std::string rest_url;       // order/account REST base — TESTNET ONLY
  std::string ws_market_url;  // market-data WS base

  // [engine]
  std::string log_dir = "logs";

  // [market_data]
  MarketDataConfig market_data;

  // Secrets from .env (never logged). Empty until Phase 5 needs them.
  std::string api_key;
  std::string api_secret;
};

// Parses a KEY=VALUE dotenv file, applying entries to the process environment
// WITHOUT overwriting existing variables (real env vars take precedence, which
// plays well with systemd Environment= in Phase 6). Throws on a malformed line.
// Returns false if the file does not exist (caller decides if that is fatal).
bool LoadDotenv(const std::string& env_path);

// Loads the TOML config, then overlays .env secrets. Throws std::runtime_error
// with context on a missing/malformed config or a non-testnet rest_url (golden
// rule #1). A missing .env is only a warning (no keys are needed before Phase 5).
AppConfig LoadConfig(const std::string& toml_path, const std::string& env_path);

}  // namespace asmm
