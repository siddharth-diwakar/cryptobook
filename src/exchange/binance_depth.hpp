#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "core/events.hpp"
#include "core/types.hpp"

namespace asmm {

// Decimal precision of a symbol's price/quantity, i.e. number of decimal places
// implied by tickSize/stepSize. BTCUSDT: px_scale=2 (0.01), qty_scale=5 (1e-5).
struct SymbolFilters {
  int px_scale;
  int qty_scale;
};

// A REST depth snapshot.
struct DepthSnapshot {
  u64 last_update_id;
  std::vector<DepthLevel> bids;
  std::vector<DepthLevel> asks;
};

// Exact decimal-string -> scaled integer (value * 10^scale). Trailing zeros
// beyond `scale` are allowed (Binance pads); any non-zero digit beyond `scale`,
// malformed input, or overflow returns false. No floating point involved.
bool DecimalToScaled(std::string_view s, int scale, i64& out);

// Parse a Binance "depthUpdate" diff JSON object into one or more MarketEvents.
// Diffs whose total level count exceeds kMaxLevelsPerEvent fragment into chained
// events sharing U/u/ts (all but the last flagged kFlagContinuation). Appends to
// `out`. Returns false on any parse/conversion error (out may be partially
// populated on failure).
bool ParseDepthDiff(std::string_view json, const SymbolFilters& filters, i64 ts_recv_ns,
                    std::vector<MarketEvent>& out);

// Parse a REST /api/v3/depth snapshot into levels + lastUpdateId.
bool ParseDepthSnapshot(std::string_view json, const SymbolFilters& filters, DepthSnapshot& out);

}  // namespace asmm
