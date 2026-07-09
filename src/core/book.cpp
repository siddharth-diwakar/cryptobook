#include "core/book.hpp"

#include <cstring>

namespace asmm {
namespace {

// Erase element at idx from [0, n), shifting the tail down. Requires idx < n.
void EraseAt(Level* arr, std::size_t& n, std::size_t idx) {
  std::memmove(arr + idx, arr + idx + 1, (n - idx - 1) * sizeof(Level));
  --n;
}

// Insert v at idx into [0, n), shifting the tail up. Requires n < capacity.
void InsertAt(Level* arr, std::size_t& n, std::size_t idx, const Level& v) {
  std::memmove(arr + idx + 1, arr + idx, (n - idx) * sizeof(Level));
  arr[idx] = v;
  ++n;
}

}  // namespace

ApplyResult L2Book::Apply(Side side, i64 px_ticks, i64 qty_lots) {
  Level* arr = (side == Side::kBid) ? bids_.data() : asks_.data();
  std::size_t& n = (side == Side::kBid) ? num_bids_ : num_asks_;

  // Index order runs worst -> best. Bids ascending (a<b), asks descending (a>b).
  const auto is_worse = [side](i64 a, i64 b) { return (side == Side::kBid) ? (a < b) : (a > b); };

  // lower_bound: first index whose price is not worse than px_ticks.
  std::size_t lo = 0;
  std::size_t hi = n;
  while (lo < hi) {
    const std::size_t mid = lo + (hi - lo) / 2;
    if (is_worse(arr[mid].px_ticks, px_ticks)) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  const std::size_t idx = lo;
  const bool found = (idx < n) && (arr[idx].px_ticks == px_ticks);

  if (found) {
    if (qty_lots == 0) {
      EraseAt(arr, n, idx);
    } else {
      arr[idx].qty_lots = qty_lots;
    }
    return ApplyResult::kOk;
  }

  if (qty_lots == 0) {
    return ApplyResult::kNoop;  // delete of an absent level
  }

  if (n == kMaxLevels) {
    // Full. arr[0] is the worst kept level. Drop the update if it is even worse;
    // otherwise evict the worst and insert (a rare full memmove).
    if (is_worse(px_ticks, arr[0].px_ticks)) {
      return ApplyResult::kDropped;
    }
    EraseAt(arr, n, 0);
    const std::size_t new_idx = (idx > 0) ? idx - 1 : 0;
    InsertAt(arr, n, new_idx, Level{px_ticks, qty_lots});
    return ApplyResult::kOk;
  }

  InsertAt(arr, n, idx, Level{px_ticks, qty_lots});
  return ApplyResult::kOk;
}

void L2Book::ApplyEvent(const MarketEvent& ev) {
  for (std::size_t i = 0; i < ev.num_bids; ++i) {
    Apply(Side::kBid, ev.levels[i].px_ticks, ev.levels[i].qty_lots);
  }
  for (std::size_t i = 0; i < ev.num_asks; ++i) {
    const std::size_t j = static_cast<std::size_t>(ev.num_bids) + i;
    Apply(Side::kAsk, ev.levels[j].px_ticks, ev.levels[j].qty_lots);
  }
  last_update_id_ = ev.final_update_id;
  last_update_ts_ns_ = ev.ts_recv_ns;
}

std::optional<Level> L2Book::BestBid() const {
  if (num_bids_ == 0) return std::nullopt;
  return bids_[num_bids_ - 1];
}

std::optional<Level> L2Book::BestAsk() const {
  if (num_asks_ == 0) return std::nullopt;
  return asks_[num_asks_ - 1];
}

std::optional<i64> L2Book::MidX2() const {
  if (num_bids_ == 0 || num_asks_ == 0) return std::nullopt;
  return bids_[num_bids_ - 1].px_ticks + asks_[num_asks_ - 1].px_ticks;
}

std::optional<double> L2Book::Microprice() const {
  if (num_bids_ == 0 || num_asks_ == 0) return std::nullopt;
  const Level b = bids_[num_bids_ - 1];
  const Level a = asks_[num_asks_ - 1];
  const double bq = static_cast<double>(b.qty_lots);
  const double aq = static_cast<double>(a.qty_lots);
  const double denom = bq + aq;
  if (denom == 0.0) return std::nullopt;
  return (static_cast<double>(b.px_ticks) * aq + static_cast<double>(a.px_ticks) * bq) / denom;
}

std::optional<Level> L2Book::DepthAt(Side side, std::size_t n) const {
  if (side == Side::kBid) {
    if (n >= num_bids_) return std::nullopt;
    return bids_[num_bids_ - 1 - n];
  }
  if (n >= num_asks_) return std::nullopt;
  return asks_[num_asks_ - 1 - n];
}

}  // namespace asmm
