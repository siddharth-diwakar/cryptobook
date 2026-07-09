#pragma once

#include <random>

#include "core/types.hpp"

namespace asmm {

// Exponential backoff with jitter for WS reconnects. Pure and deterministic
// given a seed (injectable so tests assert exact delays).
class Backoff {
 public:
  Backoff(i64 initial_ms, i64 max_ms, double multiplier, u32 seed)
      : initial_ms_(initial_ms), max_ms_(max_ms), multiplier_(multiplier), rng_(seed) {}

  // Delay for the current attempt (initial * multiplier^attempt, capped at max),
  // with +/-20% jitter; advances the attempt counter.
  i64 NextDelayMs();

  void Reset() { attempt_ = 0; }
  u32 attempt() const { return attempt_; }

 private:
  i64 initial_ms_;
  i64 max_ms_;
  double multiplier_;
  u32 attempt_{0};
  std::minstd_rand rng_;
};

}  // namespace asmm
