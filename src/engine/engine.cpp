#include "engine/engine.hpp"

#include <spdlog/spdlog.h>

#include <chrono>
#include <span>
#include <thread>

#include "core/clock.hpp"
#include "engine/replay.hpp"

namespace asmm {

namespace {
constexpr std::size_t kRingCap = 1024;      // trim threshold
constexpr std::size_t kRingKeep = 512;      // events kept after a trim
constexpr i64 kLogFlushNs = 5'000'000'000;  // 5s
}  // namespace

Engine::Engine(MarketQueue& in, i64 stale_threshold_ms, CrossCheckQueue* cc_in,
               int crosscheck_levels, EventLogWriter* log, Strategy* strat)
    : in_(in),
      cc_in_(cc_in),
      crosscheck_levels_(crosscheck_levels),
      log_(log),
      strat_(strat),
      stale_threshold_ns_(stale_threshold_ms * 1'000'000) {
  ring_.reserve(kRingCap);
}

void Engine::RecordRing(const MarketEvent& ev) {
  ring_.push_back(ev);
  if (ring_.size() > kRingCap) {
    ring_.erase(ring_.begin(), ring_.begin() + (ring_.size() - kRingKeep));
  }
}

bool Engine::Drain() {
  MarketEvent ev;
  bool did_work = false;
  while (in_.try_pop(ev)) {
    did_work = true;
    const bool ok = verifier_.Check(ev);  // redundant gap detection (should never fire)

    if (ev.flags & kFlagSnapshotStart) {
      book_.Clear();
      ring_.clear();  // a resync invalidates the replay history
      ++counters_.snapshots_applied;
      book_live_ = false;
      if (strat_) strat_->OnResync();
    }
    book_.ApplyEvent(ev);
    RecordRing(ev);
    ++counters_.events_applied;
    if (ev.kind == EventKind::kDepthDiff) book_live_ = true;

    // Append input + derived decision to the replayable log (buffered).
    if (log_) {
      log_->WriteMarketEvent(ev);
      log_->WriteDecision(MakeDecision(book_, ev.final_update_id, book_live_, !ok));
    }

    // Strategy tick — same order as ReplayStrategy, so live == replay.
    if (strat_ && book_live_ && !(ev.flags & kFlagContinuation)) {
      const StrategyOutput o = strat_->OnEvent(book_, ev);
      if (log_) {
        if (o.has_quote) log_->WriteQuote(o.quote);
        for (const auto& f : o.fills) log_->WriteFill(f);
      }
      if (o.has_quote) {
        strat_inv_.store(o.quote.inventory_lots, std::memory_order_relaxed);
        strat_sigma_micro_.store(o.quote.sigma_p_micro, std::memory_order_relaxed);
      }
      strat_fills_.store(strat_->fill_count(), std::memory_order_relaxed);
    }
  }
  return did_work;
}

void Engine::HandleCrossChecks() {
  if (!cc_in_) return;
  CrossCheckMsg msg;
  while (cc_in_->try_pop(msg)) {
    const CrossCheckResult r =
        CrossCheck(book_, msg, std::span<const MarketEvent>(ring_), crosscheck_levels_, scratch_);
    switch (r) {
      case CrossCheckResult::kMatch:
        ++counters_.crosscheck_ok;
        spdlog::info("crosscheck OK (L={} top{} match)", msg.last_update_id, crosscheck_levels_);
        break;
      case CrossCheckResult::kMismatch:
        ++counters_.crosscheck_fail;
        spdlog::error("crosscheck MISMATCH (L={}) — book diverged from exchange",
                      msg.last_update_id);
        break;
      case CrossCheckResult::kSkipped:
        ++counters_.crosscheck_skipped;
        break;
    }
  }
}

void Engine::Run(std::atomic<bool>& stop) {
  while (!stop.load(std::memory_order_relaxed)) {
    const bool did_work = Drain();
    HandleCrossChecks();

    if (book_live_) {
      const i64 age = NowNs() - book_.LastUpdateTsNs();
      if (age > stale_threshold_ns_) {
        if (!stale_) {
          stale_ = true;
          ++counters_.stale_episodes;
          spdlog::warn("engine: book stale ({} ms since last update)", age / 1'000'000);
        }
      } else if (stale_) {
        stale_ = false;
        spdlog::info("engine: book fresh again");
      }
    }

    if (log_) {
      const i64 now = NowNs();
      if (now - last_flush_ns_ > kLogFlushNs) {
        log_->Flush();
        last_flush_ns_ = now;
      }
    }

    if (!did_work) std::this_thread::sleep_for(std::chrono::microseconds(200));
  }
  Drain();  // final drain
  if (log_) log_->Flush();
}

}  // namespace asmm
