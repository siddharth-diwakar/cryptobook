#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "core/events.hpp"
#include "core/types.hpp"
#include "strategy/records.hpp"

namespace asmm {

enum class OrderState : u8 {
  kPendingNew = 0,       // command sent, no ack yet
  kWorking = 1,          // resting on the book
  kPartiallyFilled = 2,  // resting, some qty filled
  kPendingCancel = 3,    // cancel sent, awaiting confirmation
};

struct WorkingOrder {
  bool active = false;
  u64 client_id = 0;
  u64 exchange_order_id = 0;
  i64 px_ticks = 0;
  i64 qty_lots = 0;      // original order qty
  i64 cum_qty_lots = 0;  // cumulative filled
  Side side = Side::kBid;
  OrderState state = OrderState::kPendingNew;
};

// Order state machine (single-writer, engine-owned, pure — no I/O, no clock read;
// ts_ns and the client-id counter are passed in). Translates the strategy's desired
// quote into place/cancel commands (no churn: an unchanged side emits nothing),
// tracks working orders through NEW->ACK->PARTIAL->FILLED/CANCELED/REJECTED, and
// rebuilds from exchange truth on reconcile so it can never silently desync.
class Oms {
 public:
  // Desired quote (from Strategy) -> 0..N OrderCommands. Price move or pulled side
  // => cancel now (re-placed on a later tick once the slot frees), so at most one
  // resting order per side. Same price+qty => no command.
  void Reconcile(const QuoteRecord& desired, i64 ts_ns, u64& next_client_id,
                 std::vector<OrderCommand>& out);

  // Drive state transitions from an execution report (or a gateway reject).
  void OnExec(const ExecEvent& e);

  // Replace the working set with exchange truth (GET openOrders/account).
  void OnReconcile(const ReconcileReport& r);

  // Emit a single cancel-all command and mark every working order pending-cancel.
  void CancelAll(i64 ts_ns, std::vector<OrderCommand>& out);

  bool AllFlat() const;
  std::size_t working_count() const;
  const WorkingOrder* FindSide(Side side) const;

 private:
  static constexpr std::size_t kSlots = 8;

  WorkingOrder* SideSlot(Side side);
  WorkingOrder* FindByClient(u64 client_id);
  WorkingOrder* FreeSlot();
  void EmitPlace(Side side, i64 px, i64 qty, i64 ts_ns, u64& next_client_id,
                 std::vector<OrderCommand>& out);
  void EmitCancel(const WorkingOrder& o, i64 ts_ns, std::vector<OrderCommand>& out);

  u64 next_seq_ = 1;  // per-command sequence (deterministic; logging/ordering only)
  std::array<WorkingOrder, kSlots> slots_{};
};

}  // namespace asmm
