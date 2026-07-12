#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
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
      "--replay <log.bin>]",
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
