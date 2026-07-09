#include <catch2/catch_test_macros.hpp>

#include "engine/backoff.hpp"

using namespace asmm;

TEST_CASE("backoff: grows exponentially, capped, within jitter band", "[backoff]") {
  Backoff b(/*initial_ms=*/500, /*max_ms=*/30000, /*multiplier=*/2.0, /*seed=*/42);

  // base sequence 500, 1000, 2000, 4000, ... capped at 30000. Each delay is
  // within +/-20% of its (capped) base.
  const i64 bases[] = {500, 1000, 2000, 4000, 8000, 16000, 30000, 30000};
  for (i64 base : bases) {
    const i64 d = b.NextDelayMs();
    REQUIRE(d >= static_cast<i64>(base * 0.8));
    REQUIRE(d <= static_cast<i64>(base * 1.2));
  }
}

TEST_CASE("backoff: reset returns to attempt 0", "[backoff]") {
  Backoff b(500, 30000, 2.0, 7);
  b.NextDelayMs();
  b.NextDelayMs();
  REQUIRE(b.attempt() == 2);
  b.Reset();
  REQUIRE(b.attempt() == 0);
  const i64 d = b.NextDelayMs();
  REQUIRE(d >= 400);
  REQUIRE(d <= 600);  // back to the 500ms base band
}

TEST_CASE("backoff: deterministic for a fixed seed", "[backoff]") {
  Backoff a(500, 30000, 2.0, 123);
  Backoff b(500, 30000, 2.0, 123);
  for (int i = 0; i < 10; ++i) REQUIRE(a.NextDelayMs() == b.NextDelayMs());
}
