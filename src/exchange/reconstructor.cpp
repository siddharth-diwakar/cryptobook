#include "exchange/reconstructor.hpp"

namespace asmm {

void Reconstructor::OnSnapshot(const DepthSnapshot& snap, i64 ts_ns,
                               std::vector<MarketEvent>& out) {
  snapshot_last_id_ = snap.last_update_id;
  state_ = State::kAwaitFirstEvent;
  prev_u_ = 0;

  // Fragment bids then asks into fixed-size kSnapshot events. At least one event
  // is emitted even for an (unexpected) empty snapshot so the engine still sees
  // the snapshot-start marker and clears the book.
  std::size_t bi = 0;
  std::size_t ai = 0;
  bool first_fragment = true;
  do {
    MarketEvent ev{};
    ev.ts_recv_ns = ts_ns;
    ev.first_update_id = snap.last_update_id;
    ev.final_update_id = snap.last_update_id;
    ev.kind = EventKind::kSnapshot;
    if (first_fragment) {
      ev.flags |= kFlagSnapshotStart;
      first_fragment = false;
    }

    std::size_t slot = 0;
    while (bi < snap.bids.size() && slot < kMaxLevelsPerEvent) {
      ev.levels[slot++] = snap.bids[bi++];
      ++ev.num_bids;
    }
    while (ai < snap.asks.size() && slot < kMaxLevelsPerEvent) {
      ev.levels[slot++] = snap.asks[ai++];
      ++ev.num_asks;
    }
    if (bi < snap.bids.size() || ai < snap.asks.size()) ev.flags |= kFlagContinuation;
    out.push_back(ev);
  } while (bi < snap.bids.size() || ai < snap.asks.size());
}

ReconAction Reconstructor::OnDiffGroup(std::span<const MarketEvent> group) {
  if (group.empty()) return ReconAction::kGap;  // defensive; a group is never empty
  const u64 U = group.front().first_update_id;
  const u64 u = group.front().final_update_id;

  switch (state_) {
    case State::kIdle:
      return ReconAction::kNeedSnapshot;

    case State::kAwaitFirstEvent:
      if (u <= snapshot_last_id_) {
        return ReconAction::kDrop;  // predates the snapshot (protocol step 3)
      }
      // u > L here, so L+1 <= u already holds; the bracket reduces to U <= L+1.
      if (U <= snapshot_last_id_ + 1) {
        prev_u_ = u;
        state_ = State::kLive;
        return ReconAction::kEmitAndGoLive;  // protocol step 4
      }
      return ReconAction::kSnapshotTooOld;  // U > L+1: fetched too late

    case State::kLive:
      if (U == prev_u_ + 1) {
        prev_u_ = u;
        return ReconAction::kEmit;
      }
      state_ = State::kIdle;
      return ReconAction::kGap;
  }
  return ReconAction::kGap;  // unreachable
}

void Reconstructor::Reset() {
  state_ = State::kIdle;
  snapshot_last_id_ = 0;
  prev_u_ = 0;
}

}  // namespace asmm
