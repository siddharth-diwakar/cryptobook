#include "engine/token_bucket.hpp"

#include <algorithm>

namespace asmm {

TokenBucket::TokenBucket(double capacity, double refill_per_s)
    : capacity_(capacity), refill_per_s_(refill_per_s), tokens_(capacity), last_ns_(0) {}

double TokenBucket::available(i64 now_ns) const {
  const double elapsed_s = last_ns_ == 0 ? 0.0 : static_cast<double>(now_ns - last_ns_) / 1e9;
  const double refilled = tokens_ + std::max(0.0, elapsed_s) * refill_per_s_;
  return std::min(capacity_, refilled);
}

bool TokenBucket::TryTake(double n, i64 now_ns) {
  tokens_ = available(now_ns);
  last_ns_ = now_ns;
  if (tokens_ + 1e-9 < n) return false;
  tokens_ -= n;
  return true;
}

void TokenBucket::SetUsed(double used, i64 now_ns) {
  tokens_ = std::clamp(capacity_ - used, 0.0, capacity_);
  last_ns_ = now_ns;
}

}  // namespace asmm
