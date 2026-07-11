#pragma once

#include <span>
#include <string>
#include <vector>

#include "core/book.hpp"
#include "core/events.hpp"
#include "engine/event_log.hpp"

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

}  // namespace asmm
