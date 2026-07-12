#pragma once

#include "core/types.hpp"

namespace asmm {

struct Quote {
  i64 bid_px;          // ticks
  i64 ask_px;          // ticks (always > bid_px)
  double reservation;  // r (ticks)
  double delta_total;  // total optimal spread (ticks)
};

// Pure Avellaneda-Stoikov quote computation (docs/MODEL.md), all in tick units.
//   r     = s - q*gamma*sigma_p^2*tau
//   delta = gamma*sigma_p^2*tau + (2/gamma)*ln(1 + gamma/kappa)
//   bid   = floor(r - delta/2),  ask = ceil(r + delta/2)
// Then: never emit a crossed quote (ask > bid enforced); post-only clamp to the
// current book (best_bid/best_ask in ticks, 0 = unknown) so a quote can't be a
// marketable/taker order. Float math is fine here — this is a decision
// computation; the outputs are integer ticks.
//
// s_ticks: mid in ticks; q_base: inventory in base units; sigma_p: absolute price
// volatility in ticks per sqrt(tau-unit).
Quote ComputeQuote(double s_ticks, double q_base, double sigma_p, double gamma, double kappa,
                   double tau, i64 best_bid, i64 best_ask);

}  // namespace asmm
