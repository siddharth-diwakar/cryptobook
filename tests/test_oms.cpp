#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "core/events.hpp"
#include "engine/oms.hpp"
#include "strategy/records.hpp"

using namespace asmm;

namespace {

QuoteRecord Quote(i64 bid_px, i64 bid_qty, i64 ask_px, i64 ask_qty) {
  QuoteRecord q{};
  q.bid_px = bid_px;
  q.bid_qty = bid_qty;
  q.ask_px = ask_px;
  q.ask_qty = ask_qty;
  return q;
}

ExecEvent Exec(u64 client_id, ExecKind kind, i64 cum = 0, u64 oid = 111) {
  ExecEvent e{};
  e.client_id = client_id;
  e.exchange_order_id = oid;
  e.cum_qty_lots = cum;
  e.kind = kind;
  return e;
}

}  // namespace

TEST_CASE("oms: two-sided quote places two orders, no churn when unchanged", "[oms]") {
  Oms oms;
  u64 cid = 1;
  std::vector<OrderCommand> out;

  oms.Reconcile(Quote(100, 10, 102, 10), 0, cid, out);
  REQUIRE(out.size() == 2);
  REQUIRE(out[0].kind == CmdKind::kPlace);
  REQUIRE(out[0].side == Side::kBid);
  REQUIRE(out[1].side == Side::kAsk);
  REQUIRE(oms.working_count() == 2);

  // Ack both -> working.
  oms.OnExec(Exec(out[0].client_id, ExecKind::kAck));
  oms.OnExec(Exec(out[1].client_id, ExecKind::kAck));

  // Same desired quote -> no new commands (hysteresis already applied upstream).
  out.clear();
  oms.Reconcile(Quote(100, 10, 102, 10), 1, cid, out);
  REQUIRE(out.empty());
  REQUIRE(oms.working_count() == 2);
}

TEST_CASE("oms: NEW->ACK->PARTIAL->FILLED frees the slot", "[oms]") {
  Oms oms;
  u64 cid = 1;
  std::vector<OrderCommand> out;
  oms.Reconcile(Quote(100, 10, 0, 0), 0, cid, out);  // bid only
  const u64 bid_cid = out[0].client_id;

  oms.OnExec(Exec(bid_cid, ExecKind::kAck));
  REQUIRE(oms.working_count() == 1);
  oms.OnExec(Exec(bid_cid, ExecKind::kPartial, /*cum=*/4));
  REQUIRE(oms.FindSide(Side::kBid)->state == OrderState::kPartiallyFilled);
  REQUIRE(oms.FindSide(Side::kBid)->cum_qty_lots == 4);
  oms.OnExec(Exec(bid_cid, ExecKind::kFilled, /*cum=*/10));
  REQUIRE(oms.AllFlat());
}

TEST_CASE("oms: a reject frees the slot so the strategy can re-quote", "[oms]") {
  Oms oms;
  u64 cid = 1;
  std::vector<OrderCommand> out;
  oms.Reconcile(Quote(100, 10, 0, 0), 0, cid, out);
  oms.OnExec(Exec(out[0].client_id, ExecKind::kRejected));
  REQUIRE(oms.AllFlat());

  // Next reconcile with the same desire re-places a fresh order.
  out.clear();
  oms.Reconcile(Quote(100, 10, 0, 0), 1, cid, out);
  REQUIRE(out.size() == 1);
  REQUIRE(out[0].kind == CmdKind::kPlace);
}

