#include "engine/gap_verifier.hpp"

namespace asmm {

bool GapVerifier::Check(const MarketEvent& ev) {
  ++events_checked_;
  const bool is_continuation_of_prev = prev_continuation_;
  const bool this_has_cont = (ev.flags & kFlagContinuation) != 0;

  // Snapshot fragments: a snapshot-start resets verification (book rebuilt). All
  // snapshot fragments carry final_update_id == lastUpdateId (L).
  if (ev.kind == EventKind::kSnapshot) {
    if (ev.flags & kFlagSnapshotStart) {
      have_snapshot_ = true;
      live_ = false;
      snapshot_last_id_ = ev.final_update_id;
    }
    prev_continuation_ = this_has_cont;
    if (!this_has_cont) prev_u_ = ev.final_update_id;  // = L
    return true;
  }

  bool ok = true;
  if (is_continuation_of_prev) {
    // Same group as the previous fragment; sequence is checked once per group,
    // at its first fragment. Nothing to check here.
  } else if (!have_snapshot_) {
    // No snapshot seen yet — cannot verify (engine path normally always has one).
  } else if (!live_) {
    // First diff group after the snapshot: the bracket rule U <= L+1 <= u.
    if (ev.first_update_id <= snapshot_last_id_ + 1 &&
        snapshot_last_id_ + 1 <= ev.final_update_id) {
      live_ = true;
    } else {
      ok = false;  // engine should only ever be handed the bracket event here
    }
  } else if (ev.first_update_id != prev_u_ + 1) {
    ok = false;  // a real undetected discontinuity
  }

  if (!ok) ++undetected_gaps_;
  prev_continuation_ = this_has_cont;
  if (!this_has_cont) prev_u_ = ev.final_update_id;
  return ok;
}

}  // namespace asmm
