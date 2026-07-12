#include "strategy/sigma_ewma.hpp"

namespace asmm {

bool SigmaEwma::Observe(i64 ts_exchange_ms, double mid_ticks) {
  if (mid_ticks <= 0.0) return false;
  const i64 bucket = ts_exchange_ms / 1000;
  if (bucket <= last_bucket_) return false;  // at most one sample per second

  if (!have_last_) {
    have_last_ = true;
    last_bucket_ = bucket;
    last_mid_ = mid_ticks;
    return false;  // first observation just seeds the baseline
  }

  const double k = static_cast<double>(bucket - last_bucket_);  // seconds elapsed (>= 1)
  const double ret = std::log(mid_ticks / last_mid_);
  const double ret_1s = ret / std::sqrt(k);  // per-sqrt-second equivalent
  var_ = lambda_ * var_ + (1.0 - lambda_) * ret_1s * ret_1s;

  last_bucket_ = bucket;
  last_mid_ = mid_ticks;
  ++samples_;
  return true;
}

}  // namespace asmm
