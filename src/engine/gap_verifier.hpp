#pragma once

#include "core/events.hpp"
#include "core/types.hpp"

namespace asmm {

// Engine-side, INDEPENDENT verifier of Binance sequence continuity — redundant
// with the MD thread's Reconstructor. It sees the flat stream of events the
// engine pops (snapshot fragments + emitted diffs, continuation-aware) and never
// sees dropped events. In correct operation the MD thread resyncs on any gap
// before emitting a bad-sequence diff, so this verifier should NEVER fire:
// undetected_gaps() staying 0 across the 24h run is the acceptance proof. If the
// MD logic has a bug and emits a discontinuity without an intervening
// snapshot-start, this catches it.
class GapVerifier {
 public:
  // Feed each event in engine pop order. Returns false (and counts) if a
  // sequence violation is detected.
  bool Check(const MarketEvent& ev);

  u64 undetected_gaps() const { return undetected_gaps_; }
  u64 events_checked() const { return events_checked_; }
  bool live() const { return live_; }

 private:
  bool have_snapshot_{false};
  bool live_{false};  // past the bracket event, verifying U == prev_u + 1
  bool prev_continuation_{false};
  u64 prev_u_{0};  // final_update_id of the last completed group
  u64 snapshot_last_id_{0};
  u64 undetected_gaps_{0};
  u64 events_checked_{0};
};

}  // namespace asmm
