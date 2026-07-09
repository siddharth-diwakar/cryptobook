#pragma once

#include <span>
#include <vector>

#include "core/events.hpp"
#include "core/types.hpp"
#include "exchange/binance_depth.hpp"

namespace asmm {

// What the caller (market-data driver) should do with a diff group.
enum class ReconAction : u8 {
  kDrop,            // event(s) predate the snapshot; discard
  kEmit,            // live event(s); apply to the book
  kEmitAndGoLive,   // the bracket event; apply, book is now live
  kGap,             // sequence gap; caller must resync (state -> Idle)
  kNeedSnapshot,    // no snapshot held; caller must fetch one
  kSnapshotTooOld,  // first event has U > lastUpdateId+1; refetch a newer snapshot
};

// Pure, I/O-free implementation of Binance's L2 reconstruction protocol
// (ARCHITECTURE §3). No clock, no logging, no sockets — fully deterministic and
// unit-testable. The driver feeds it snapshots and diff groups and acts on the
// returned action.
class Reconstructor {
 public:
  enum class State : u8 { kIdle, kAwaitFirstEvent, kLive };

  // Convert a REST snapshot into fragmented kSnapshot MarketEvents (appended to
  // `out`) and move to kAwaitFirstEvent. The first fragment is flagged
  // kFlagSnapshotStart; every fragment carries first/final_update_id ==
  // snapshot.last_update_id; all but the last are flagged kFlagContinuation.
  void OnSnapshot(const DepthSnapshot& snap, i64 ts_ns, std::vector<MarketEvent>& out);

  // Decide what to do with one diff group: the fragments produced by a single
  // ParseDepthDiff call, which share first/final_update_id (U/u). Non-mutating
  // w.r.t. `group`; updates internal sequence state.
  ReconAction OnDiffGroup(std::span<const MarketEvent> group);

  void Reset();

  State state() const { return state_; }
  u64 prev_u() const { return prev_u_; }
  u64 snapshot_last_id() const { return snapshot_last_id_; }

 private:
  State state_{State::kIdle};
  u64 snapshot_last_id_{0};
  u64 prev_u_{0};
};

}  // namespace asmm
