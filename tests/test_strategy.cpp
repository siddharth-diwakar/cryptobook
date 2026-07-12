#include <catch2/catch_test_macros.hpp>

#include "core/book.hpp"
#include "core/events.hpp"
#include "strategy/strategy.hpp"

using namespace asmm;

namespace {

L2Book MakeBook(i64 bid_px, i64 bid_qty, i64 ask_px, i64 ask_qty) {
  L2Book b;
  b.Apply(Side::kBid, bid_px, bid_qty);
  b.Apply(Side::kAsk, ask_px, ask_qty);
  return b;
}

MarketEvent Ev(i64 ts_ms, u64 fid) {
  MarketEvent e{};
  e.kind = EventKind::kDepthDiff;
  e.ts_exchange_ms = ts_ms;
  e.final_update_id = fid;
  return e;
}

StrategyParams Params() {
  StrategyParams p;
  p.sigma_min_samples = 2;
  p.sigma_spike_threshold = 1e9;  // never spike in tests
  p.q_max_lots = 1000;
  p.quote_size_lots = 5;
  p.min_notional_usdt = 0.0;
  p.maker_fee_bps = 0.0;
  p.gamma = 1e-9;  // tiny -> quotes hug the mid so they rest near top-of-book
  p.hysteresis_ticks = 1;
  return p;
}

}  // namespace

TEST_CASE("strategy: warmup pulls quotes, then quotes two-sided", "[strategy]") {
  Strategy s(Params());
  REQUIRE(s.OnEvent(MakeBook(10000, 100, 10002, 100), Ev(1000, 1)).quote.pulled == 1);
  REQUIRE(s.OnEvent(MakeBook(10000, 100, 10002, 100), Ev(2000, 2)).quote.pulled == 1);
  const auto o3 = s.OnEvent(MakeBook(10001, 100, 10003, 100), Ev(3000, 3));
  REQUIRE(o3.quote.pulled == 0);
  REQUIRE(o3.quote.quoting == 1);
  REQUIRE(o3.quote.bid_px < o3.quote.ask_px);
}

TEST_CASE("strategy: resting bid is filled when the book crosses it next event", "[strategy]") {
  Strategy s(Params());
  s.OnEvent(MakeBook(10000, 100, 10002, 100), Ev(1000, 1));
  s.OnEvent(MakeBook(10000, 100, 10002, 100), Ev(2000, 2));
  const auto o3 = s.OnEvent(MakeBook(10001, 100, 10003, 100), Ev(3000, 3));
  REQUIRE(o3.quote.quoting == 1);
  const i64 our_bid = o3.quote.bid_px;
  REQUIRE(our_bid > 0);

  // Next event: best ask drops to at/below our resting bid -> buy fill (capped at
  // the displayed ask qty of 3).
  const auto o4 = s.OnEvent(MakeBook(our_bid - 6, 100, our_bid, 3), Ev(4000, 4));
  REQUIRE_FALSE(o4.fills.empty());
  REQUIRE(o4.fills[0].side == 0);      // buy
  REQUIRE(o4.fills[0].qty_lots == 3);  // partial: min(size 5, displayed 3)
  REQUIRE(s.inventory() == 3);
}

TEST_CASE("strategy: one-sided past the inventory limit", "[strategy]") {
  StrategyParams p = Params();
  p.q_max_lots = 2;  // low limit
  Strategy s(p);
  s.OnEvent(MakeBook(10000, 100, 10002, 100), Ev(1000, 1));
  s.OnEvent(MakeBook(10000, 100, 10002, 100), Ev(2000, 2));
  const i64 bid = s.OnEvent(MakeBook(10001, 100, 10003, 100), Ev(3000, 3)).quote.bid_px;
  // Same event: fill past +q_max (buy 3 > limit 2), then re-quote one-sided.
  const auto o4 = s.OnEvent(MakeBook(bid - 6, 100, bid, 3), Ev(4000, 4));
  REQUIRE(s.inventory() == 3);       // bought past the +2 limit
  REQUIRE(o4.quote.one_sided == 1);  // now long -> stop buying
  REQUIRE(o4.quote.bid_px == 0);     // bid pulled
  REQUIRE(o4.quote.ask_px > 0);      // still offering to sell down
}
