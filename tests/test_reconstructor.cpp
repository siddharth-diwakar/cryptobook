#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/book.hpp"
#include "exchange/binance_depth.hpp"
#include "exchange/reconstructor.hpp"

#ifndef ASMM_DATA_DIR
#define ASMM_DATA_DIR "."
#endif

using namespace asmm;

namespace {

// A diff group is the fragments sharing one U/u. For synthetic tests one event
// suffices (total levels <= kMaxLevelsPerEvent).
std::vector<MarketEvent> Group(u64 U, u64 u, std::vector<DepthLevel> bids = {},
                               std::vector<DepthLevel> asks = {}) {
  MarketEvent ev{};
  ev.first_update_id = U;
  ev.final_update_id = u;
  ev.kind = EventKind::kDepthDiff;
  std::size_t slot = 0;
  for (const auto& b : bids) {
    ev.levels[slot++] = b;
    ++ev.num_bids;
  }
  for (const auto& a : asks) {
    ev.levels[slot++] = a;
    ++ev.num_asks;
  }
  return {ev};
}

ReconAction Feed(Reconstructor& r, const std::vector<MarketEvent>& g) {
  return r.OnDiffGroup(std::span<const MarketEvent>(g));
}

}  // namespace

TEST_CASE("recon: idle needs a snapshot", "[recon]") {
  Reconstructor r;
  REQUIRE(Feed(r, Group(10, 12)) == ReconAction::kNeedSnapshot);
  REQUIRE(r.state() == Reconstructor::State::kIdle);
}

TEST_CASE("recon: snapshot fragments carry snapshot-start and last id", "[recon]") {
  DepthSnapshot snap;
  snap.last_update_id = 1000;
  for (int i = 0; i < 100; ++i) snap.bids.push_back(DepthLevel{100 - i, 1});  // 100 bids
  for (int i = 0; i < 40; ++i) snap.asks.push_back(DepthLevel{200 + i, 1});   // 40 asks

  Reconstructor r;
  std::vector<MarketEvent> out;
  r.OnSnapshot(snap, /*ts_ns=*/5, out);
  REQUIRE(r.state() == Reconstructor::State::kAwaitFirstEvent);
  REQUIRE(out.size() >= 3);  // 140 levels / 64 per event

  std::size_t total_bids = 0;
  std::size_t total_asks = 0;
  for (std::size_t k = 0; k < out.size(); ++k) {
    total_bids += out[k].num_bids;
    total_asks += out[k].num_asks;
    REQUIRE(out[k].kind == EventKind::kSnapshot);
    REQUIRE(out[k].first_update_id == 1000);
    REQUIRE(out[k].final_update_id == 1000);
    REQUIRE(((out[k].flags & kFlagSnapshotStart) != 0) == (k == 0));
    const bool is_last = (k + 1 == out.size());
    REQUIRE(((out[k].flags & kFlagContinuation) != 0) == !is_last);
  }
  REQUIRE(total_bids == 100);
  REQUIRE(total_asks == 40);
}

TEST_CASE("recon: bootstrap drops stale, brackets, goes live", "[recon]") {
  DepthSnapshot snap;
  snap.last_update_id = 1000;
  snap.bids.push_back(DepthLevel{100, 1});
  snap.asks.push_back(DepthLevel{200, 1});

  Reconstructor r;
  std::vector<MarketEvent> out;
  r.OnSnapshot(snap, 0, out);

  REQUIRE(Feed(r, Group(900, 950)) == ReconAction::kDrop);             // u < L
  REQUIRE(Feed(r, Group(998, 1000)) == ReconAction::kDrop);            // u == L
  REQUIRE(Feed(r, Group(1000, 1005)) == ReconAction::kEmitAndGoLive);  // U<=L+1<=u
  REQUIRE(r.state() == Reconstructor::State::kLive);
  REQUIRE(r.prev_u() == 1005);

  REQUIRE(Feed(r, Group(1006, 1010)) == ReconAction::kEmit);  // U == prev_u+1
  REQUIRE(r.prev_u() == 1010);
}

TEST_CASE("recon: bracket via U exactly L+1", "[recon]") {
  DepthSnapshot snap;
  snap.last_update_id = 1000;
  snap.bids.push_back(DepthLevel{100, 1});
  snap.asks.push_back(DepthLevel{200, 1});
  Reconstructor r;
  std::vector<MarketEvent> out;
  r.OnSnapshot(snap, 0, out);
  REQUIRE(Feed(r, Group(1001, 1003)) == ReconAction::kEmitAndGoLive);  // U==L+1
}

TEST_CASE("recon: snapshot too old when first event U > L+1", "[recon]") {
  DepthSnapshot snap;
  snap.last_update_id = 1000;
  snap.bids.push_back(DepthLevel{100, 1});
  snap.asks.push_back(DepthLevel{200, 1});
  Reconstructor r;
  std::vector<MarketEvent> out;
  r.OnSnapshot(snap, 0, out);
  REQUIRE(Feed(r, Group(1005, 1010)) == ReconAction::kSnapshotTooOld);  // U=1005 > 1001
  REQUIRE(r.state() == Reconstructor::State::kAwaitFirstEvent);         // stays, awaits refetch
}

