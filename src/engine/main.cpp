#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "core/clock.hpp"
#include "engine/config.hpp"
#include "engine/engine.hpp"
#include "engine/event_log.hpp"
#include "engine/market_data_thread.hpp"
#include "engine/replay.hpp"
#include "exchange/rest_client.hpp"
#include "exchange/signed_rest_client.hpp"
#include "exchange/signing.hpp"
#include "exchange/ws_client.hpp"
#include "strategy/strategy.hpp"
#include "version.hpp"

namespace {

std::atomic<bool> g_stop{false};
void OnSignal(int) {
  g_stop.store(true);
}

void PrintUsage(const char* prog) {
  spdlog::error(
      "usage: {} [--version | --config <path> | --run --config <path> [--seconds N] [--log] | "
      "--smoke --config <path> | --replay <log.bin>]",
      prog);
}

// Strip scheme (wss://, https://) and any path, returning the host.
std::string HostOf(const std::string& url) {
  std::string s = url;
  const auto scheme = s.find("://");
  if (scheme != std::string::npos) s = s.substr(scheme + 3);
  const auto slash = s.find('/');
  if (slash != std::string::npos) s = s.substr(0, slash);
  return s;
}

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

asmm::MarketDataParams MakeParams(const asmm::AppConfig& cfg) {
  const asmm::MarketDataConfig& md = cfg.market_data;
  asmm::MarketDataParams p;
  p.ws_host = HostOf(cfg.ws_market_url);
  p.ws_target = "/ws/" + Lower(cfg.symbol) + "@" + md.depth_stream;
  p.rest_host = HostOf(md.snapshot_rest_url);
  p.snapshot_target =
      "/api/v3/depth?symbol=" + cfg.symbol + "&limit=" + std::to_string(md.depth_snapshot_limit);
  p.filters = asmm::SymbolFilters{md.px_scale, md.qty_scale};
  p.backoff_initial_ms = md.backoff_initial_ms;
  p.backoff_max_ms = md.backoff_max_ms;
  p.backoff_multiplier = md.backoff_multiplier;
  return p;
}

std::string MakeLogPath(const std::string& log_dir) {
  std::error_code ec;
  std::filesystem::create_directories(log_dir, ec);
  const std::time_t t = std::time(nullptr);
  char name[64];
  std::strftime(name, sizeof(name), "events-%Y%m%d-%H%M%S.bin", std::localtime(&t));
  return log_dir + "/" + name;
}

// --- Lightweight JSON field extractors for the smoke tool only (not the hot
// path, not replay). A fail-fast probe doesn't warrant a full simdjson parse.
std::string FindJsonStr(const std::string& body, const std::string& key) {
  const std::string pat = "\"" + key + "\":\"";
  const auto p = body.find(pat);
  if (p == std::string::npos) return "";
  const auto start = p + pat.size();
  const auto end = body.find('"', start);
  if (end == std::string::npos) return "";
  return body.substr(start, end - start);
}

asmm::i64 FindJsonInt(const std::string& body, const std::string& key) {
  const std::string pat = "\"" + key + "\":";
  const auto p = body.find(pat);
  if (p == std::string::npos) return -1;
  auto start = p + pat.size();
  while (start < body.size() && (body[start] == ' ')) ++start;
  asmm::i64 v = 0;
  bool any = false;
  while (start < body.size() && body[start] >= '0' && body[start] <= '9') {
    v = v * 10 + (body[start] - '0');
    any = true;
    ++start;
  }
  return any ? v : -1;
}

// Number of fractional digits down to the significant '1' of a power-of-ten tick
// like "0.01000000" (-> 2) or "0.00001000" (-> 5). Robust to trailing zeros.
int FractionalScale(const std::string& tick) {
  const auto dot = tick.find('.');
  if (dot == std::string::npos) return 0;
  int scale = 0;
  for (std::size_t i = dot + 1; i < tick.size(); ++i) {
    if (tick[i] != '0') scale = static_cast<int>(i - dot);
  }
  return scale;
}

// Phase 5 fail-fast: prove testnet reachability, key/signature validity, symbol
// filters, and the user-data listenKey lifecycle BEFORE building the live engine.
// Network-only (CI-exempt). Returns 0 iff every check passes.
int RunSmoke(const asmm::AppConfig& cfg) {
  if (cfg.api_key.empty() || cfg.api_secret.empty()) {
    spdlog::error("smoke: BINANCE_TESTNET_API_KEY / _SECRET not set (populate .env). Aborting.");
    return 1;
  }
  const std::string host = HostOf(cfg.rest_url);
  if (!asmm::IsTestnetOrderHost(host)) {  // defense in depth; config already checked
    spdlog::error("smoke: rest_url host '{}' is not a testnet order host", host);
    return 1;
  }
  auto rest = asmm::MakeBeastSignedRestClient(
      cfg.api_key, cfg.api_secret, cfg.orders.recv_window_ms, cfg.market_data.read_timeout_s);
  spdlog::info("smoke: host={} recvWindow={}ms", host, cfg.orders.recv_window_ms);

  // (1) GET /api/v3/time -> reachability + clock offset.
  const auto local_before = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count();
  const asmm::HttpResult t = rest->Request(asmm::HttpMethod::kGet, host, "/api/v3/time", "", false);
  if (!t.ok()) {
    spdlog::error("smoke: GET /time failed (status={} err='{}'). Testnet unreachable from here?",
                  t.status, t.err);
    return 1;
  }
  const asmm::i64 server_ms = FindJsonInt(t.body, "serverTime");
  const asmm::i64 offset = server_ms - local_before;
  spdlog::info("smoke: [1/4] /time ok serverTime={} local~{} offset={}ms", server_ms, local_before,
               offset);
  if (server_ms < 0) {
    spdlog::error("smoke: could not parse serverTime from '{}'", t.body);
    return 1;
  }
  if (std::llabs(offset) > cfg.orders.recv_window_ms / 2) {
    spdlog::error("smoke: clock skew {}ms exceeds recvWindow/2 ({}ms). NTP-sync the box.", offset,
                  cfg.orders.recv_window_ms / 2);
    return 1;
  }
  rest->SetTimeOffsetMs(offset);

  // (2) GET /api/v3/exchangeInfo -> verify tickSize/stepSize match our scales.
  const asmm::HttpResult ei = rest->Request(asmm::HttpMethod::kGet, host, "/api/v3/exchangeInfo",
                                            "symbol=" + cfg.symbol, false);
  if (!ei.ok()) {
    spdlog::error("smoke: GET /exchangeInfo failed (status={} body='{}')", ei.status, ei.body);
    return 1;
  }
  const int tick_scale = FractionalScale(FindJsonStr(ei.body, "tickSize"));
  const int step_scale = FractionalScale(FindJsonStr(ei.body, "stepSize"));
  spdlog::info("smoke: [2/4] exchangeInfo tickSize->px_scale={} stepSize->qty_scale={}", tick_scale,
               step_scale);
  if (tick_scale != cfg.market_data.px_scale || step_scale != cfg.market_data.qty_scale) {
    spdlog::error("smoke: filter mismatch! config px_scale={} qty_scale={} but exchange says {}/{}",
                  cfg.market_data.px_scale, cfg.market_data.qty_scale, tick_scale, step_scale);
    return 1;
  }

  // (3) POST /api/v3/order/test (signed) LIMIT_MAKER, far below market so it can
  //     never cross. Validates key + signature + filters without resting an order.
  const std::string price = asmm::ScaledToDecimal(10000 * 100, cfg.market_data.px_scale);  // 10000
  const std::string qty = asmm::ScaledToDecimal(100, cfg.market_data.qty_scale);           // 0.001
  const std::string order_q =
      "symbol=" + cfg.symbol + "&side=BUY&type=LIMIT_MAKER&quantity=" + qty + "&price=" + price;
  const asmm::HttpResult ot =
      rest->Request(asmm::HttpMethod::kPost, host, "/api/v3/order/test", order_q, true);
  if (!ot.ok()) {
    spdlog::error(
        "smoke: POST /order/test failed (status={} body='{}'). Bad key/signature/filters?",
        ot.status, ot.body);
    return 1;
  }
  spdlog::info("smoke: [3/4] /order/test ok (signed LIMIT_MAKER {} @ {})", qty, price);

  // (4) User-data listenKey lifecycle: create (key-only, no signature) + delete.
  const asmm::HttpResult lk =
      rest->Request(asmm::HttpMethod::kPost, host, "/api/v3/userDataStream", "", false);
  const std::string listen_key = FindJsonStr(lk.body, "listenKey");
  if (!lk.ok() || listen_key.empty()) {
    spdlog::error("smoke: POST /userDataStream failed (status={} body='{}')", lk.status, lk.body);
    return 1;
  }
  const asmm::HttpResult del = rest->Request(
      asmm::HttpMethod::kDelete, host, "/api/v3/userDataStream", "listenKey=" + listen_key, false);
  spdlog::info("smoke: [4/4] userDataStream ok listenKey={}... delete_status={}",
               listen_key.substr(0, 8), del.status);

  spdlog::info(
      "smoke: ALL GREEN — testnet reachable, keys/signature valid, filters match, "
      "user-data stream OK. Safe to build Stage B / run live.");
  return 0;
}

int RunLive(const asmm::AppConfig& cfg, int seconds, bool do_log) {
  std::signal(SIGINT, OnSignal);
  std::signal(SIGTERM, OnSignal);

  auto queue = std::make_unique<asmm::MarketQueue>();
  auto cc_queue = std::make_unique<asmm::CrossCheckQueue>();
  auto ws = asmm::MakeBeastWsClient(cfg.market_data.read_timeout_s);
  auto rest = asmm::MakeBeastRestClient(cfg.market_data.read_timeout_s);
  auto cc_rest = asmm::MakeBeastRestClient(cfg.market_data.read_timeout_s);
  const asmm::MarketDataParams params = MakeParams(cfg);

  std::unique_ptr<asmm::EventLogWriter> log;
  if (do_log) {
    const std::string path = MakeLogPath(cfg.log_dir);
    log = std::make_unique<asmm::EventLogWriter>(path);
    spdlog::info("event log: {}", path);
  }

  std::unique_ptr<asmm::Strategy> strat;
  if (cfg.strategy_enabled) {
    strat = std::make_unique<asmm::Strategy>(cfg.strategy);
    spdlog::info("strategy: gamma={} kappa={} tau={} q_max={} size={} (PAPER, no orders)",
                 cfg.strategy.gamma, cfg.strategy.kappa, cfg.strategy.tau_days,
                 cfg.strategy.q_max_lots, cfg.strategy.quote_size_lots);
  }

  asmm::MarketDataThread md(*ws, *rest, params, *queue);
  asmm::Engine engine(*queue, cfg.market_data.stale_threshold_ms, cc_queue.get(),
                      cfg.market_data.crosscheck_levels, log.get(), strat.get());

  const std::string cc_target = "/api/v3/depth?symbol=" + cfg.symbol +
                                "&limit=" + std::to_string(cfg.market_data.crosscheck_depth_limit);

  spdlog::info("run: ws={}{} rest={}{}", params.ws_host, params.ws_target, params.rest_host,
               params.snapshot_target);

  std::thread md_thread([&] { md.Run(g_stop); });
  std::thread engine_thread([&] { engine.Run(g_stop); });

  // Cross-check thread: periodic REST snapshot -> CrossCheckMsg -> engine.
  std::thread cc_thread([&] {
    const int interval = cfg.market_data.crosscheck_interval_s;
    while (!g_stop.load()) {
      for (int s = 0; s < interval && !g_stop.load(); ++s)
        std::this_thread::sleep_for(std::chrono::seconds(1));
      if (g_stop.load()) break;
      std::string err;
      const auto body = cc_rest->Get(params.rest_host, cc_target, err);
      if (!body) {
        spdlog::warn("crosscheck: fetch failed: {}", err);
        continue;
      }
      asmm::DepthSnapshot snap;
      asmm::CrossCheckMsg msg;
      if (asmm::ParseDepthSnapshot(*body, params.filters, snap) &&
          asmm::ToCrossCheckMsg(snap, msg)) {
        cc_queue->try_push(msg);
      }
    }
  });

  const asmm::i64 deadline_ns =
      seconds > 0 ? asmm::NowNs() + asmm::i64(seconds) * 1'000'000'000 : 0;
  while (!g_stop.load()) {
    std::this_thread::sleep_for(std::chrono::seconds(2));
    const auto bb = engine.book().BestBid();
    const auto ba = engine.book().BestAsk();
    spdlog::info(
        "status live={} bid={} ask={} lastId={} applied={} snaps={} gaps={} resyncs={} "
        "reconnects={} undetected_gaps={} stale={} xcheck_ok={} xcheck_fail={} xcheck_skip={}",
        engine.book_live(), bb ? bb->px_ticks : 0, ba ? ba->px_ticks : 0,
        engine.book().LastUpdateId(), engine.counters().events_applied,
        engine.counters().snapshots_applied, md.counters().gaps_detected, md.counters().resyncs,
        md.counters().ws_reconnects, engine.undetected_gaps(), engine.counters().stale_episodes,
        engine.counters().crosscheck_ok, engine.counters().crosscheck_fail,
        engine.counters().crosscheck_skipped);
    if (strat) {
      spdlog::info("  strategy q={} fills={} sigma_p_micro={}", engine.strat_inventory(),
                   engine.strat_fills(), engine.strat_sigma_micro());
    }
    if (deadline_ns != 0 && asmm::NowNs() >= deadline_ns) g_stop.store(true);
  }

  md_thread.join();
  engine_thread.join();
  cc_thread.join();
  if (log) log->Close();
  spdlog::info(
      "shutdown: applied={} snapshots={} gaps={} resyncs={} reconnects={} undetected_gaps={} "
      "xcheck_ok={} xcheck_fail={}",
      engine.counters().events_applied, engine.counters().snapshots_applied,
      md.counters().gaps_detected, md.counters().resyncs, md.counters().ws_reconnects,
      engine.undetected_gaps(), engine.counters().crosscheck_ok, engine.counters().crosscheck_fail);
  return (engine.undetected_gaps() == 0 && engine.counters().crosscheck_fail == 0) ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

  if (argc < 2) {
    PrintUsage(argv[0]);
    return 1;
  }
  const std::string arg = argv[1];

  if (arg == "--version") {
    std::printf("asmm %s (git %s, %s)\n", asmm::kVersion, asmm::kGitSha, asmm::kBuildType);
    return 0;
  }

  // Convert a recorded depth jsonl to a binary event log: --jsonl-to-log <in> <out>
  if (arg == "--jsonl-to-log") {
    if (argc < 4) {
      PrintUsage(argv[0]);
      return 1;
    }
    try {
      const asmm::SymbolFilters filters{2, 5};  // BTCUSDT
      std::vector<asmm::MarketEvent> events;
      std::ifstream in(argv[2]);
      std::string line;
      while (std::getline(in, line)) {
        if (!line.empty()) asmm::ParseDepthDiff(line, filters, 0, events);
      }
      asmm::WriteEventsToLog(argv[3], std::span<const asmm::MarketEvent>(events));
      spdlog::info("wrote {} events to {}", events.size(), argv[3]);
      return 0;
    } catch (const std::exception& e) {
      spdlog::error("jsonl-to-log error: {}", e.what());
      return 1;
    }
  }

  // Parse flags (shared across modes).
  std::string config_path;
  std::string replay_path;
  int seconds = 0;
  bool do_log = false;
  double gamma_override = 0.0;
  const bool run_mode = (arg == "--run");
  const bool replay_mode = (arg == "--replay");
  const bool smoke_mode = (arg == "--smoke");
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--config" && i + 1 < argc)
      config_path = argv[++i];
    else if (a == "--seconds" && i + 1 < argc)
      seconds = std::atoi(argv[++i]);
    else if (a == "--gamma" && i + 1 < argc)
      gamma_override = std::atof(argv[++i]);
    else if (a == "--log")
      do_log = true;
    else if (a == "--replay" && i + 1 < argc)
      replay_path = argv[++i];
  }

