#include <catch2/catch_test_macros.hpp>
#include <span>
#include <vector>

#include "core/book.hpp"
#include "engine/crosscheck.hpp"
#include "exchange/binance_depth.hpp"

using namespace asmm;

namespace {

MarketEvent Diff(u64 U, u64 u, std::vector<DepthLevel> bids, std::vector<DepthLevel> asks) {
  MarketEvent ev{};
  ev.kind = EventKind::kDepthDiff;
  ev.first_update_id = U;
  ev.final_update_id = u;
  std::size_t slot = 0;
  for (const auto& b : bids) {
    ev.levels[slot++] = b;
    ++ev.num_bids;
  }
  for (const auto& a : asks) {
    ev.levels[slot++] = a;
    ++ev.num_asks;
  }
  return ev;
}

}  // namespace

TEST_CASE("crosscheck: aligned book matches snapshot", "[crosscheck]") {
  DepthSnapshot snap;
  snap.last_update_id = 1000;
  snap.bids = {{100, 5}, {99, 4}};
  snap.asks = {{101, 6}, {102, 7}};

  // Live book: snapshot state + two diffs past L.
  L2Book live;
  for (auto& b : snap.bids) live.Apply(Side::kBid, b.px_ticks, b.qty_lots);
  for (auto& a : snap.asks) live.Apply(Side::kAsk, a.px_ticks, a.qty_lots);
  const MarketEvent d1 = Diff(1001, 1005, {{100, 8}}, {});
  const MarketEvent d2 = Diff(1006, 1010, {}, {{101, 9}});
  live.ApplyEvent(d1);
  live.ApplyEvent(d2);

  CrossCheckMsg msg;
  REQUIRE(ToCrossCheckMsg(snap, msg));
  std::vector<MarketEvent> ring = {d1, d2};
  L2Book scratch;

  REQUIRE(CrossCheck(live, msg, std::span<const MarketEvent>(ring), 2, scratch) ==
          CrossCheckResult::kMatch);
}

TEST_CASE("crosscheck: divergence is detected", "[crosscheck]") {
  DepthSnapshot snap;
  snap.last_update_id = 1000;
  snap.bids = {{100, 5}, {99, 4}};
  snap.asks = {{101, 6}, {102, 7}};

  L2Book live;
  for (auto& b : snap.bids) live.Apply(Side::kBid, b.px_ticks, b.qty_lots);
  for (auto& a : snap.asks) live.Apply(Side::kAsk, a.px_ticks, a.qty_lots);
  const MarketEvent d1 = Diff(1001, 1005, {{100, 8}}, {});
  live.ApplyEvent(d1);
  // Simulate the live book diverging from the exchange (a bug): mutate a level
  // NOT present in the ring, so the replayed scratch won't match.
  live.Apply(Side::kBid, 99, 42);

  CrossCheckMsg msg;
  REQUIRE(ToCrossCheckMsg(snap, msg));
  std::vector<MarketEvent> ring = {d1};
  L2Book scratch;

  REQUIRE(CrossCheck(live, msg, std::span<const MarketEvent>(ring), 2, scratch) ==
          CrossCheckResult::kMismatch);
}

TEST_CASE("crosscheck: ring too short is skipped, not failed", "[crosscheck]") {
  DepthSnapshot snap;
  snap.last_update_id = 1000;
  snap.bids = {{100, 5}};
  snap.asks = {{101, 6}};

  L2Book live;
  live.Apply(Side::kBid, 100, 5);
  live.Apply(Side::kAsk, 101, 6);

  CrossCheckMsg msg;
  REQUIRE(ToCrossCheckMsg(snap, msg));
  // Ring's earliest event already starts past L+1 (bracket missing).
  const MarketEvent late = Diff(1006, 1010, {{100, 8}}, {});
  std::vector<MarketEvent> ring = {late};
  L2Book scratch;

  REQUIRE(CrossCheck(live, msg, std::span<const MarketEvent>(ring), 1, scratch) ==
          CrossCheckResult::kSkipped);
}
