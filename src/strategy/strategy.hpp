#pragma once

#include <vector>

#include "core/book.hpp"
#include "core/events.hpp"
#include "strategy/paper_fills.hpp"
#include "strategy/params.hpp"
#include "strategy/records.hpp"
#include "strategy/sigma_ewma.hpp"

namespace asmm {

struct StrategyOutput {
  bool has_quote = false;
  QuoteRecord quote{};
  std::vector<FillRecord> fills;  // 0..2 per event
};

// Per-event A-S paper-trading strategy (docs/MODEL.md, ARCHITECTURE §6). Pure and
// deterministic: driven only by the book and recorded MarketEvent fields, never
// NowNs(). One instance lives on the engine thread (single writer).
class Strategy {
 public:
  // live_mode=false (default): paper — simulate fills against our own book.
  // live_mode=true: real orders — fills arrive via OnExec; SimulateFills is off.
  explicit Strategy(const StrategyParams& p, bool live_mode = false);

  // Update sigma, apply guards, compute/hysteresis-filter quotes, apply inventory
  // risk limits, and (paper mode only) simulate fills. Call once per applied,
  // non-continuation event when the book is live.
  StrategyOutput OnEvent(const L2Book& book, const MarketEvent& ev);

  // Live mode: apply a real exchange fill to inventory/cash/PnL.
  void OnExec(const ExecEvent& e);

  // Snapshot resync: pull quotes, restart sigma mid-continuity.
  void OnResync();

  i64 inventory() const { return paper_.inventory(); }
  i64 cash_units() const { return paper_.cash_units(); }
  double sigma_r() const { return sigma_.sigma_r(); }
  u64 fill_count() const { return fill_count_; }

 private:
  bool MinNotionalOk(i64 px_ticks, i64 qty_lots) const;

  StrategyParams p_;
  bool live_mode_;
  SigmaEwma sigma_;
  PaperBook paper_;
  i64 working_bid_{0};
  i64 working_ask_{0};
  bool have_working_{false};
  u64 fill_count_{0};
};

}  // namespace asmm
