#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "strategy/as_quoter.hpp"

using namespace asmm;

TEST_CASE("as_quoter: hand-computed reservation and spread", "[quoter]") {
  // s=100, q=5, sigma_p=2, gamma=0.1, kappa=1.5, tau=1, no book.
  // r = 100 - 5*0.1*(2^2)*1 = 100 - 2 = 98
  // delta = 0.1*4 + (2/0.1)*ln(1 + 0.1/1.5) = 0.4 + 20*ln(1.0666667) = 0.4 + 1.290772
  const Quote q = ComputeQuote(100, 5, 2, 0.1, 1.5, 1.0, 0, 0);
  REQUIRE(std::abs(q.reservation - 98.0) < 1e-9);
  REQUIRE(std::abs(q.delta_total - (0.4 + 20.0 * std::log(1.0 + 0.1 / 1.5))) < 1e-9);
  REQUIRE(q.bid_px == 97);  // floor(98 - 0.845386)
  REQUIRE(q.ask_px == 99);  // ceil(98 + 0.845386)
}

TEST_CASE("as_quoter: inventory shifts reservation symmetrically", "[quoter]") {
  const Quote lng = ComputeQuote(100, 5, 2, 0.1, 1.5, 1.0, 0, 0);
  const Quote sht = ComputeQuote(100, -5, 2, 0.1, 1.5, 1.0, 0, 0);
  REQUIRE(lng.reservation < 100.0);  // long -> shade down
  REQUIRE(sht.reservation > 100.0);  // short -> shade up
  REQUIRE(std::abs((100.0 - lng.reservation) - (sht.reservation - 100.0)) < 1e-9);
}

TEST_CASE("as_quoter: wider with higher sigma or gamma", "[quoter]") {
  const Quote base = ComputeQuote(100, 0, 2, 0.1, 1.5, 1.0, 0, 0);
  REQUIRE(ComputeQuote(100, 0, 4, 0.1, 1.5, 1.0, 0, 0).delta_total > base.delta_total);
  REQUIRE(ComputeQuote(100, 0, 2, 0.5, 1.5, 1.0, 0, 0).delta_total > base.delta_total);
}

TEST_CASE("as_quoter: doubling sigma quadruples the sigma^2 terms", "[quoter]") {
  const double g = 0.1, k = 1.5, tau = 1.0;
  const Quote a = ComputeQuote(100, 5, 2, g, k, tau, 0, 0);
  const Quote b = ComputeQuote(100, 5, 4, g, k, tau, 0, 0);
  // Inventory r-shift = q*gamma*sigma^2*tau.
  REQUIRE(std::abs((100.0 - b.reservation) / (100.0 - a.reservation) - 4.0) < 1e-9);
  // Spread term gamma*sigma^2*tau (delta minus the constant liquidity term).
  const double liq = (2.0 / g) * std::log(1.0 + g / k);
  REQUIRE(std::abs((b.delta_total - liq) / (a.delta_total - liq) - 4.0) < 1e-9);
}

TEST_CASE("as_quoter: never crosses and post-only clamps", "[quoter]") {
  // Extreme short pushes r far above the book -> bid clamps to best_ask-1.
  const Quote c = ComputeQuote(100, -1000, 2, 0.1, 1.5, 1.0, /*bb=*/99, /*ba=*/101);
  REQUIRE(c.bid_px == 100);  // best_ask - 1
  REQUIRE(c.bid_px < c.ask_px);
  // Extreme long pushes r far below -> ask clamps to best_bid+1.
  const Quote d = ComputeQuote(100, 1000, 2, 0.1, 1.5, 1.0, 99, 101);
  REQUIRE(d.ask_px == 100);  // best_bid + 1
  REQUIRE(d.bid_px < d.ask_px);
}
