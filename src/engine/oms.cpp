#include "engine/oms.hpp"

#include <algorithm>

namespace asmm {

WorkingOrder* Oms::SideSlot(Side side) {
  for (auto& s : slots_) {
    if (s.active && s.side == side) return &s;
  }
  return nullptr;
}

WorkingOrder* Oms::FindByClient(u64 client_id) {
  for (auto& s : slots_) {
    if (s.active && s.client_id == client_id) return &s;
  }
  return nullptr;
}

WorkingOrder* Oms::FreeSlot() {
  for (auto& s : slots_) {
    if (!s.active) return &s;
  }
  return nullptr;
}

const WorkingOrder* Oms::FindSide(Side side) const {
  for (const auto& s : slots_) {
    if (s.active && s.side == side) return &s;
  }
  return nullptr;
}

void Oms::EmitPlace(Side side, i64 px, i64 qty, i64 ts_ns, u64& next_client_id,
                    std::vector<OrderCommand>& out) {
  WorkingOrder* slot = FreeSlot();
  if (slot == nullptr) return;  // saturated (shouldn't happen with <=2 sides); skip
  const u64 cid = next_client_id++;
  OrderCommand c{};
  c.seq = next_seq_++;
  c.client_id = cid;
  c.px_ticks = px;
  c.qty_lots = qty;
  c.ts_ns = ts_ns;
  c.kind = CmdKind::kPlace;
  c.side = side;
  out.push_back(c);

  *slot = WorkingOrder{};
  slot->active = true;
  slot->client_id = cid;
  slot->px_ticks = px;
  slot->qty_lots = qty;
  slot->side = side;
  slot->state = OrderState::kPendingNew;
}

void Oms::EmitCancel(const WorkingOrder& o, i64 ts_ns, std::vector<OrderCommand>& out) {
  OrderCommand c{};
  c.seq = next_seq_++;
  c.client_id = o.client_id;
  c.ts_ns = ts_ns;
  c.kind = CmdKind::kCancel;
  c.side = o.side;
  out.push_back(c);
}

void Oms::Reconcile(const QuoteRecord& desired, i64 ts_ns, u64& next_client_id,
                    std::vector<OrderCommand>& out) {
  struct SideWant {
    Side side;
    i64 px;
    i64 qty;
  };
  const SideWant wants[2] = {{Side::kBid, desired.bid_px, desired.bid_qty},
                             {Side::kAsk, desired.ask_px, desired.ask_qty}};

  for (const SideWant& w : wants) {
    WorkingOrder* cur = SideSlot(w.side);
    if (w.px == 0) {
      // Want no quote on this side: cancel any resting order.
      if (cur != nullptr && cur->state != OrderState::kPendingCancel) {
        EmitCancel(*cur, ts_ns, out);
        cur->state = OrderState::kPendingCancel;
      }
      continue;
    }
    if (cur == nullptr) {
      EmitPlace(w.side, w.px, w.qty, ts_ns, next_client_id, out);
      continue;
    }
    if (cur->state == OrderState::kPendingCancel) {
      continue;  // already cancelling; re-place once the slot frees
    }
    if (cur->px_ticks != w.px || cur->qty_lots != w.qty) {
      // Price/size changed: cancel now, re-place on a later tick (single order/side).
      EmitCancel(*cur, ts_ns, out);
      cur->state = OrderState::kPendingCancel;
    }
    // else: same price+qty -> no command (no churn).
  }
}

void Oms::OnExec(const ExecEvent& e) {
  WorkingOrder* o = FindByClient(e.client_id);
  if (o == nullptr) return;  // unrecognized (e.g. pre-reconcile); reconcile will resync
  switch (e.kind) {
    case ExecKind::kAck:
      o->state = OrderState::kWorking;
      o->exchange_order_id = e.exchange_order_id;
      break;
    case ExecKind::kPartial:
      o->state = OrderState::kPartiallyFilled;
      o->exchange_order_id = e.exchange_order_id;
      o->cum_qty_lots = e.cum_qty_lots;
      break;
    case ExecKind::kFilled:
    case ExecKind::kCanceled:
    case ExecKind::kRejected:
    case ExecKind::kExpired:
      o->active = false;  // terminal: free the slot
      break;
  }
}

void Oms::OnReconcile(const ReconcileReport& r) {
  for (auto& s : slots_) s.active = false;  // rebuild from exchange truth
  const u32 n = std::min<u32>(r.num_open, kMaxReconcileRows);
  for (u32 i = 0; i < n; ++i) {
    WorkingOrder* slot = FreeSlot();
    if (slot == nullptr) break;
    const OpenOrderRow& row = r.rows[i];
    *slot = WorkingOrder{};
    slot->active = true;
    slot->client_id = row.client_id;
    slot->exchange_order_id = row.exchange_order_id;
    slot->px_ticks = row.px_ticks;
    slot->qty_lots = row.orig_qty_lots;
    slot->cum_qty_lots = row.cum_qty_lots;
    slot->side = row.side;
    slot->state = row.cum_qty_lots > 0 ? OrderState::kPartiallyFilled : OrderState::kWorking;
  }
}

void Oms::CancelAll(i64 ts_ns, std::vector<OrderCommand>& out) {
  OrderCommand c{};
  c.seq = next_seq_++;
  c.client_id = 0;
  c.ts_ns = ts_ns;
  c.kind = CmdKind::kCancelAll;
  out.push_back(c);
  for (auto& s : slots_) {
    if (s.active) s.state = OrderState::kPendingCancel;
  }
}

bool Oms::AllFlat() const { return working_count() == 0; }

std::size_t Oms::working_count() const {
  std::size_t n = 0;
  for (const auto& s : slots_) {
    if (s.active) ++n;
  }
  return n;
}

}  // namespace asmm