  // Offline replay of a recorded log: --replay <log.bin> [--config <toml> [--gamma G]]
  if (replay_mode) {
    if (replay_path.empty()) {
      PrintUsage(argv[0]);
      return 1;
    }
    try {
      const auto decisions = asmm::ReplayLogFile(replay_path);
      const auto& last = decisions.empty() ? asmm::DecisionRecord{} : decisions.back();
      spdlog::info("replayed {} decisions from {}", decisions.size(), replay_path);
      spdlog::info("book final: lastId={} bid={} ask={} mid_x2={}", last.final_update_id,
                   last.best_bid_px, last.best_ask_px, last.mid_x2);
      if (!config_path.empty()) {
        asmm::AppConfig cfg = asmm::LoadConfig(config_path, ".env");
        if (gamma_override > 0.0) cfg.strategy.gamma = gamma_override;
        const auto s = asmm::ReplayStrategyFile(replay_path, cfg.strategy).summary;
        spdlog::info(
            "strategy gamma={}: quotes={} fills={} final_q={} max|q|={} mean_q={:.2f} "
            "cross_viol={} one_sided={} pulled={} cash_units={}",
            cfg.strategy.gamma, s.quotes, s.fills, s.final_inventory, s.max_abs_inventory,
            s.mean_inventory, s.cross_violations, s.one_sided_ticks, s.pulled_ticks,
            s.final_cash_units);
      }
      return 0;
    } catch (const std::exception& e) {
      spdlog::error("replay error: {}", e.what());
      return 1;
    }
  }

  if (config_path.empty()) {
    PrintUsage(argv[0]);
    return 1;
  }

  try {
    asmm::AppConfig cfg = asmm::LoadConfig(config_path, ".env");
    if (gamma_override > 0.0) cfg.strategy.gamma = gamma_override;
    if (smoke_mode) return RunSmoke(cfg);
    if (run_mode) return RunLive(cfg, seconds, do_log);
    // --config: load-and-report (unchanged Phase 0 behavior).
    spdlog::info("loaded config: symbol={} rest_url={} ws_market_url={} log_dir={}", cfg.symbol,
                 cfg.rest_url, cfg.ws_market_url, cfg.log_dir);
    spdlog::info("api key loaded: {} (len {})", cfg.api_key.empty() ? "no" : "yes",
                 cfg.api_key.size());
    return 0;
  } catch (const std::exception& e) {
    spdlog::error("error: {}", e.what());
    return 1;
  }
}
