#include <catch2/catch_test_macros.hpp>

#include "core/events.hpp"
#include "strategy/paper_fills.hpp"
#include "strategy/params.hpp"
#include "strategy/strategy.hpp"

using namespace asmm;

namespace {
ExecEvent Fill(Side side, i64 px, i64 last_qty, i64 cum, i64 fee, CommAsset asset) {
  ExecEvent e{};
  e.side = side;
  e.last_px_ticks = px;
  e.last_qty_lots = last_qty;
  e.cum_qty_lots = cum;
  e.fee_units = fee;
  e.comm_asset = asset;
  e.kind = (cum >= last_qty && last_qty > 0) ? ExecKind::kFilled : ExecKind::kPartial;
  return e;
}
}  // namespace

TEST_CASE("real fills: a buy then sell nets spread minus fees (quote commission)", "[real_fills]") {
  PaperBook pb(10.0);  // 10 bps maker fee
  pb.ApplyRealFill(Fill(Side::kBid, 6'500'000, 4, 4, 26, CommAsset::kQuote));
  REQUIRE(pb.inventory() == 4);
  REQUIRE(pb.cash_units() == -(6'500'000 * 4) - 26);

  pb.ApplyRealFill(Fill(Side::kAsk, 6'600'000, 4, 4, 26, CommAsset::kQuote));
  REQUIRE(pb.inventory() == 0);
  // Bought 4 @ 6.5M, sold 4 @ 6.6M -> +400000 gross, minus 26+26 fees.
  REQUIRE(pb.cash_units() == 400'000 - 52);
}

TEST_CASE("real fills: partial fills accumulate inventory", "[real_fills]") {
  PaperBook pb(10.0);
  pb.ApplyRealFill(Fill(Side::kBid, 6'500'000, 3, 3, 20, CommAsset::kQuote));
  pb.ApplyRealFill(Fill(Side::kBid, 6'500'000, 7, 10, 46, CommAsset::kQuote));
  REQUIRE(pb.inventory() == 10);
  REQUIRE(pb.cash_units() == -(6'500'000 * 10) - 66);
}

TEST_CASE("real fills: base-asset commission reduces inventory, not cash", "[real_fills]") {
  PaperBook pb(10.0);
  pb.ApplyRealFill(Fill(Side::kBid, 100, 10, 10, 1, CommAsset::kBase));
  REQUIRE(pb.inventory() == 9);       // 10 bought, 1 lot taken as fee
  REQUIRE(pb.cash_units() == -1000);  // cash only reflects the trade

  // BNB (other) commission is excluded from BTC/USDT PnL entirely.
  PaperBook pb2(10.0);
  pb2.ApplyRealFill(Fill(Side::kBid, 100, 10, 10, 999, CommAsset::kOther));
  REQUIRE(pb2.inventory() == 10);
  REQUIRE(pb2.cash_units() == -1000);
}

TEST_CASE("real fills: non-fill reports (l==0) are ignored", "[real_fills]") {
  PaperBook pb(10.0);
  ExecEvent ack{};
  ack.side = Side::kBid;
  ack.last_qty_lots = 0;
  ack.kind = ExecKind::kAck;
  pb.ApplyRealFill(ack);
  REQUIRE(pb.inventory() == 0);
  REQUIRE(pb.cash_units() == 0);
}

TEST_CASE("real fills: Strategy in live mode routes fills through OnExec", "[real_fills]") {
  StrategyParams p;
  Strategy strat(p, /*live_mode=*/true);
  REQUIRE(strat.inventory() == 0);
  strat.OnExec(Fill(Side::kBid, 6'500'000, 5, 5, 30, CommAsset::kQuote));
  REQUIRE(strat.inventory() == 5);
  REQUIRE(strat.fill_count() == 1);
  strat.OnExec(Fill(Side::kAsk, 6'600'000, 5, 5, 30, CommAsset::kQuote));
  REQUIRE(strat.inventory() == 0);
  REQUIRE(strat.fill_count() == 2);
}
