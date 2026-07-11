#pragma once

#include <cstdio>
#include <string>
#include <type_traits>
#include <vector>

#include "core/events.hpp"
#include "core/types.hpp"

namespace asmm {

// Deterministic, integer-only decision output derived from book state after one
// input event. NO floating point (microprice is excluded) so the decision stream
// is byte-identical across machines/compilers — the Phase 3 acceptance.
struct DecisionRecord {
  u64 final_update_id;  // input correlation
  i64 best_bid_px;      // 0 if that side is empty
  i64 best_bid_qty;
  i64 best_ask_px;
  i64 best_ask_qty;
  i64 mid_x2;  // bid+ask ticks; 0 if either side empty
  u32 num_bids;
  u32 num_asks;
  u8 book_live;
  u8 had_gap;  // the redundant verifier fired on this event
  u8 _pad[6];
};
static_assert(sizeof(DecisionRecord) == 64);
static_assert(std::is_trivially_copyable_v<DecisionRecord>);

enum RecordType : u32 { kRecMarketEvent = 1, kRecDecision = 2 };

// Per-record framing: 8 bytes, little-endian.
struct RecordHeader {
  u32 type;
  u32 len;  // payload bytes
};
static_assert(sizeof(RecordHeader) == 8);

// File header: magic + version + compiled struct sizes (cross-machine / version
// compatibility guard checked by the reader).
struct LogHeader {
  char magic[4];  // "ASML"
  u32 version;
  u32 market_event_size;
  u32 decision_size;
};
static_assert(sizeof(LogHeader) == 16);

inline constexpr u32 kLogVersion = 1;

// Append-only binary log writer. Buffered; flush on timer/close. Not thread-safe
// — the single engine thread owns it.
class EventLogWriter {
 public:
  explicit EventLogWriter(const std::string& path);  // throws on open failure
  ~EventLogWriter();

  EventLogWriter(const EventLogWriter&) = delete;
  EventLogWriter& operator=(const EventLogWriter&) = delete;

  void WriteMarketEvent(const MarketEvent& ev);
  void WriteDecision(const DecisionRecord& d);
  void Flush();  // write buffer to disk + fflush
  void Close();

 private:
  void Append(const void* p, std::size_t n);
  void WriteRecord(u32 type, const void* payload, u32 len);

  std::FILE* f_{nullptr};
  std::vector<char> buf_;
};

// Sequential reader. Validates the file header against the compiled struct sizes.
class EventLogReader {
 public:
  explicit EventLogReader(const std::string& path);  // throws on open/format error
  ~EventLogReader();

  EventLogReader(const EventLogReader&) = delete;
  EventLogReader& operator=(const EventLogReader&) = delete;

  // Reads the next record. Returns false at EOF. out_type says which of ev/d was
  // filled. Unknown record types are skipped.
  bool Next(u32& out_type, MarketEvent& ev, DecisionRecord& d);

 private:
  std::FILE* f_{nullptr};
};

}  // namespace asmm
