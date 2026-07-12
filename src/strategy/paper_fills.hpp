#pragma once

#include <vector>

#include "core/book.hpp"
#include "core/types.hpp"
#include "strategy/records.hpp"

namespace asmm {

struct PaperQuote {
  i64 px_ticks{0};
  i64 qty_lots{0};
  bool active{false};
};

// Paper order / inventory / cash state. Cash is an integer in units of
// 10^-(px_scale+qty_scale) USDT, so px_ticks*qty_lots is exact.
class PaperBook {
 public:
  explicit PaperBook(double maker_fee_bps) : fee_bps_(maker_fee_bps) {}

  void SetQuotes(PaperQuote bid, PaperQuote ask) {
    bid_ = bid;
    ask_ = ask;
  }
  void PullQuotes() {
    bid_.active = false;
    ask_.active = false;
  }

  // Fill against the current book: our bid fills when best ask <= our bid (buy);
  // our ask fills when best bid >= our ask (sell). Partial fills capped at the
  // displayed opposite-side qty. Appends FillRecords; returns the count.
  int SimulateFills(const L2Book& book, u64 final_update_id, std::vector<FillRecord>& out);

  i64 inventory() const { return q_lots_; }
  i64 cash_units() const { return cash_units_; }
  const PaperQuote& bid() const { return bid_; }
  const PaperQuote& ask() const { return ask_; }

 private:
  i64 FeeUnits(i64 px_ticks, i64 qty_lots) const;

  double fee_bps_;
  PaperQuote bid_;
  PaperQuote ask_;
  i64 q_lots_{0};
  i64 cash_units_{0};
};

}  // namespace asmm
