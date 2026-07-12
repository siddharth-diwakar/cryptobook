#include "strategy/paper_fills.hpp"

#include <algorithm>
#include <cmath>

namespace asmm {

i64 PaperBook::FeeUnits(i64 px_ticks, i64 qty_lots) const {
  // Notional (in cash units) = px_ticks * qty_lots. Fee rounded up (against us).
  const double notional = static_cast<double>(px_ticks) * static_cast<double>(qty_lots);
  return static_cast<i64>(std::ceil(notional * fee_bps_ / 10000.0));
}

int PaperBook::SimulateFills(const L2Book& book, u64 final_update_id,
                             std::vector<FillRecord>& out) {
  int n = 0;

  // Buy: our resting bid is hit when the best ask reaches or crosses it.
  if (bid_.active && bid_.qty_lots > 0) {
    if (const auto ba = book.BestAsk(); ba && ba->px_ticks <= bid_.px_ticks) {
      const i64 fill = std::min(bid_.qty_lots, ba->qty_lots);
      if (fill > 0) {
        const i64 fee = FeeUnits(bid_.px_ticks, fill);
        q_lots_ += fill;
        cash_units_ -= bid_.px_ticks * fill;  // pay at our bid price
        cash_units_ -= fee;
        bid_.qty_lots -= fill;
        if (bid_.qty_lots == 0) bid_.active = false;
        out.push_back(FillRecord{final_update_id,
                                 bid_.px_ticks,
                                 fill,
                                 fee,
                                 q_lots_,
                                 cash_units_,
                                 /*side=*/0,
                                 {}});
        ++n;
      }
    }
  }

  // Sell: our resting ask is hit when the best bid reaches or crosses it.
  if (ask_.active && ask_.qty_lots > 0) {
    if (const auto bb = book.BestBid(); bb && bb->px_ticks >= ask_.px_ticks) {
      const i64 fill = std::min(ask_.qty_lots, bb->qty_lots);
      if (fill > 0) {
        const i64 fee = FeeUnits(ask_.px_ticks, fill);
        q_lots_ -= fill;
        cash_units_ += ask_.px_ticks * fill;
        cash_units_ -= fee;
        ask_.qty_lots -= fill;
        if (ask_.qty_lots == 0) ask_.active = false;
        out.push_back(FillRecord{final_update_id,
                                 ask_.px_ticks,
                                 -fill,
                                 fee,
                                 q_lots_,
                                 cash_units_,
                                 /*side=*/1,
                                 {}});
        ++n;
      }
    }
  }

  return n;
}

}  // namespace asmm
