#pragma once

#include <atomic>
#include <vector>

#include "core/book.hpp"
#include "core/types.hpp"
#include "engine/crosscheck.hpp"
#include "engine/event_log.hpp"
#include "engine/gap_verifier.hpp"
#include "engine/market_data_thread.hpp"
#include "strategy/strategy.hpp"

namespace asmm {

struct EngineCounters {
  u64 events_applied = 0;
  u64 snapshots_applied = 0;
  u64 stale_episodes = 0;
  u64 crosscheck_ok = 0;
  u64 crosscheck_fail = 0;
  u64 crosscheck_skipped = 0;
};

// The single writer of the order book. Drains queue A, applies events, runs the
// redundant GapVerifier, stale detection, and (if a cross-check queue is given)
// the periodic REST cross-check against a recent-event ring. No locks.
class Engine {
 public:
  Engine(MarketQueue& in, i64 stale_threshold_ms, CrossCheckQueue* cc_in = nullptr,
         int crosscheck_levels = 20, EventLogWriter* log = nullptr, Strategy* strat = nullptr);

  void Run(std::atomic<bool>& stop);

  const L2Book& book() const { return book_; }
  const EngineCounters& counters() const { return counters_; }
  u64 undetected_gaps() const { return verifier_.undetected_gaps(); }
  bool book_live() const { return book_live_; }

  // Strategy display snapshot (published by the engine thread; safe cross-thread).
  i64 strat_inventory() const { return strat_inv_.load(std::memory_order_relaxed); }
  i64 strat_sigma_micro() const { return strat_sigma_micro_.load(std::memory_order_relaxed); }
  u64 strat_fills() const { return strat_fills_.load(std::memory_order_relaxed); }

 private:
  bool Drain();
  void RecordRing(const MarketEvent& ev);
  void HandleCrossChecks();

  MarketQueue& in_;
  CrossCheckQueue* cc_in_;
  int crosscheck_levels_;
  EventLogWriter* log_;
  Strategy* strat_;
  i64 last_flush_ns_{0};
  L2Book book_;
  L2Book scratch_;  // reused by cross-check; avoids per-call allocation
  GapVerifier verifier_;
  std::vector<MarketEvent> ring_;  // recent applied events (trimmed)
  i64 stale_threshold_ns_;
  bool book_live_{false};
  bool stale_{false};
  EngineCounters counters_;

  // Display-only snapshots published for the status thread (not read by logic).
  std::atomic<i64> strat_inv_{0};
  std::atomic<i64> strat_sigma_micro_{0};
  std::atomic<u64> strat_fills_{0};
};

}  // namespace asmm
