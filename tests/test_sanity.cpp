#include <boost/beast/version.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "engine/config.hpp"

TEST_CASE("toolchain: C++20 is enabled", "[sanity]") {
  REQUIRE(2 + 2 == 4);

  // std::span is C++20 — proves the standard is actually on, not just requested.
  std::vector<int> v{1, 2, 3, 4};
  const std::span<int> s{v};
  REQUIRE(s.size() == 4);

  // Designated initializers (C++20).
  struct Point {
    int x;
    int y;
  };
  const Point p{.x = 1, .y = 2};
  REQUIRE(p.x == 1);
  REQUIRE(p.y == 2);
}

TEST_CASE("deps: Boost.Beast is available", "[sanity][deps]") {
  // Proves the Boost FetchContent wiring compiles and links (used from Phase 2).
  REQUIRE(std::strlen(BOOST_BEAST_VERSION_STRING) > 0);
}

TEST_CASE("config: testnet.toml parses and is testnet-only", "[config]") {
  const std::string toml_path = std::string(ASMM_SOURCE_DIR) + "/config/testnet.toml";
  // No .env in CI; a missing .env must not throw.
  const asmm::AppConfig cfg = asmm::LoadConfig(toml_path, "/nonexistent/.env");

  REQUIRE(cfg.symbol == "BTCUSDT");
  // Golden rule #1: the order endpoint must be testnet.
  REQUIRE(cfg.rest_url.find("testnet") != std::string::npos);
}
