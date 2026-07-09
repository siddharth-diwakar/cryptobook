#include <simdjson.h>

#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "core/book.hpp"
#include "exchange/binance_depth.hpp"

#ifndef ASMM_DATA_DIR
#define ASMM_DATA_DIR "."
#endif

using namespace asmm;

namespace {

std::optional<std::string> Slurp(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

DepthLevel BestOf(const std::vector<DepthLevel>& levels, Side side) {
  DepthLevel best = levels.front();
  for (const auto& l : levels) {
    if (side == Side::kBid ? (l.px_ticks > best.px_ticks) : (l.px_ticks < best.px_ticks)) {
      best = l;
    }
  }
  return best;
}

}  // namespace

// Acceptance (ROADMAP Phase 1): replaying the recording reproduces best bid/ask
// matching the REST snapshot taken at the end of the recording.
TEST_CASE("replay: recorded depth reproduces snapshot top-of-book", "[replay]") {
  const std::string dir = ASMM_DATA_DIR;
  const auto meta_s = Slurp(dir + "/depth_btcusdt.meta.json");
  const auto snap_s = Slurp(dir + "/depth_btcusdt.snapshot.json");
  const auto jsonl_path = dir + "/depth_btcusdt.jsonl";

  if (!meta_s || !snap_s || !Slurp(jsonl_path).has_value()) {
    SKIP("no recorded fixture in " + dir + " — run: python analysis/record_depth.py --minutes 30");
  }

  simdjson::dom::parser parser;
  simdjson::dom::element meta;
  REQUIRE_FALSE(parser.parse(simdjson::padded_string(*meta_s)).get(meta));
  std::int64_t px_scale = 0;
  std::int64_t qty_scale = 0;
  std::uint64_t snap_id = 0;
  REQUIRE_FALSE(meta["px_scale"].get(px_scale));
  REQUIRE_FALSE(meta["qty_scale"].get(qty_scale));
  REQUIRE_FALSE(meta["snapshot_last_update_id"].get(snap_id));
  const SymbolFilters filters{static_cast<int>(px_scale), static_cast<int>(qty_scale)};

  DepthSnapshot snap;
  REQUIRE(ParseDepthSnapshot(*snap_s, filters, snap));
  REQUIRE_FALSE(snap.bids.empty());
  REQUIRE_FALSE(snap.asks.empty());

  L2Book book;
  std::ifstream in(jsonl_path);
  std::string line;
  std::uint64_t prev_final = 0;
  bool first = true;
  std::size_t lines_applied = 0;

  while (std::getline(in, line)) {
    if (line.empty()) continue;
    std::vector<MarketEvent> events;
    REQUIRE(ParseDepthDiff(line, filters, 0, events));
    REQUIRE_FALSE(events.empty());
    const std::uint64_t U = events.front().first_update_id;
    const std::uint64_t u = events.front().final_update_id;

    if (u <= snap_id) {
      // fully before the snapshot; apply
    } else if (U <= snap_id + 1 && snap_id + 1 <= u) {
      // straddles the snapshot; apply then stop
    } else {
      break;  // past the snapshot
    }

    // Sequence continuity across the recording (spot: U == prev_u + 1). If this
    // fails the recording itself has a gap — re-record.
    if (!first) {
      REQUIRE(U == prev_final + 1);
    }
    first = false;
    prev_final = u;

    for (const auto& ev : events) book.ApplyEvent(ev);
    ++lines_applied;

    if (u > snap_id) break;  // that was the straddle event
  }

  REQUIRE(lines_applied > 0);

  const DepthLevel snap_bb = BestOf(snap.bids, Side::kBid);
  const DepthLevel snap_ba = BestOf(snap.asks, Side::kAsk);

  REQUIRE(book.BestBid().has_value());
  REQUIRE(book.BestAsk().has_value());
  REQUIRE(book.BestBid()->px_ticks == snap_bb.px_ticks);
  REQUIRE(book.BestBid()->qty_lots == snap_bb.qty_lots);
  REQUIRE(book.BestAsk()->px_ticks == snap_ba.px_ticks);
  REQUIRE(book.BestAsk()->qty_lots == snap_ba.qty_lots);
}
