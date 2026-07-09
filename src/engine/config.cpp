#include "engine/config.hpp"

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>
#include <toml++/toml.hpp>

namespace asmm {
namespace {

std::string Trim(const std::string& s) {
  const auto begin = s.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) return "";
  const auto end = s.find_last_not_of(" \t\r\n");
  return s.substr(begin, end - begin + 1);
}

std::string GetEnvOr(const char* key, const std::string& fallback) {
  const char* v = std::getenv(key);
  return v ? std::string(v) : fallback;
}

}  // namespace

bool LoadDotenv(const std::string& env_path) {
  std::ifstream in(env_path);
  if (!in) return false;

  std::string line;
  int lineno = 0;
  while (std::getline(in, line)) {
    ++lineno;
    const std::string trimmed = Trim(line);
    if (trimmed.empty() || trimmed[0] == '#') continue;

    const auto eq = trimmed.find('=');
    if (eq == std::string::npos) {
      throw std::runtime_error(env_path + ":" + std::to_string(lineno) +
                               ": malformed line (expected KEY=VALUE)");
    }
    const std::string key = Trim(trimmed.substr(0, eq));
    const std::string val = Trim(trimmed.substr(eq + 1));
    if (key.empty()) {
      throw std::runtime_error(env_path + ":" + std::to_string(lineno) + ": empty key");
    }
    // overwrite=0: real environment variables take precedence over the file.
    ::setenv(key.c_str(), val.c_str(), 0);
  }
  return true;
}

AppConfig LoadConfig(const std::string& toml_path, const std::string& env_path) {
  toml::table tbl;
  try {
    tbl = toml::parse_file(toml_path);
  } catch (const toml::parse_error& e) {
    throw std::runtime_error("failed to parse config '" + toml_path +
                             "': " + std::string(e.description()));
  }

  AppConfig cfg;
  cfg.symbol = tbl["exchange"]["symbol"].value_or(std::string{});
  cfg.rest_url = tbl["exchange"]["rest_url"].value_or(std::string{});
  cfg.ws_market_url = tbl["exchange"]["ws_market_url"].value_or(std::string{});
  cfg.log_dir = tbl["engine"]["log_dir"].value_or(std::string{"logs"});

  if (cfg.symbol.empty()) {
    throw std::runtime_error("config '" + toml_path + "': [exchange].symbol is required");
  }
  if (cfg.rest_url.empty()) {
    throw std::runtime_error("config '" + toml_path + "': [exchange].rest_url is required");
  }
  // Golden rule #1: the order/account endpoint must never point at production.
  if (cfg.rest_url.find("testnet") == std::string::npos) {
    throw std::runtime_error("config '" + toml_path +
                             "': rest_url must be a testnet endpoint (got '" + cfg.rest_url + "')");
  }

  if (!LoadDotenv(env_path)) {
    spdlog::warn(".env file '{}' not found; API keys unset (fine until Phase 5)", env_path);
  }
  cfg.api_key = GetEnvOr("BINANCE_TESTNET_API_KEY", "");
  cfg.api_secret = GetEnvOr("BINANCE_TESTNET_API_SECRET", "");

  return cfg;
}

}  // namespace asmm
