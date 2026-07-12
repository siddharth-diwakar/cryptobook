#pragma once

#include <cmath>

#include "core/types.hpp"

namespace asmm {

// EWMA of 1-second mid log-return variance, sampled on EVENT TIME (recorded
// Binance ts_exchange_ms) — never wall clock — so it is replay-deterministic.
// Output: relative volatility per sqrt-second.
class SigmaEwma {
 public:
  SigmaEwma(double halflife_s, int min_samples)
      : lambda_(std::exp(-std::log(2.0) / halflife_s)), min_samples_(min_samples) {}

  // Feed a mid observation with its exchange timestamp (ms). Takes at most one
  // sample per integer-second bucket. Returns true iff a new sample was taken.
  bool Observe(i64 ts_exchange_ms, double mid_ticks);

  // On snapshot resync (book cleared): keep the variance/warmup, but restart mid
  // continuity so the next sample doesn't span the gap.
  void ResetContinuity() {
    have_last_ = false;
    last_bucket_ = -1;
  }

  bool ready() const { return samples_ >= min_samples_; }
  int samples() const { return samples_; }
  double sigma_r() const { return std::sqrt(var_); }

 private:
  double lambda_;
  int min_samples_;
  double var_{0.0};
  i64 last_bucket_{-1};
  double last_mid_{0.0};
  bool have_last_{false};
  int samples_{0};
};

}  // namespace asmm
