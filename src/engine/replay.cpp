#include "engine/replay.hpp"

#include "engine/gap_verifier.hpp"

namespace asmm {

DecisionRecord MakeDecision(const L2Book& book, u64 final_update_id, bool book_live, bool had_gap) {
  DecisionRecord d{};
  d.final_update_id = final_update_id;
  if (const auto bb = book.BestBid()) {
    d.best_bid_px = bb->px_ticks;
    d.best_bid_qty = bb->qty_lots;
  }
  if (const auto ba = book.BestAsk()) {
    d.best_ask_px = ba->px_ticks;
    d.best_ask_qty = ba->qty_lots;
  }
  if (const auto m = book.MidX2()) d.mid_x2 = *m;
  d.num_bids = static_cast<u32>(book.NumBids());
  d.num_asks = static_cast<u32>(book.NumAsks());
  d.book_live = book_live ? 1 : 0;
  d.had_gap = had_gap ? 1 : 0;
  return d;
}

std::vector<DecisionRecord> ReplayEvents(std::span<const MarketEvent> events) {
  L2Book book;
  GapVerifier verifier;
  bool book_live = false;
  std::vector<DecisionRecord> out;
  out.reserve(events.size());

  for (const auto& ev : events) {
    const bool ok = verifier.Check(ev);
    if (ev.flags & kFlagSnapshotStart) {
      book.Clear();
      book_live = false;
    }
    book.ApplyEvent(ev);
    if (ev.kind == EventKind::kDepthDiff) book_live = true;
    out.push_back(MakeDecision(book, ev.final_update_id, book_live, /*had_gap=*/!ok));
  }
  return out;
}

std::vector<DecisionRecord> ReplayLogFile(const std::string& in_path) {
  EventLogReader reader(in_path);
  std::vector<MarketEvent> events;
  u32 type = 0;
  MarketEvent ev{};
  DecisionRecord d{};
  while (reader.Next(type, ev, d)) {
    if (type == kRecMarketEvent) events.push_back(ev);
  }
  return ReplayEvents(std::span<const MarketEvent>(events));
}

void WriteEventsToLog(const std::string& path, std::span<const MarketEvent> events) {
  EventLogWriter writer(path);
  for (const auto& ev : events) writer.WriteMarketEvent(ev);
  writer.Close();
}

}  // namespace asmm
