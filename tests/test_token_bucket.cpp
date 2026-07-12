#include <catch2/catch_test_macros.hpp>

#include "engine/token_bucket.hpp"

using namespace asmm;

namespace {
constexpr i64 kSec = 1'000'000'000;
}

TEST_CASE("token bucket: starts full and depletes", "[token_bucket]") {
  TokenBucket b(10.0, 1.0);  // cap 10, refill 1/s
  REQUIRE(b.available(kSec) == 10.0);
  REQUIRE(b.TryTake(10.0, kSec));
  REQUIRE_FALSE(b.TryTake(1.0, kSec));  // empty, no refill yet
}

TEST_CASE("token bucket: refills over time, capped at capacity", "[token_bucket]") {
  TokenBucket b(10.0, 1.0);
  REQUIRE(b.TryTake(10.0, kSec));       // drain at t=1s
  REQUIRE_FALSE(b.TryTake(1.0, kSec));  // still empty
  REQUIRE(b.TryTake(2.0, 3 * kSec));    // 2s later -> 2 tokens refilled
  REQUIRE_FALSE(b.TryTake(0.5, 3 * kSec));
  // Long idle refills only up to capacity, never beyond.
  REQUIRE(b.available(1000 * kSec) == 10.0);
}

TEST_CASE("token bucket: SetUsed overrides from exchange header", "[token_bucket]") {
  TokenBucket b(1000.0, 1000.0 / 60.0);  // weight/min
  b.SetUsed(950.0, 5 * kSec);
  REQUIRE(b.available(5 * kSec) == 50.0);  // remaining = cap - used
  REQUIRE(b.TryTake(50.0, 5 * kSec));
  REQUIRE_FALSE(b.TryTake(1.0, 5 * kSec));

  // Used exceeding capacity clamps to empty, not negative.
  b.SetUsed(1200.0, 6 * kSec);
  REQUIRE(b.available(6 * kSec) == 0.0);
}
