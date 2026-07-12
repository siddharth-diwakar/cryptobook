#pragma once

#include <type_traits>

#include "core/types.hpp"

namespace asmm {

// Strategy tick output — the quoting decision (integer/fixed-point so a run is
// self-consistent; sigma stored as fixed-point micro-units for logging).
struct QuoteRecord {
  u64 final_update_id;
  i64 bid_px;  // 0 if the bid is pulled
  i64 bid_qty;
  i64 ask_px;  // 0 if the ask is pulled
  i64 ask_qty;
  i64 mid_x2;
  i64 inventory_lots;
  i64 sigma_p_micro;  // sigma_p (ticks/sqrt-day) * 1e6
  u8 quoting;         // 1 if >= 1 side active
  u8 one_sided;       // 1 if inventory limit forced one side
  u8 pulled;          // 1 if guards pulled quotes
  u8 _pad[5];
};
static_assert(sizeof(QuoteRecord) == 72);
static_assert(std::is_trivially_copyable_v<QuoteRecord>);

// A simulated (paper) fill.
struct FillRecord {
  u64 final_update_id;
  i64 px_ticks;
  i64 qty_lots;  // signed: + = buy (bid filled), - = sell (ask filled)
  i64 fee_units;
  i64 inventory_after;
  i64 cash_after;  // integer cash, units of 10^-(px_scale+qty_scale) USDT
  u8 side;         // 0 = bid/buy, 1 = ask/sell
  u8 _pad[7];
};
static_assert(sizeof(FillRecord) == 56);
static_assert(std::is_trivially_copyable_v<FillRecord>);

}  // namespace asmm
