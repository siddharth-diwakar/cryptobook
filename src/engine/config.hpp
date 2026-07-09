#pragma once

#include <string>

namespace asmm {

// Runtime configuration loaded at startup from a TOML file plus a .env file.
// Startup-only state; it never appears on the hot path.
struct AppConfig {
  // [exchange]
  std::string symbol;         // e.g. "BTCUSDT"
  std::string rest_url;       // order/account REST base — TESTNET ONLY
  std::string ws_market_url;  // market-data WS base

  // [engine]
  std::string log_dir = "logs";

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
