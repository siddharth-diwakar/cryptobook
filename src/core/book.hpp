#pragma once

#include <array>
#include <cstddef>
#include <optional>

#include "core/events.hpp"
#include "core/types.hpp"

namespace asmm {

struct Level {
  i64 px_ticks;
  i64 qty_lots;
};

// L2 order book reconstructed from depth diffs. Two fixed-size arrays, no heap.
//
// Storage is WORST-price-first so top-of-book inserts/deletes — where nearly all
// churn happens — move a near-zero tail (see docs/DECISIONS.md). Bids are stored
// ascending, asks descending, so the best level is always the last active
// element and best bid/ask are O(1). A best-first layout would memmove the whole
// ~80 KB tail on every top insert (~3-8 us), blowing the p99 < 5 us target.
class L2Book {
 public:
  static constexpr std::size_t kMaxLevels = 5000;

  // Apply one level update for one side. qty_lots == 0 deletes the level.
  ApplyResult Apply(Side side, i64 px_ticks, i64 qty_lots);

  // Apply every level carried by an event (bids then asks) and advance the
  // last-update bookkeeping.
  void ApplyEvent(const MarketEvent& ev);

  std::optional<Level> BestBid() const;
  std::optional<Level> BestAsk() const;

  // bid_px + ask_px in ticks (x2 to avoid a half-tick fraction). nullopt if
  // either side is empty.
  std::optional<i64> MidX2() const;

  // Size-weighted mid: (bid_px*ask_qty + ask_px*bid_qty)/(bid_qty+ask_qty).
  // nullopt if either side is empty or top sizes sum to zero.
  std::optional<double> Microprice() const;

  // n-th level from the top (n == 0 is best). nullopt if fewer than n+1 levels.
  std::optional<Level> DepthAt(Side side, std::size_t n) const;

  std::size_t NumBids() const { return num_bids_; }
  std::size_t NumAsks() const { return num_asks_; }
  i64 LastUpdateTsNs() const { return last_update_ts_ns_; }
  u64 LastUpdateId() const { return last_update_id_; }

 private:
  std::array<Level, kMaxLevels> bids_{};  // ascending px: best == bids_[num_bids_-1]
  std::array<Level, kMaxLevels> asks_{};  // descending px: best == asks_[num_asks_-1]
  std::size_t num_bids_{0};
  std::size_t num_asks_{0};
  i64 last_update_ts_ns_{0};
  u64 last_update_id_{0};
};

}  // namespace asmm
