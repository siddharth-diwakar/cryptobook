#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <span>
#include <string>
#include <vector>

#include "engine/replay.hpp"
#include "exchange/binance_depth.hpp"
#include "strategy/params.hpp"

#ifndef ASMM_DATA_DIR
#define ASMM_DATA_DIR "."
#endif

using namespace asmm;

namespace {

std::string QBytes(const std::vector<QuoteRecord>& q) {
  return std::string(reinterpret_cast<const char*>(q.data()), q.size() * sizeof(QuoteRecord));
}
std::string FBytes(const std::vector<FillRecord>& f) {
  return std::string(reinterpret_cast<const char*>(f.data()), f.size() * sizeof(FillRecord));
}

}  // namespace

TEST_CASE("strategy replay: deterministic and invariant-clean on the fixture",
          "[strategy][replay]") {
  const std::string jsonl = std::string(ASMM_DATA_DIR) + "/depth_btcusdt.jsonl";
  {
    std::ifstream probe(jsonl);
    if (!probe) SKIP("no recorded fixture");
  }
  const SymbolFilters f{2, 5};
  std::vector<MarketEvent> events;
  std::ifstream in(jsonl);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty()) ParseDepthDiff(line, f, 0, events);
  }
  REQUIRE(events.size() > 1000);

  StrategyParams p;
  p.gamma = 1e-9;  // tiny -> actually quotes/fills at BTC price scale
  p.sigma_min_samples = 5;
  p.q_max_lots = 1000;
  p.quote_size_lots = 10;
  p.min_notional_usdt = 0.0;

  const auto a = ReplayStrategy(std::span<const MarketEvent>(events), p);
  const auto b = ReplayStrategy(std::span<const MarketEvent>(events), p);

  // Same build, same input -> byte-identical quote and fill streams.
  REQUIRE(QBytes(a.quotes) == QBytes(b.quotes));
  REQUIRE(FBytes(a.fills) == FBytes(b.fills));

  // Invariants (MODEL.md acceptance): quotes never cross; inventory bounded.
  REQUIRE(a.summary.cross_violations == 0);
  REQUIRE(a.summary.max_abs_inventory <= 2 * p.q_max_lots);
  for (const auto& q : a.quotes) {
    if (q.bid_px > 0 && q.ask_px > 0) REQUIRE(q.bid_px < q.ask_px);
  }
}
