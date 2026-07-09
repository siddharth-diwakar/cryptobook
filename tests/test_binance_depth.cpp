#include <catch2/catch_test_macros.hpp>
#include <string_view>
#include <vector>

#include "exchange/binance_depth.hpp"

using namespace asmm;

TEST_CASE("decimal->scaled: exact fixed-point conversion", "[parse]") {
  i64 v = -1;
  REQUIRE(DecimalToScaled("104250.10", 2, v));
  REQUIRE(v == 10425010);
  REQUIRE(DecimalToScaled("104250.10000000", 2, v));  // Binance zero-padding
  REQUIRE(v == 10425010);
  REQUIRE(DecimalToScaled("10", 5, v));
  REQUIRE(v == 1000000);
  REQUIRE(DecimalToScaled("2.5", 5, v));
  REQUIRE(v == 250000);
  REQUIRE(DecimalToScaled("0.00000000", 5, v));
  REQUIRE(v == 0);
  REQUIRE(DecimalToScaled("0.00001", 5, v));
  REQUIRE(v == 1);

  REQUIRE_FALSE(DecimalToScaled("104250.123", 2, v));  // finer than tick
  REQUIRE_FALSE(DecimalToScaled("abc", 2, v));
  REQUIRE_FALSE(DecimalToScaled("", 2, v));
  REQUIRE_FALSE(DecimalToScaled("1.2.3", 2, v));
}

TEST_CASE("parse depth diff: basic fields, levels, deletion", "[parse]") {
  const std::string_view j =
      R"({"e":"depthUpdate","E":123456789,"s":"BTCUSDT","U":157,"u":160,)"
      R"("b":[["104250.10","2.5"],["104249.00","0"]],"a":[["104251.20","1.0"]]})";
  const SymbolFilters f{2, 5};
  std::vector<MarketEvent> out;
  REQUIRE(ParseDepthDiff(j, f, 42, out));
  REQUIRE(out.size() == 1);

  const MarketEvent& e = out[0];
  REQUIRE(e.first_update_id == 157);
  REQUIRE(e.final_update_id == 160);
  REQUIRE(e.ts_recv_ns == 42);
  REQUIRE(e.ts_exchange_ms == 123456789);
  REQUIRE(e.num_bids == 2);
  REQUIRE(e.num_asks == 1);
  REQUIRE(e.levels[0].px_ticks == 10425010);
  REQUIRE(e.levels[0].qty_lots == 250000);
  REQUIRE(e.levels[1].qty_lots == 0);  // deletion carried as qty 0
  REQUIRE(e.levels[2].px_ticks == 10425120);
  REQUIRE((e.flags & kFlagContinuation) == 0);
}

TEST_CASE("parse depth diff: large diff fragments across events", "[parse]") {
  // Build a diff with 100 bid levels + 40 ask levels (> kMaxLevelsPerEvent=64).
  std::string j = R"({"e":"depthUpdate","E":1,"s":"BTCUSDT","U":1,"u":2,"b":[)";
  for (int i = 0; i < 100; ++i) {
    if (i) j += ",";
    j += R"(["1)" + std::to_string(1000 + i) + R"(.00","1"])";
  }
  j += R"(],"a":[)";
  for (int i = 0; i < 40; ++i) {
    if (i) j += ",";
    j += R"(["2)" + std::to_string(1000 + i) + R"(.00","1"])";
  }
  j += "]}";

  const SymbolFilters f{2, 5};
  std::vector<MarketEvent> out;
  REQUIRE(ParseDepthDiff(j, f, 7, out));

  std::size_t total_bids = 0;
  std::size_t total_asks = 0;
  for (std::size_t k = 0; k < out.size(); ++k) {
    total_bids += out[k].num_bids;
    total_asks += out[k].num_asks;
    const bool is_last = (k + 1 == out.size());
    REQUIRE(((out[k].flags & kFlagContinuation) != 0) == !is_last);
    REQUIRE(out[k].first_update_id == 1);
    REQUIRE(out[k].final_update_id == 2);
  }
  REQUIRE(total_bids == 100);
  REQUIRE(total_asks == 40);
}

TEST_CASE("parse snapshot", "[parse]") {
  const std::string_view j =
      R"({"lastUpdateId":160,"bids":[["104250.10","2.5"]],"asks":[["104251.20","1.0"]]})";
  const SymbolFilters f{2, 5};
  DepthSnapshot s;
  REQUIRE(ParseDepthSnapshot(j, f, s));
  REQUIRE(s.last_update_id == 160);
  REQUIRE(s.bids.size() == 1);
  REQUIRE(s.bids[0].px_ticks == 10425010);
  REQUIRE(s.asks[0].px_ticks == 10425120);
}
