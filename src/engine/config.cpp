#include "engine/config.hpp"

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>
#include <toml++/toml.hpp>

#include "exchange/signing.hpp"

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
  // Exact host allowlist, not a substring — "testnet.binance.vision.evil.com"
  // and the like must NOT pass. This is the sole barrier before we ever sign.
  if (!IsTestnetOrderHost(HostOf(cfg.rest_url))) {
    throw std::runtime_error("config '" + toml_path +
                             "': rest_url host must be a Binance testnet order host (got '" +
                             HostOf(cfg.rest_url) + "' from '" + cfg.rest_url + "')");
  }

  // [market_data] — all fields optional (defaults in MarketDataConfig).
  MarketDataConfig& md = cfg.market_data;
  const auto mdt = tbl["market_data"];
  md.snapshot_rest_url = mdt["snapshot_rest_url"].value_or(md.snapshot_rest_url);
  md.depth_stream = mdt["depth_stream"].value_or(md.depth_stream);
  md.depth_snapshot_limit = mdt["depth_snapshot_limit"].value_or(md.depth_snapshot_limit);
  md.px_scale = mdt["px_scale"].value_or(md.px_scale);
  md.qty_scale = mdt["qty_scale"].value_or(md.qty_scale);
  md.stale_threshold_ms = mdt["stale_threshold_ms"].value_or(md.stale_threshold_ms);
  md.backoff_initial_ms = mdt["backoff_initial_ms"].value_or(md.backoff_initial_ms);
  md.backoff_max_ms = mdt["backoff_max_ms"].value_or(md.backoff_max_ms);
  md.backoff_multiplier = mdt["backoff_multiplier"].value_or(md.backoff_multiplier);
  md.read_timeout_s = mdt["read_timeout_s"].value_or(md.read_timeout_s);
  md.crosscheck_interval_s = mdt["crosscheck_interval_s"].value_or(md.crosscheck_interval_s);
  md.crosscheck_depth_limit = mdt["crosscheck_depth_limit"].value_or(md.crosscheck_depth_limit);
  md.crosscheck_levels = mdt["crosscheck_levels"].value_or(md.crosscheck_levels);

  // Market data must use the public mirror, never the geo-blocked production host.
  if (md.snapshot_rest_url.find("api.binance.com") != std::string::npos) {
    throw std::runtime_error("config '" + toml_path +
                             "': market_data.snapshot_rest_url must be the data.binance.vision "
                             "mirror, not api.binance.com (HTTP 451 from US networks)");
  }
  if (md.px_scale < 0 || md.qty_scale < 0) {
    throw std::runtime_error("config '" + toml_path + "': market_data px/qty scale must be >= 0");
  }

  // [strategy] — all optional (defaults in StrategyParams).
  const auto st = tbl["strategy"];
  cfg.strategy_enabled = st["enabled"].value_or(false);
  StrategyParams& sp = cfg.strategy;
  sp.gamma = st["gamma"].value_or(sp.gamma);
  sp.kappa = st["kappa"].value_or(sp.kappa);
  sp.tau_days = st["tau_days"].value_or(sp.tau_days);
  sp.sigma_halflife_s = st["sigma_halflife_s"].value_or(sp.sigma_halflife_s);
  sp.sigma_min_samples = st["sigma_min_samples"].value_or(sp.sigma_min_samples);
  sp.sigma_spike_threshold = st["sigma_spike_threshold"].value_or(sp.sigma_spike_threshold);
  sp.hysteresis_ticks = st["hysteresis_ticks"].value_or(sp.hysteresis_ticks);
  sp.q_max_lots = st["q_max_lots"].value_or(sp.q_max_lots);
  sp.quote_size_lots = st["quote_size_lots"].value_or(sp.quote_size_lots);
  sp.maker_fee_bps = st["maker_fee_bps"].value_or(sp.maker_fee_bps);
  sp.min_notional_usdt = st["min_notional_usdt"].value_or(sp.min_notional_usdt);
  sp.px_scale = md.px_scale;  // tick/step scales come from [market_data]
  sp.qty_scale = md.qty_scale;

  // [orders] — all optional (defaults in OrderConfig). Disabled by default.
  const auto ot = tbl["orders"];
  OrderConfig& oc = cfg.orders;
  oc.enabled = ot["enabled"].value_or(oc.enabled);
  oc.recv_window_ms = ot["recv_window_ms"].value_or(oc.recv_window_ms);
  oc.max_order_rate_per_10s = ot["max_order_rate_per_10s"].value_or(oc.max_order_rate_per_10s);
  oc.max_request_weight_per_min =
      ot["max_request_weight_per_min"].value_or(oc.max_request_weight_per_min);
  oc.user_data_ws_url = ot["user_data_ws_url"].value_or(oc.user_data_ws_url);
  oc.reconcile_interval_s = ot["reconcile_interval_s"].value_or(oc.reconcile_interval_s);
  oc.flatten_on_kill = ot["flatten_on_kill"].value_or(oc.flatten_on_kill);
  oc.order_backoff_initial_ms =
      ot["order_backoff_initial_ms"].value_or(oc.order_backoff_initial_ms);
  oc.order_backoff_max_ms = ot["order_backoff_max_ms"].value_or(oc.order_backoff_max_ms);
  oc.client_id_prefix = ot["client_id_prefix"].value_or(oc.client_id_prefix);
  oc.cancel_all_on_kill_timeout_ms =
      ot["cancel_all_on_kill_timeout_ms"].value_or(oc.cancel_all_on_kill_timeout_ms);

  // The user-data stream carries our own account/order updates — it must also be
  // a testnet host (golden rule #1). Validated even when disabled: fail closed.
  if (!IsTestnetOrderHost(HostOf(oc.user_data_ws_url))) {
    throw std::runtime_error(
        "config '" + toml_path +
        "': orders.user_data_ws_url host must be a Binance testnet host (got '" +
        HostOf(oc.user_data_ws_url) + "')");
  }
  if (oc.recv_window_ms <= 0 || oc.recv_window_ms > 60000) {
    throw std::runtime_error("config '" + toml_path +
                             "': orders.recv_window_ms must be in (0, 60000] (got " +
                             std::to_string(oc.recv_window_ms) + ")");
  }

  if (!LoadDotenv(env_path)) {
    spdlog::warn(".env file '{}' not found; API keys unset (fine until Phase 5)", env_path);
  }
  cfg.api_key = GetEnvOr("BINANCE_TESTNET_API_KEY", "");
  cfg.api_secret = GetEnvOr("BINANCE_TESTNET_API_SECRET", "");

  return cfg;
}

}  // namespace asmm
