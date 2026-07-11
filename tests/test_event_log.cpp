#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <string>
#include <vector>

#include "engine/event_log.hpp"

#ifndef ASMM_TMP_DIR
#define ASMM_TMP_DIR "."
#endif

using namespace asmm;

namespace {

std::string Tmp(const char* name) {
  return std::string(ASMM_TMP_DIR) + "/" + name;
}

MarketEvent Ev(u64 U, u64 u) {
  MarketEvent ev{};
  ev.first_update_id = U;
  ev.final_update_id = u;
  ev.kind = EventKind::kDepthDiff;
  ev.num_bids = 1;
  ev.levels[0] = DepthLevel{100, 5};
  return ev;
}

}  // namespace

TEST_CASE("event_log: write then read round-trips records in order", "[eventlog]") {
  const std::string path = Tmp("rt.bin");
  DecisionRecord d{};
  d.final_update_id = 99;
  d.best_bid_px = 100;
  d.best_bid_qty = 5;
  {
    EventLogWriter w(path);
    w.WriteMarketEvent(Ev(1, 2));
    w.WriteDecision(d);
    w.WriteMarketEvent(Ev(3, 4));
    w.Close();
  }

  EventLogReader r(path);
  u32 t = 0;
  MarketEvent ev{};
  DecisionRecord dd{};

  REQUIRE(r.Next(t, ev, dd));
  REQUIRE(t == kRecMarketEvent);
  REQUIRE(ev.final_update_id == 2);

  REQUIRE(r.Next(t, ev, dd));
  REQUIRE(t == kRecDecision);
  REQUIRE(dd.final_update_id == 99);
  REQUIRE(dd.best_bid_px == 100);

  REQUIRE(r.Next(t, ev, dd));
  REQUIRE(t == kRecMarketEvent);
  REQUIRE(ev.final_update_id == 4);

  REQUIRE_FALSE(r.Next(t, ev, dd));  // clean EOF
}

TEST_CASE("event_log: reader rejects a bad file header", "[eventlog]") {
  const std::string path = Tmp("bad.bin");
  {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    REQUIRE(f != nullptr);
    const char junk[16] = "NOPE-badheader";
    std::fwrite(junk, 1, sizeof(junk), f);
    std::fclose(f);
  }
  REQUIRE_THROWS(EventLogReader(path));
}
