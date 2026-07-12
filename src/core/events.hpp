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

// ----------------------------------------------------------------------------
// Phase 5 — order flow. All fixed-size integer PODs, like MarketEvent, so they
// cross the SPSC queues and land in the binary event log byte-identically.
// px/qty are the same scaled ticks/lots the book uses (see SymbolFilters).
// ----------------------------------------------------------------------------

// Engine -> order-gateway command.
enum class CmdKind : u8 { kPlace = 0, kCancel = 1, kCancelAll = 2 };

struct OrderCommand {
  u64 seq;        // engine-assigned monotonic sequence (deterministic, not time-derived)
  u64 client_id;  // maps to Binance newClientOrderId; 0 for kCancelAll
  i64 px_ticks;   // kPlace only; 0 otherwise
  i64 qty_lots;   // kPlace only; 0 otherwise
  i64 ts_ns;      // steady_clock at emit — logging/latency ONLY, never a decision input
  CmdKind kind;
  Side side;  // kPlace side; ignored for cancel / cancel-all
  u8 _pad[6];
};
static_assert(std::is_trivially_copyable_v<OrderCommand>);
static_assert(std::is_standard_layout_v<OrderCommand>);
static_assert(sizeof(OrderCommand) == 48);

// User-data stream / gateway -> engine. Derived from a Binance executionReport
// (or a local gateway reject). ts_exchange_ms ("T") is the RECORDED, replay-safe
// time; ts_recv_ns is steady_clock at parse and is logging-only.
enum class ExecKind : u8 {
  kAck = 0,       // NEW acknowledged, resting
  kPartial = 1,   // PARTIALLY_FILLED
  kFilled = 2,    // FILLED (terminal)
  kCanceled = 3,  // CANCELED / EXPIRED-by-cancel (terminal)
  kRejected = 4,  // REJECTED (e.g. LIMIT_MAKER would cross) (terminal)
  kExpired = 5,   // EXPIRED (terminal)
};

// Which asset a commission ("N") was charged in, relative to the traded symbol.
enum class CommAsset : u8 { kBase = 0, kQuote = 1, kOther = 2 };

struct ExecEvent {
  u64 seq;                // producer-assigned ingest sequence
  u64 client_id;          // parsed from clientOrderId "c" (0 if unrecognized)
  u64 exchange_order_id;  // "i"
  i64 last_px_ticks;      // "L" last executed price
  i64 last_qty_lots;      // "l" last executed qty
  i64 cum_qty_lots;       // "z" cumulative filled qty (guards double-counting)
  i64 fee_units;          // "n" scaled to cash units (kQuote) or lots (kBase)
  i64 ts_exchange_ms;     // "T" transaction time — RECORDED, replay-safe
  i64 ts_recv_ns;         // steady_clock at parse — logging only
  ExecKind kind;          // mapped from status "X"
  Side side;              // "S"
  CommAsset comm_asset;   // from commission asset "N"
  u8 reject_reason;       // coded reason (0 = none)
  u8 _pad[4];
};
static_assert(std::is_trivially_copyable_v<ExecEvent>);
static_assert(std::is_standard_layout_v<ExecEvent>);
static_assert(sizeof(ExecEvent) == 80);

// A single open order from a reconcile snapshot (GET /openOrders).
struct OpenOrderRow {
  u64 client_id;
  u64 exchange_order_id;
  i64 px_ticks;
  i64 orig_qty_lots;
  i64 cum_qty_lots;
  Side side;
  u8 _pad[7];
};
static_assert(std::is_trivially_copyable_v<OpenOrderRow>);
static_assert(std::is_standard_layout_v<OpenOrderRow>);
static_assert(sizeof(OpenOrderRow) == 48);

// Exchange truth pushed to the engine on (re)connect and periodically, so the
// order state machine can never silently desync. A snapshot with more than
// kMaxReconcileRows open orders forces a fresh reconcile (num_open records the
// true count even when it exceeds the array).
inline constexpr u32 kMaxReconcileRows = 16;

struct ReconcileReport {
  i64 base_free_lots;
  i64 base_locked_lots;
  i64 quote_free_units;
  i64 quote_locked_units;
  i64 ts_exchange_ms;
  u32 num_open;  // true open-order count (may exceed kMaxReconcileRows)
  u32 _pad;
  OpenOrderRow rows[kMaxReconcileRows];
};
static_assert(std::is_trivially_copyable_v<ReconcileReport>);
static_assert(std::is_standard_layout_v<ReconcileReport>);
static_assert(sizeof(ReconcileReport) == 48 + sizeof(OpenOrderRow) * kMaxReconcileRows);

}  // namespace asmm
