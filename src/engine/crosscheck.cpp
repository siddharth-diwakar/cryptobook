#include "engine/crosscheck.hpp"

namespace asmm {

bool ToCrossCheckMsg(const DepthSnapshot& snap, CrossCheckMsg& out) {
  if (snap.bids.size() > 128 || snap.asks.size() > 128) return false;
  out.last_update_id = snap.last_update_id;
  out.num_bids = static_cast<u32>(snap.bids.size());
  out.num_asks = static_cast<u32>(snap.asks.size());
  for (std::size_t i = 0; i < snap.bids.size(); ++i) out.bids[i] = snap.bids[i];
  for (std::size_t i = 0; i < snap.asks.size(); ++i) out.asks[i] = snap.asks[i];
  return true;
}

CrossCheckResult CrossCheck(const L2Book& live, const CrossCheckMsg& msg,
                            std::span<const MarketEvent> recent, int levels, L2Book& scratch) {
  const u64 L = msg.last_update_id;

  // Seed the scratch book from the REST snapshot (its state at L).
  scratch.Clear();
  for (u32 i = 0; i < msg.num_bids; ++i)
    scratch.Apply(Side::kBid, msg.bids[i].px_ticks, msg.bids[i].qty_lots);
  for (u32 i = 0; i < msg.num_asks; ++i)
    scratch.Apply(Side::kAsk, msg.asks[i].px_ticks, msg.asks[i].qty_lots);

  // Replay recent engine events past L, starting at the bracket group
  // (U <= L+1 <= u), so scratch advances to the live book's current state.
  bool started = false;
  for (const auto& ev : recent) {
    if (!started) {
      if (ev.kind != EventKind::kDepthDiff) continue;
      if (ev.final_update_id <= L) continue;  // still at/before the snapshot
      if (ev.first_update_id <= L + 1) {
        started = true;  // the bracket group
      } else {
        return CrossCheckResult::kSkipped;  // ring too short to reach L
      }
    }
    if (started) {
      if (ev.kind == EventKind::kSnapshot) return CrossCheckResult::kSkipped;  // resync in window
      scratch.ApplyEvent(ev);
    }
  }
  if (!started) return CrossCheckResult::kSkipped;  // ring never reached past L

  // Both books now represent the same exchange state; compare the top levels.
  if (scratch.NumBids() < static_cast<std::size_t>(levels) ||
      scratch.NumAsks() < static_cast<std::size_t>(levels)) {
    return CrossCheckResult::kSkipped;  // not enough depth to compare meaningfully
  }
  for (int n = 0; n < levels; ++n) {
    const auto sb = scratch.DepthAt(Side::kBid, static_cast<std::size_t>(n));
    const auto lb = live.DepthAt(Side::kBid, static_cast<std::size_t>(n));
    const auto sa = scratch.DepthAt(Side::kAsk, static_cast<std::size_t>(n));
    const auto la = live.DepthAt(Side::kAsk, static_cast<std::size_t>(n));
    if (!sb || !lb || !sa || !la) return CrossCheckResult::kMismatch;
    if (sb->px_ticks != lb->px_ticks || sb->qty_lots != lb->qty_lots)
      return CrossCheckResult::kMismatch;
    if (sa->px_ticks != la->px_ticks || sa->qty_lots != la->qty_lots)
      return CrossCheckResult::kMismatch;
  }
  return CrossCheckResult::kMatch;
}

}  // namespace asmm
