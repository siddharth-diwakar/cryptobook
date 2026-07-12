#include <catch2/catch_test_macros.hpp>

#include "exchange/net_transport.hpp"
#include "fake_signed_rest.hpp"

using namespace asmm;

// The Beast signed client itself is network-only (CI-exempt, smoke-tested). What
// CI covers here is the interface contract + the FakeSignedRest harness that the
// Stage B gateway/user-data tests drive.

TEST_CASE("signed-rest: fake records calls and returns scripted results", "[signed_rest]") {
  FakeSignedRest fake;
  fake.PushOk(R"({"listenKey":"abc"})");

  HttpResult ack;
  ack.status = 200;
  ack.body = R"({"orderId":123})";
  ack.used_weight = 6;
  fake.PushResult(ack);

  const HttpResult r1 = fake.Request(HttpMethod::kPost, "testnet.binance.vision",
                                     "/api/v3/userDataStream", "", false);
  REQUIRE(r1.ok());
  REQUIRE(r1.body == R"({"listenKey":"abc"})");

  const HttpResult r2 = fake.Request(HttpMethod::kPost, "testnet.binance.vision", "/api/v3/order",
                                     "symbol=BTCUSDT&side=BUY", true);
  REQUIRE(r2.ok());
  REQUIRE(r2.used_weight == 6);

  REQUIRE(fake.calls.size() == 2);
  REQUIRE(fake.calls[0].sign == false);  // USER_STREAM: key only, no signature
  REQUIRE(fake.calls[0].path == "/api/v3/userDataStream");
  REQUIRE(fake.calls[1].sign == true);  // order: signed
  REQUIRE(fake.calls[1].query == "symbol=BTCUSDT&side=BUY");
}

TEST_CASE("signed-rest: exhausted script yields a default 200", "[signed_rest]") {
  FakeSignedRest fake;
  const HttpResult r = fake.Request(HttpMethod::kDelete, "testnet.binance.vision",
                                    "/api/v3/openOrders", "symbol=BTCUSDT", true);
  REQUIRE(r.ok());
  REQUIRE(r.body == "{}");
}

TEST_CASE("signed-rest: HttpResult.ok() only for 200", "[signed_rest]") {
  HttpResult blocked;  // default: status 0 (host refused / transport error)
  REQUIRE_FALSE(blocked.ok());
  HttpResult rejected;
  rejected.status = 400;
  REQUIRE_FALSE(rejected.ok());
  HttpResult good;
  good.status = 200;
  REQUIRE(good.ok());
}
