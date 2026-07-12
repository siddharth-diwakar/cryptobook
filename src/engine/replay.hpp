#pragma once

#include <span>
#include <string>
#include <vector>

#include "core/book.hpp"
#include "core/events.hpp"
#include "engine/event_log.hpp"
#include "strategy/params.hpp"
#include "strategy/records.hpp"

namespace asmm {

// Build the deterministic decision for the current book state. Integer-only.
DecisionRecord MakeDecision(const L2Book& book, u64 final_update_id, bool book_live, bool had_gap);

// Deterministically replay input events through a fresh book (single-threaded,
// NO wall-clock), returning one DecisionRecord per event. This is the canonical
// replay: same input -> identical output, always. Mirrors the engine's
// book-application logic (Clear on snapshot-start, gap verification) without any
// threading/clock/network.
std::vector<DecisionRecord> ReplayEvents(std::span<const MarketEvent> events);

// Read all MarketEvent records from a log file and replay them.
std::vector<DecisionRecord> ReplayLogFile(const std::string& in_path);

// Write events to a new log file (MarketEvent records only). Helper for building
// input logs from other sources (e.g. a recorded jsonl fixture).
void WriteEventsToLog(const std::string& path, std::span<const MarketEvent> events);

struct StrategySummary {
  u64 events = 0;
  u64 quotes = 0;
  u64 fills = 0;
  i64 final_inventory = 0;
  i64 max_abs_inventory = 0;
  double mean_inventory = 0.0;
  i64 final_cash_units = 0;
  i64 cross_violations = 0;  // emitted quotes with bid >= ask (must be 0)
  u64 one_sided_ticks = 0;
  u64 pulled_ticks = 0;
};

struct StrategyReplay {
  std::vector<QuoteRecord> quotes;
  std::vector<FillRecord> fills;
  StrategySummary summary;
};

// Deterministically replay the A-S strategy over input events (single-threaded,
// clock-free — same tick order the engine uses live). Same input + params ->
// identical quote/fill streams.
StrategyReplay ReplayStrategy(std::span<const MarketEvent> events, const StrategyParams& p);

// Read the MarketEvent records from a log file (ignores decision/quote/fill
// records) and run ReplayStrategy over them.
StrategyReplay ReplayStrategyFile(const std::string& in_path, const StrategyParams& p);

}  // namespace asmm
