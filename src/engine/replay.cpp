#include "engine/replay.hpp"

#include <cstdlib>

#include "engine/gap_verifier.hpp"
#include "strategy/strategy.hpp"

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

namespace {
std::vector<MarketEvent> ReadLogEvents(const std::string& in_path) {
  EventLogReader reader(in_path);
  std::vector<MarketEvent> events;
  u32 type = 0;
  MarketEvent ev{};
  DecisionRecord d{};
  while (reader.Next(type, ev, d)) {
    if (type == kRecMarketEvent) events.push_back(ev);
  }
  return events;
}
}  // namespace

std::vector<DecisionRecord> ReplayLogFile(const std::string& in_path) {
  const auto events = ReadLogEvents(in_path);
  return ReplayEvents(std::span<const MarketEvent>(events));
}

void WriteEventsToLog(const std::string& path, std::span<const MarketEvent> events) {
  EventLogWriter writer(path);
  for (const auto& ev : events) writer.WriteMarketEvent(ev);
  writer.Close();
}

StrategyReplay ReplayStrategy(std::span<const MarketEvent> events, const StrategyParams& p) {
  L2Book book;
  Strategy strat(p);
  bool book_live = false;
  StrategyReplay out;
  i64 inv_sum = 0;
  u64 quote_count = 0;

  for (const auto& ev : events) {
    ++out.summary.events;
    if (ev.flags & kFlagSnapshotStart) {
      book.Clear();
      book_live = false;
      strat.OnResync();
    }
    book.ApplyEvent(ev);
    if (ev.kind == EventKind::kDepthDiff) book_live = true;

    if (book_live && !(ev.flags & kFlagContinuation)) {
      const StrategyOutput o = strat.OnEvent(book, ev);
      if (o.has_quote) {
        out.quotes.push_back(o.quote);
        ++out.summary.quotes;
        inv_sum += o.quote.inventory_lots;
        ++quote_count;
        if (o.quote.one_sided) ++out.summary.one_sided_ticks;
        if (o.quote.pulled) ++out.summary.pulled_ticks;
        if (o.quote.bid_px > 0 && o.quote.ask_px > 0 && o.quote.bid_px >= o.quote.ask_px) {
          ++out.summary.cross_violations;
        }
      }
      for (const auto& f : o.fills) {
        out.fills.push_back(f);
        ++out.summary.fills;
      }
      const i64 q = std::llabs(strat.inventory());
      if (q > out.summary.max_abs_inventory) out.summary.max_abs_inventory = q;
    }
  }

  out.summary.final_inventory = strat.inventory();
  out.summary.final_cash_units = strat.cash_units();
  out.summary.mean_inventory =
      quote_count ? static_cast<double>(inv_sum) / static_cast<double>(quote_count) : 0.0;
  return out;
}

StrategyReplay ReplayStrategyFile(const std::string& in_path, const StrategyParams& p) {
  const auto events = ReadLogEvents(in_path);
  return ReplayStrategy(std::span<const MarketEvent>(events), p);
}

}  // namespace asmm
