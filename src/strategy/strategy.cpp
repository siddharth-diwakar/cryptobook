#include "strategy/strategy.hpp"

#include <cmath>
#include <cstdlib>

#include "strategy/as_quoter.hpp"

namespace asmm {

namespace {
constexpr double kSecondsPerDay = 86400.0;

QuoteRecord MakeQuote(u64 fid, i64 mid_x2, i64 inventory, double sigma_p, i64 bid_px, i64 bid_qty,
                      i64 ask_px, i64 ask_qty, bool quoting, bool one_sided, bool pulled) {
  QuoteRecord q{};
  q.final_update_id = fid;
  q.bid_px = bid_px;
  q.bid_qty = bid_qty;
  q.ask_px = ask_px;
  q.ask_qty = ask_qty;
  q.mid_x2 = mid_x2;
  q.inventory_lots = inventory;
  q.sigma_p_micro = static_cast<i64>(sigma_p * 1e6);
  q.quoting = quoting ? 1 : 0;
  q.one_sided = one_sided ? 1 : 0;
  q.pulled = pulled ? 1 : 0;
  return q;
}
}  // namespace

Strategy::Strategy(const StrategyParams& p, bool live_mode)
    : p_(p),
      live_mode_(live_mode),
      sigma_(p.sigma_halflife_s, p.sigma_min_samples),
      paper_(p.maker_fee_bps) {}

void Strategy::OnExec(const ExecEvent& e) {
  paper_.ApplyRealFill(e);
  if (e.last_qty_lots > 0) ++fill_count_;
}

bool Strategy::MinNotionalOk(i64 px_ticks, i64 qty_lots) const {
  const double notional = static_cast<double>(px_ticks) * static_cast<double>(qty_lots) *
                          std::pow(10.0, -static_cast<double>(p_.px_scale + p_.qty_scale));
  return notional >= p_.min_notional_usdt;
}

void Strategy::OnResync() {
  paper_.PullQuotes();
  sigma_.ResetContinuity();
  have_working_ = false;
}

StrategyOutput Strategy::OnEvent(const L2Book& book, const MarketEvent& ev) {
  StrategyOutput out;
  const auto midx2 = book.MidX2();
  if (!midx2) {
    paper_.PullQuotes();
    have_working_ = false;
    return out;
  }
  const double mid = static_cast<double>(*midx2) / 2.0;

  sigma_.Observe(ev.ts_exchange_ms, mid);

  // Fills FIRST: the quotes resting from the previous tick may be crossed by this
  // new book state. (Re-quoting happens after, so a just-placed post-only quote is
  // never "filled" in the same event.) Paper mode only — in live mode real fills
  // arrive via OnExec from the user-data stream.
  if (!live_mode_) {
    paper_.SimulateFills(book, ev.final_update_id, out.fills);
    fill_count_ += out.fills.size();
  }

  const double sr = sigma_.sigma_r();
  const double sigma_p = mid * sr * std::sqrt(kSecondsPerDay);

  // Guards: warmup, volatility spike. Pull quotes and emit a pulled record.
  if (!sigma_.ready() || sr > p_.sigma_spike_threshold) {
    paper_.PullQuotes();
    have_working_ = false;
    out.has_quote = true;
    out.quote = MakeQuote(ev.final_update_id, *midx2, paper_.inventory(), sigma_p, 0, 0, 0, 0,
                          /*quoting=*/false, /*one_sided=*/false, /*pulled=*/true);
    return out;
  }

  const double q_base =
      static_cast<double>(paper_.inventory()) / std::pow(10.0, static_cast<double>(p_.qty_scale));
  const i64 bb = book.BestBid() ? book.BestBid()->px_ticks : 0;
  const i64 ba = book.BestAsk() ? book.BestAsk()->px_ticks : 0;
  const Quote qt = ComputeQuote(mid, q_base, sigma_p, p_.gamma, p_.kappa, p_.tau_days, bb, ba);

  // Inventory risk: one-side only past the soft limit.
  const i64 q = paper_.inventory();
  bool quote_bid = true;
  bool quote_ask = true;
  bool one_sided = false;
  if (q >= p_.q_max_lots) {
    quote_bid = false;  // too long -> stop buying
    one_sided = true;
  }
  if (q <= -p_.q_max_lots) {
    quote_ask = false;  // too short -> stop selling
    one_sided = true;
  }

  // Hysteresis: hold the working price if the new one moved < H ticks.
  i64 bid_px = qt.bid_px;
  i64 ask_px = qt.ask_px;
  if (have_working_) {
    if (std::llabs(bid_px - working_bid_) < p_.hysteresis_ticks) bid_px = working_bid_;
    if (std::llabs(ask_px - working_ask_) < p_.hysteresis_ticks) ask_px = working_ask_;
  }
  // Re-apply the post-only clamp AFTER hysteresis: a held price may now cross the
  // moved book, which would rest a marketable quote and manufacture phantom fills.
  if (ba > 0 && bid_px >= ba) bid_px = ba - 1;
  if (bb > 0 && ask_px <= bb) ask_px = bb + 1;
  if (bid_px >= ask_px) bid_px = ask_px - 1;
  working_bid_ = bid_px;
  working_ask_ = ask_px;
  have_working_ = true;

  const i64 size = p_.quote_size_lots;
  const PaperQuote pb{bid_px, size, quote_bid && MinNotionalOk(bid_px, size)};
  const PaperQuote pa{ask_px, size, quote_ask && MinNotionalOk(ask_px, size)};
  paper_.SetQuotes(pb, pa);  // rests until the next event's fill check

  out.has_quote = true;
  out.quote = MakeQuote(ev.final_update_id, *midx2, paper_.inventory(), sigma_p,
                        pb.active ? bid_px : 0, pb.active ? size : 0, pa.active ? ask_px : 0,
                        pa.active ? size : 0, /*quoting=*/pb.active || pa.active, one_sided, false);
  return out;
}

}  // namespace asmm
