#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string>

#include "exchange/signing.hpp"

using namespace asmm;

TEST_CASE("signing: HMAC-SHA256 matches Binance's published example", "[signing]") {
  // The apiKey/secret + totalParams from the Binance SIGNED-endpoint docs. The
  // expected digest was cross-verified with the `openssl dgst -sha256 -hmac`
  // CLI and Python's hmac module (all three agree). If this drifts, our order
  // signatures are wrong and every order would be rejected -1022.
  const std::string secret = "NhqPtmdSJYdKjVHjA7PZj4Mge3R5YNiP1e3UZjInClVN65XAbvqqM6A7H5fATj0";
  const std::string payload =
      "symbol=LTCBTC&side=BUY&type=LIMIT&timeInForce=GTC&quantity=1&price=0.1"
      "&recvWindow=5000&timestamp=1499827319559";
  REQUIRE(HmacSha256Hex(secret, payload) ==
          "b89008e7051ffbf2242be7dc5ae67fd146e6430688627b802c0cbec146e46aef");
}

TEST_CASE("signing: HostOf strips scheme, path, port, userinfo", "[signing]") {
  REQUIRE(HostOf("https://testnet.binance.vision") == "testnet.binance.vision");
  REQUIRE(HostOf("wss://testnet.binance.vision/ws") == "testnet.binance.vision");
  REQUIRE(HostOf("https://testnet.binance.vision:443/api/v3/order") == "testnet.binance.vision");
  REQUIRE(HostOf("https://user:pass@testnet.binance.vision/x") == "testnet.binance.vision");
  REQUIRE(HostOf("testnet.binance.vision") == "testnet.binance.vision");
}

TEST_CASE("signing: testnet allowlist accepts only the testnet host", "[signing]") {
  REQUIRE(IsTestnetOrderHost("testnet.binance.vision"));
  REQUIRE(IsTestnetOrderHost("api3.testnet.binance.vision"));

  // Golden rule #1: production and look-alikes must be refused.
  REQUIRE_FALSE(IsTestnetOrderHost("api.binance.com"));
  REQUIRE_FALSE(IsTestnetOrderHost("stream.binance.com"));
  REQUIRE_FALSE(IsTestnetOrderHost("testnet.binance.vision.evil.com"));
  REQUIRE_FALSE(IsTestnetOrderHost("eviltestnet.binance.vision"));
  REQUIRE_FALSE(IsTestnetOrderHost("TESTNET.binance.vision"));
  REQUIRE_FALSE(IsTestnetOrderHost(""));

  // The end-to-end guard the config will use: URL -> host -> allowlist.
  REQUIRE(IsTestnetOrderHost(HostOf("https://testnet.binance.vision")));
  REQUIRE_FALSE(IsTestnetOrderHost(HostOf("https://api.binance.com")));
}

TEST_CASE("signing: client-id encode/decode round-trips", "[signing]") {
  REQUIRE(EncodeClientId("asmm", 0) == "asmm-0");
  REQUIRE(EncodeClientId("asmm", 123456) == "asmm-123456");

  REQUIRE(DecodeClientId("asmm", "asmm-123456") == std::optional<u64>(123456));
  REQUIRE(DecodeClientId("asmm", "asmm-0") == std::optional<u64>(0));

  // Wrong prefix, missing separator, non-numeric, or empty id -> rejected.
  REQUIRE(DecodeClientId("asmm", "other-1") == std::nullopt);
  REQUIRE(DecodeClientId("asmm", "asmm1") == std::nullopt);
  REQUIRE(DecodeClientId("asmm", "asmm-") == std::nullopt);
  REQUIRE(DecodeClientId("asmm", "asmm-12x") == std::nullopt);
  REQUIRE(DecodeClientId("asmm", "asmm-12.3") == std::nullopt);
}

TEST_CASE("signing: ScaledToDecimal is the inverse of DecimalToScaled", "[signing]") {
  REQUIRE(ScaledToDecimal(6512345, 2) == "65123.45");
  REQUIRE(ScaledToDecimal(10, 5) == "0.00010");
  REQUIRE(ScaledToDecimal(0, 5) == "0.00000");
  REQUIRE(ScaledToDecimal(100000, 5) == "1.00000");
  REQUIRE(ScaledToDecimal(6500000, 2) == "65000.00");
  REQUIRE(ScaledToDecimal(5, 0) == "5");
  REQUIRE(ScaledToDecimal(-6512345, 2) == "-65123.45");
}
