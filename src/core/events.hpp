#pragma once

#include <type_traits>

#include "core/types.hpp"

namespace asmm {

enum class EventKind : u8 { kDepthDiff = 0, kTrade = 1, kSnapshot = 2 };

struct DepthLevel {
  i64 px_ticks;
  i64 qty_lots;
};
static_assert(sizeof(DepthLevel) == 16);

// Shared bid+ask level budget per event. Diffs larger than this fragment into
// multiple chained events (see kFlagContinuation).
inline constexpr u8 kMaxLevelsPerEvent = 64;

// Flag bits.
inline constexpr u8 kFlagContinuation = 0x01;  // more fragments of the same U/u follow
inline constexpr u8 kFlagSnapshotStart =
    0x02;  // first fragment of a snapshot: Clear() the book first

// Fixed-size POD carried through the SPSC queue. Bids occupy levels[0, num_bids);
// asks occupy levels[num_bids, num_bids + num_asks). All integer ticks/lots.
struct MarketEvent {
  u64 seq;              // local monotonic ingest sequence (producer-assigned)
  i64 ts_recv_ns;       // steady_clock at parse time
  i64 ts_exchange_ms;   // Binance "E" wall-clock ms — a RECORDED field, so replay-safe
                        // for deterministic time sampling (e.g. sigma). Never NowNs().
  u64 first_update_id;  // Binance "U"
  u64 final_update_id;  // Binance "u"
  u64 prev_update_id;   // Binance "pu" (futures only; 0 on spot, reserved)
  EventKind kind;
  u8 num_bids;
  u8 num_asks;
  u8 flags;
  u32 _pad;
  DepthLevel levels[kMaxLevelsPerEvent];
};

static_assert(std::is_trivially_copyable_v<MarketEvent>);
static_assert(std::is_standard_layout_v<MarketEvent>);
static_assert(sizeof(MarketEvent) == 56 + sizeof(DepthLevel) * kMaxLevelsPerEvent);

}  // namespace asmm
