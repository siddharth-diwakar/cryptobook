#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "engine/gap_verifier.hpp"
#include "exchange/reconstructor.hpp"

using namespace asmm;

namespace {

MarketEvent Snap(u64 L, bool start, bool cont) {
  MarketEvent ev{};
  ev.kind = EventKind::kSnapshot;
  ev.first_update_id = L;
  ev.final_update_id = L;
  if (start) ev.flags |= kFlagSnapshotStart;
  if (cont) ev.flags |= kFlagContinuation;
  return ev;
}

MarketEvent Diff(u64 U, u64 u, bool cont = false) {
  MarketEvent ev{};
  ev.kind = EventKind::kDepthDiff;
  ev.first_update_id = U;
  ev.final_update_id = u;
  if (cont) ev.flags |= kFlagContinuation;
  return ev;
}

}  // namespace

TEST_CASE("gapverify: clean bootstrap + live stream never fires", "[gapverify]") {
  GapVerifier v;
  REQUIRE(v.Check(Snap(1000, true, false)));  // snapshot-start, single fragment
  REQUIRE(v.Check(Diff(1000, 1005)));         // bracket U<=L+1<=u
  REQUIRE(v.live());
  REQUIRE(v.Check(Diff(1006, 1010)));
  REQUIRE(v.Check(Diff(1011, 1011)));
  REQUIRE(v.Check(Diff(1012, 1020)));
  REQUIRE(v.undetected_gaps() == 0);
}

TEST_CASE("gapverify: multi-fragment snapshot then bracket", "[gapverify]") {
  GapVerifier v;
  REQUIRE(v.Check(Snap(1000, true, true)));    // snapshot frag 1 (continues)
  REQUIRE(v.Check(Snap(1000, false, true)));   // frag 2 (continues)
  REQUIRE(v.Check(Snap(1000, false, false)));  // final frag
  REQUIRE(v.Check(Diff(1001, 1004)));          // bracket U==L+1
  REQUIRE(v.live());
  REQUIRE(v.undetected_gaps() == 0);
}

TEST_CASE("gapverify: fragmented diff group does not false-positive", "[gapverify]") {
  GapVerifier v;
  REQUIRE(v.Check(Snap(500, true, false)));
  REQUIRE(v.Check(Diff(500, 510)));  // bracket, go live, prev_u=510
  // Next group is fragmented: two events sharing U=511,u=520.
  REQUIRE(v.Check(Diff(511, 520, /*cont=*/true)));
  REQUIRE(v.Check(Diff(511, 520, /*cont=*/false)));
  REQUIRE(v.Check(Diff(521, 530)));  // continues cleanly from 520
  REQUIRE(v.undetected_gaps() == 0);
}

TEST_CASE("gapverify: undetected discontinuity is caught and counted", "[gapverify]") {
  GapVerifier v;
  REQUIRE(v.Check(Snap(1000, true, false)));
  REQUIRE(v.Check(Diff(1001, 1005)));        // live, prev_u=1005
  REQUIRE_FALSE(v.Check(Diff(1007, 1010)));  // expected 1006 -> GAP
  REQUIRE(v.undetected_gaps() == 1);
}

TEST_CASE("gapverify: snapshot-start resets after a resync", "[gapverify]") {
  GapVerifier v;
  REQUIRE(v.Check(Snap(1000, true, false)));
  REQUIRE(v.Check(Diff(1001, 1005)));
  // Simulate MD-thread resync: a fresh snapshot arrives with a new L.
  REQUIRE(v.Check(Snap(2000, true, false)));
  REQUIRE_FALSE(v.live());
  REQUIRE(v.Check(Diff(2001, 2003)));  // new bracket
  REQUIRE(v.live());
  REQUIRE(v.Check(Diff(2004, 2004)));
  REQUIRE(v.undetected_gaps() == 0);  // resync is NOT a gap
}
