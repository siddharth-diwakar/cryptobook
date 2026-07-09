#include "engine/backoff.hpp"

#include <algorithm>
#include <cmath>

namespace asmm {

i64 Backoff::NextDelayMs() {
  double base = static_cast<double>(initial_ms_) * std::pow(multiplier_, attempt_);
  base = std::min(base, static_cast<double>(max_ms_));
  ++attempt_;
  std::uniform_real_distribution<double> jitter(0.8, 1.2);
  return static_cast<i64>(base * jitter(rng_));
}

}  // namespace asmm