TEST_CASE("recon: gap in live stream", "[recon]") {
  DepthSnapshot snap;
  snap.last_update_id = 1000;
  snap.bids.push_back(DepthLevel{100, 1});
  snap.asks.push_back(DepthLevel{200, 1});
  Reconstructor r;
  std::vector<MarketEvent> out;
  r.OnSnapshot(snap, 0, out);
  REQUIRE(Feed(r, Group(1001, 1005)) == ReconAction::kEmitAndGoLive);
  REQUIRE(Feed(r, Group(1007, 1010)) == ReconAction::kGap);  // expected 1006, got 1007
  REQUIRE(r.state() == Reconstructor::State::kIdle);
  REQUIRE(Feed(r, Group(1011, 1012)) == ReconAction::kNeedSnapshot);  // needs resync
}

TEST_CASE("recon: malformed u < U is a gap", "[recon]") {
  DepthSnapshot snap;
  snap.last_update_id = 1000;
  snap.bids.push_back(DepthLevel{100, 1});
  snap.asks.push_back(DepthLevel{200, 1});
  Reconstructor r;
  std::vector<MarketEvent> out;
  r.OnSnapshot(snap, 0, out);
  REQUIRE(Feed(r, Group(1001, 1005)) == ReconAction::kEmitAndGoLive);
  REQUIRE(Feed(r, Group(1006, 1004)) == ReconAction::kEmit);  // U ok; u<U is odd but seq-valid
  // Next must continue from u=1004 -> expects 1005.
  REQUIRE(Feed(r, Group(1005, 1008)) == ReconAction::kEmit);
}

// ---- Fixture-driven: bootstrap from a mid-recording snapshot, then a gap. ----

namespace {

std::vector<std::vector<MarketEvent>> LoadGroups(const std::string& path, const SymbolFilters& f) {
  std::vector<std::vector<MarketEvent>> groups;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    std::vector<MarketEvent> g;
    if (ParseDepthDiff(line, f, 0, g) && !g.empty()) groups.push_back(std::move(g));
  }
  return groups;
}

DepthSnapshot SnapshotFromBook(const L2Book& book) {
  DepthSnapshot s;
  s.last_update_id = book.LastUpdateId();
  for (std::size_t i = 0; i < book.NumBids(); ++i) {
    const auto l = book.DepthAt(Side::kBid, i);
    s.bids.push_back(DepthLevel{l->px_ticks, l->qty_lots});
  }
  for (std::size_t i = 0; i < book.NumAsks(); ++i) {
    const auto l = book.DepthAt(Side::kAsk, i);
    s.asks.push_back(DepthLevel{l->px_ticks, l->qty_lots});
  }
  return s;
}

}  // namespace

TEST_CASE("recon: fixture bootstrap + gap + resync reproduces full replay", "[recon][replay]") {
  const std::string dir = ASMM_DATA_DIR;
  const std::string jsonl = dir + "/depth_btcusdt.jsonl";
  {
    std::ifstream probe(jsonl);
    if (!probe) SKIP("no recorded fixture — run analysis/record_depth.py");
  }
  const SymbolFilters f{2, 5};
  const auto groups = LoadGroups(jsonl, f);
  REQUIRE(groups.size() > 20);

  // Truth: full replay from empty.
  L2Book truth;
  for (const auto& g : groups)
    for (const auto& ev : g) truth.ApplyEvent(ev);

  // Build a mid-recording snapshot from a half-replay.
  const std::size_t half = groups.size() / 2;
  L2Book mid;
  for (std::size_t i = 0; i < half; ++i)
    for (const auto& ev : groups[i]) mid.ApplyEvent(ev);
  const DepthSnapshot snap = SnapshotFromBook(mid);

  // Reconstruct: snapshot at `half`, then feed every group from the start.
  Reconstructor r;
  L2Book book;
  std::vector<MarketEvent> snap_events;
  r.OnSnapshot(snap, 0, snap_events);
  for (const auto& ev : snap_events) {
    if (ev.flags & kFlagSnapshotStart) book.Clear();
    book.ApplyEvent(ev);
  }

  bool went_live = false;
  for (const auto& g : groups) {
    const ReconAction a = r.OnDiffGroup(std::span<const MarketEvent>(g));
    if (a == ReconAction::kDrop) continue;
    if (a == ReconAction::kEmitAndGoLive) went_live = true;
    REQUIRE((a == ReconAction::kEmit || a == ReconAction::kEmitAndGoLive || went_live));
    if (a == ReconAction::kEmit || a == ReconAction::kEmitAndGoLive)
      for (const auto& ev : g) book.ApplyEvent(ev);
  }
  REQUIRE(went_live);

  // Reconstructed top-of-book must equal the full-replay truth.
  REQUIRE(book.BestBid()->px_ticks == truth.BestBid()->px_ticks);
  REQUIRE(book.BestBid()->qty_lots == truth.BestBid()->qty_lots);
  REQUIRE(book.BestAsk()->px_ticks == truth.BestAsk()->px_ticks);
  REQUIRE(book.BestAsk()->qty_lots == truth.BestAsk()->qty_lots);

  // Gap injection: bootstrap again, go live, then skip one group.
  Reconstructor r2;
  std::vector<MarketEvent> tmp;
  r2.OnSnapshot(snap, 0, tmp);
  std::optional<std::size_t> live_at;
  for (std::size_t i = 0; i < groups.size(); ++i) {
    if (r2.OnDiffGroup(std::span<const MarketEvent>(groups[i])) == ReconAction::kEmitAndGoLive) {
      live_at = i;
      break;
    }
  }
  REQUIRE(live_at.has_value());
  REQUIRE(*live_at + 2 < groups.size());
  // Skip groups[live_at+1]; feeding groups[live_at+2] must be detected as a gap.
  REQUIRE(r2.OnDiffGroup(std::span<const MarketEvent>(groups[*live_at + 2])) == ReconAction::kGap);
}
