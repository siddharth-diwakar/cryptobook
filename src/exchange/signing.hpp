#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "core/types.hpp"

// Pure request-signing + safety helpers for the Phase 5 order path. No Boost/Beast
// (links only OpenSSL::Crypto for HMAC), so every function here is CI-testable
// without a network. The Beast signed REST client (asmm_net) calls into these.
namespace asmm {

// Lowercase-hex HMAC-SHA256 of `payload` under `secret` — exactly what Binance
// SIGNED endpoints expect as the `signature` parameter.
std::string HmacSha256Hex(std::string_view secret, std::string_view payload);

// Golden rule #1, enforced structurally: only an exact-match testnet order host
// may ever be signed against. Replaces the old substring find("testnet") guard,
// which would accept hosts like "testnet.binance.vision.evil.com". Case-sensitive
// (DNS hosts are lowercase in our config; we don't want "TESTNET..." to slip a
// prod host through some case-folding quirk).
bool IsTestnetOrderHost(std::string_view host);

// Extract the host from a URL like "https://testnet.binance.vision" or
// "wss://testnet.binance.vision/ws". Returns the host with scheme, path, port,
// and any userinfo stripped. Empty on malformed input.
std::string HostOf(std::string_view url);

// client_id <-> Binance newClientOrderId. Encoding is "<prefix>-<decimal>", which
// is alphanumeric+'-' and well under Binance's 36-char clientOrderId limit.
std::string EncodeClientId(std::string_view prefix, u64 id);
std::optional<u64> DecodeClientId(std::string_view prefix, std::string_view client_order_id);

// Inverse of DecimalToScaled: render a scaled integer as a fixed-point decimal
// string with `scale` fractional digits (e.g. value=6512345, scale=2 -> "65123.45";
// value=10, scale=5 -> "0.00010"). Used to build order price/qty parameters.
std::string ScaledToDecimal(i64 value, int scale);

}  // namespace asmm
