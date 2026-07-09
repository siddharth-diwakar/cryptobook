#pragma once

#include <span>

#include "core/book.hpp"
#include "core/events.hpp"
#include "core/spsc_queue.hpp"
#include "core/types.hpp"
#include "exchange/binance_depth.hpp"

namespace asmm {

// A REST depth snapshot for cross-checking (top levels only; limit=100 -> 128).
struct CrossCheckMsg {
  u64 last_update_id;
  u32 num_bids;
  u32 num_asks;
  DepthLevel bids[128];
  DepthLevel asks[128];
};

using CrossCheckQueue = SpscQueue<CrossCheckMsg, 4>;

enum class CrossCheckResult { kMatch, kMismatch, kSkipped };

// Pack a parsed REST snapshot into a fixed-size CrossCheckMsg. Returns false if
// it has more levels than the message can hold.
bool ToCrossCheckMsg(const DepthSnapshot& snap, CrossCheckMsg& out);

// Verify the live book against a REST snapshot exactly. Seeds `scratch` from the
// snapshot (at its lastUpdateId L), replays `recent` engine events whose ids run
// past L (starting at the bracket group U <= L+1 <= u), then compares the top
// `levels` (integer ticks -> exact equality). kSkipped if the recent ring does
// not reach back to L or a resync occurred in the window. `scratch` is a caller-
// owned reusable book (Cleared internally) to avoid per-call allocation.
CrossCheckResult CrossCheck(const L2Book& live, const CrossCheckMsg& msg,
                            std::span<const MarketEvent> recent, int levels, L2Book& scratch);

}  // namespace asmm
