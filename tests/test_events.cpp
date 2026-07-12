#include <catch2/catch_test_macros.hpp>

#include "core/events.hpp"
#include "core/spsc_queue.hpp"

using namespace asmm;

// The layout invariants are enforced by static_assert in events.hpp; if this file
// compiles they already hold. These runtime checks document the sizes and prove
// the new PODs survive a round-trip through the SPSC queue byte-for-byte.

TEST_CASE("events: Phase 5 PODs have the fixed wire sizes", "[events]") {
  REQUIRE(sizeof(OrderCommand) == 48);
  REQUIRE(sizeof(ExecEvent) == 80);
  REQUIRE(sizeof(OpenOrderRow) == 48);
  REQUIRE(sizeof(ReconcileReport) == 48 + 16 * sizeof(OpenOrderRow));
}

TEST_CASE("events: OrderCommand round-trips through an SPSC queue", "[events]") {
  SpscQueue<OrderCommand, 8> q;
  const OrderCommand in{.seq = 7,
                        .client_id = 42,
                        .px_ticks = 6'512'345,
                        .qty_lots = 10,
                        .ts_ns = 123456789,
                        .kind = CmdKind::kPlace,
                        .side = Side::kBid,
                        ._pad = {}};
  REQUIRE(q.try_push(in));
  OrderCommand out{};
  REQUIRE(q.try_pop(out));
  REQUIRE(out.seq == in.seq);
  REQUIRE(out.client_id == in.client_id);
  REQUIRE(out.px_ticks == in.px_ticks);
  REQUIRE(out.qty_lots == in.qty_lots);
  REQUIRE(out.kind == CmdKind::kPlace);
  REQUIRE(out.side == Side::kBid);
}

TEST_CASE("events: ExecEvent round-trips and preserves fill fields", "[events]") {
  SpscQueue<ExecEvent, 8> q;
  const ExecEvent in{.seq = 1,
                     .client_id = 42,
                     .exchange_order_id = 998877,
                     .last_px_ticks = 6'500'000,
                     .last_qty_lots = 4,
                     .cum_qty_lots = 4,
                     .fee_units = 26,
                     .ts_exchange_ms = 1'700'000'000'000,
                     .ts_recv_ns = 55,
                     .kind = ExecKind::kPartial,
                     .side = Side::kAsk,
                     .comm_asset = CommAsset::kQuote,
                     .reject_reason = 0,
                     ._pad = {}};
  REQUIRE(q.try_push(in));
  ExecEvent out{};
  REQUIRE(q.try_pop(out));
  REQUIRE(out.exchange_order_id == in.exchange_order_id);
  REQUIRE(out.last_px_ticks == in.last_px_ticks);
  REQUIRE(out.cum_qty_lots == in.cum_qty_lots);
  REQUIRE(out.fee_units == in.fee_units);
  REQUIRE(out.ts_exchange_ms == in.ts_exchange_ms);
  REQUIRE(out.kind == ExecKind::kPartial);
  REQUIRE(out.comm_asset == CommAsset::kQuote);
}

TEST_CASE("events: ReconcileReport carries open-order rows", "[events]") {
  SpscQueue<ReconcileReport, 4> q;
  ReconcileReport in{};
  in.base_free_lots = 100;
  in.quote_free_units = 5'000'000;
  in.ts_exchange_ms = 1'700'000'000'123;
  in.num_open = 2;
  in.rows[0] = OpenOrderRow{.client_id = 42,
                            .exchange_order_id = 1,
                            .px_ticks = 6'400'000,
                            .orig_qty_lots = 10,
                            .cum_qty_lots = 0,
                            .side = Side::kBid,
                            ._pad = {}};
  in.rows[1] = OpenOrderRow{.client_id = 43,
                            .exchange_order_id = 2,
                            .px_ticks = 6'600'000,
                            .orig_qty_lots = 10,
                            .cum_qty_lots = 3,
                            .side = Side::kAsk,
                            ._pad = {}};
  REQUIRE(q.try_push(in));
  ReconcileReport out{};
  REQUIRE(q.try_pop(out));
  REQUIRE(out.num_open == 2);
  REQUIRE(out.rows[0].client_id == 42);
  REQUIRE(out.rows[1].side == Side::kAsk);
  REQUIRE(out.rows[1].cum_qty_lots == 3);
  REQUIRE(out.quote_free_units == 5'000'000);
}