TEST_CASE("oms: a price move cancels then re-places on a later tick", "[oms]") {
  Oms oms;
  u64 cid = 1;
  std::vector<OrderCommand> out;
  oms.Reconcile(Quote(100, 10, 0, 0), 0, cid, out);
  const u64 first = out[0].client_id;
  oms.OnExec(Exec(first, ExecKind::kAck));

  // Price moved: expect a single cancel (no simultaneous place -> at most 1/side).
  out.clear();
  oms.Reconcile(Quote(101, 10, 0, 0), 1, cid, out);
  REQUIRE(out.size() == 1);
  REQUIRE(out[0].kind == CmdKind::kCancel);
  REQUIRE(out[0].client_id == first);
  REQUIRE(oms.FindSide(Side::kBid)->state == OrderState::kPendingCancel);

  // While pending-cancel, no further commands.
  out.clear();
  oms.Reconcile(Quote(101, 10, 0, 0), 2, cid, out);
  REQUIRE(out.empty());

  // Cancel confirmed -> slot frees -> next reconcile places at the new price.
  oms.OnExec(Exec(first, ExecKind::kCanceled));
  out.clear();
  oms.Reconcile(Quote(101, 10, 0, 0), 3, cid, out);
  REQUIRE(out.size() == 1);
  REQUIRE(out[0].kind == CmdKind::kPlace);
  REQUIRE(out[0].px_ticks == 101);
  REQUIRE(out[0].client_id != first);
}

TEST_CASE("oms: a pulled side cancels the resting order", "[oms]") {
  Oms oms;
  u64 cid = 1;
  std::vector<OrderCommand> out;
  oms.Reconcile(Quote(100, 10, 102, 10), 0, cid, out);
  oms.OnExec(Exec(out[0].client_id, ExecKind::kAck));
  oms.OnExec(Exec(out[1].client_id, ExecKind::kAck));

  out.clear();
  oms.Reconcile(Quote(0, 0, 102, 10), 1, cid, out);  // bid pulled
  REQUIRE(out.size() == 1);
  REQUIRE(out[0].kind == CmdKind::kCancel);
  REQUIRE(out[0].side == Side::kBid);
}

TEST_CASE("oms: OnReconcile rebuilds the working set from exchange truth", "[oms]") {
  Oms oms;
  u64 cid = 1;
  std::vector<OrderCommand> out;
  oms.Reconcile(Quote(100, 10, 102, 10), 0, cid, out);  // 2 local pending orders

  // Exchange says only ONE order actually rests (client_id 500), partially filled.
  ReconcileReport r{};
  r.num_open = 1;
  r.rows[0] = OpenOrderRow{.client_id = 500,
                           .exchange_order_id = 9,
                           .px_ticks = 100,
                           .orig_qty_lots = 10,
                           .cum_qty_lots = 3,
                           .side = Side::kBid,
                           ._pad = {}};
  oms.OnReconcile(r);
  REQUIRE(oms.working_count() == 1);
  const WorkingOrder* bid = oms.FindSide(Side::kBid);
  REQUIRE(bid != nullptr);
  REQUIRE(bid->client_id == 500);
  REQUIRE(bid->state == OrderState::kPartiallyFilled);
  REQUIRE(oms.FindSide(Side::kAsk) == nullptr);
}

TEST_CASE("oms: CancelAll emits one command and marks orders pending-cancel", "[oms]") {
  Oms oms;
  u64 cid = 1;
  std::vector<OrderCommand> out;
  oms.Reconcile(Quote(100, 10, 102, 10), 0, cid, out);
  oms.OnExec(Exec(out[0].client_id, ExecKind::kAck));
  oms.OnExec(Exec(out[1].client_id, ExecKind::kAck));

  out.clear();
  oms.CancelAll(5, out);
  REQUIRE(out.size() == 1);
  REQUIRE(out[0].kind == CmdKind::kCancelAll);
  REQUIRE(oms.FindSide(Side::kBid)->state == OrderState::kPendingCancel);

  // Exchange confirms cancels -> flat.
  oms.OnExec(Exec(oms.FindSide(Side::kBid)->client_id, ExecKind::kCanceled));
  oms.OnExec(Exec(oms.FindSide(Side::kAsk)->client_id, ExecKind::kCanceled));
  REQUIRE(oms.AllFlat());
}
