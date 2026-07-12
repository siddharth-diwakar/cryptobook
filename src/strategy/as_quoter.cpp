#include "strategy/as_quoter.hpp"

#include <cmath>

namespace asmm {

Quote ComputeQuote(double s_ticks, double q_base, double sigma_p, double gamma, double kappa,
                   double tau, i64 best_bid, i64 best_ask) {
  const double sig2tau = sigma_p * sigma_p * tau;
  const double r = s_ticks - q_base * gamma * sig2tau;
  const double delta = gamma * sig2tau + (2.0 / gamma) * std::log(1.0 + gamma / kappa);

  i64 bid = static_cast<i64>(std::floor(r - delta / 2.0));
  i64 ask = static_cast<i64>(std::ceil(r + delta / 2.0));
  if (bid >= ask) ask = bid + 1;  // never cross (invariant)

  // Post-only clamp: a resting quote must not cross the current market.
  if (best_ask > 0 && bid >= best_ask) bid = best_ask - 1;
  if (best_bid > 0 && ask <= best_bid) ask = best_bid + 1;
  if (bid >= ask) bid = ask - 1;  // re-establish the invariant after clamping

  return Quote{bid, ask, r, delta};
}

}  // namespace asmm
