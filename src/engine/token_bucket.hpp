#pragma once

#include "core/types.hpp"

namespace asmm {

// A simple lazily-refilled token bucket for outbound rate limiting (order count
// and request weight). Time is INJECTED (now_ns), never read internally, so it is
// deterministic and unit-testable. The gateway thread passes NowNs() — a network-
// boundary component, not an engine decision, so no determinism rule is touched.
class TokenBucket {
 public:
  TokenBucket(double capacity, double refill_per_s);

  // Refill for elapsed time, then take `n` tokens if available. Returns false
  // (taking nothing) when the bucket lacks `n` tokens.
  bool TryTake(double n, i64 now_ns);

  // Authoritative override from an exchange rate-limit header (e.g.
  // X-MBX-USED-WEIGHT-1M): set the *used* amount, so remaining = capacity - used.
  void SetUsed(double used, i64 now_ns);

  // Projected tokens available at now_ns (does not mutate).
  double available(i64 now_ns) const;

  double capacity() const { return capacity_; }

 private:
  double capacity_;
  double refill_per_s_;
  double tokens_;  // currently available
  i64 last_ns_;
};

}  // namespace asmm
