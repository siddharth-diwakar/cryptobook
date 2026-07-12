#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "core/types.hpp"
#include "strategy/sigma_ewma.hpp"

using namespace asmm;

TEST_CASE("sigma: at most one sample per second bucket", "[sigma]") {
  SigmaEwma s(60.0, 1);
  REQUIRE_FALSE(s.Observe(1000, 100.0));  // seeds baseline
  REQUIRE_FALSE(s.Observe(1500, 101.0));  // same bucket (1) -> no sample
  REQUIRE(s.Observe(2000, 101.0));        // bucket 2 -> sample
  REQUIRE(s.samples() == 1);
}

TEST_CASE("sigma: warmup gate", "[sigma]") {
  SigmaEwma s(60.0, 3);
  s.Observe(0, 100);
  REQUIRE_FALSE(s.ready());
  s.Observe(1000, 100.1);
  s.Observe(2000, 100.2);
  s.Observe(3000, 100.1);
  REQUIRE(s.samples() == 3);
  REQUIRE(s.ready());
}

TEST_CASE("sigma: EWMA variance matches hand computation", "[sigma]") {
  const double hl = 10.0;
  const double lambda = std::exp(-std::log(2.0) / hl);
  SigmaEwma s(hl, 1);
  s.Observe(0, 100.0);     // seed
  s.Observe(1000, 101.0);  // ret1 over 1s
  const double r1 = std::log(101.0 / 100.0);
  double var = (1.0 - lambda) * r1 * r1;
  REQUIRE(std::abs(s.sigma_r() - std::sqrt(var)) < 1e-12);
  s.Observe(2000, 100.0);
  const double r2 = std::log(100.0 / 101.0);
  var = lambda * var + (1.0 - lambda) * r2 * r2;
  REQUIRE(std::abs(s.sigma_r() - std::sqrt(var)) < 1e-12);
}

TEST_CASE("sigma: a k-second gap scales the return by 1/sqrt(k)", "[sigma]") {
  // halflife -> 0 makes lambda ~ 0, so var == last per-1s return squared.
  SigmaEwma s(1e-6, 1);
  s.Observe(0, 100.0);
  s.Observe(4000, 110.0);  // 4-second gap
  const double expected_1s = std::log(110.0 / 100.0) / std::sqrt(4.0);
  REQUIRE(std::abs(s.sigma_r() - std::abs(expected_1s)) < 1e-9);
}

TEST_CASE("sigma: deterministic across two identical runs", "[sigma]") {
  auto run = [] {
    SigmaEwma s(30.0, 1);
    i64 t = 0;
    double m = 100.0;
    for (int i = 0; i < 100; ++i) {
      t += 1000;
      m *= (i % 2 ? 1.001 : 0.999);
      s.Observe(t, m);
    }
    return s.sigma_r();
  };
  REQUIRE(run() == run());  // bit-identical
}
