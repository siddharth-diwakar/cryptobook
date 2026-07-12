#include "exchange/signing.hpp"

#include <openssl/hmac.h>

#include <array>
#include <charconv>
#include <cstddef>
#include <string>

namespace asmm {
namespace {

constexpr char kHex[] = "0123456789abcdef";

}  // namespace

std::string HmacSha256Hex(std::string_view secret, std::string_view payload) {
  std::array<unsigned char, 32> mac{};
  unsigned int mac_len = 0;
  HMAC(EVP_sha256(), secret.data(), static_cast<int>(secret.size()),
       reinterpret_cast<const unsigned char*>(payload.data()), payload.size(), mac.data(),
       &mac_len);

  std::string out;
  out.reserve(mac_len * 2);
  for (unsigned int i = 0; i < mac_len; ++i) {
    out.push_back(kHex[mac[i] >> 4]);
    out.push_back(kHex[mac[i] & 0x0F]);
  }
  return out;
}

std::string HostOf(std::string_view url) {
  const auto scheme = url.find("://");
  std::string_view rest = (scheme == std::string_view::npos) ? url : url.substr(scheme + 3);
  // Host authority ends at the first '/', '?', or '#'.
  const auto end = rest.find_first_of("/?#");
  std::string_view authority = (end == std::string_view::npos) ? rest : rest.substr(0, end);
  // Strip userinfo (everything up to and including '@').
  const auto at = authority.rfind('@');
  if (at != std::string_view::npos) authority = authority.substr(at + 1);
  // Strip port.
  const auto colon = authority.rfind(':');
  if (colon != std::string_view::npos) authority = authority.substr(0, colon);
  return std::string(authority);
}

bool IsTestnetOrderHost(std::string_view host) {
  // Exact allowlist. The api1..api4 forms are documented alternates for the same
  // testnet; the bare host is what our config uses.
  return host == "testnet.binance.vision" || host == "api1.testnet.binance.vision" ||
         host == "api2.testnet.binance.vision" || host == "api3.testnet.binance.vision" ||
         host == "api4.testnet.binance.vision";
}

std::string EncodeClientId(std::string_view prefix, u64 id) {
  return std::string(prefix) + "-" + std::to_string(id);
}

std::optional<u64> DecodeClientId(std::string_view prefix, std::string_view client_order_id) {
  const std::size_t want = prefix.size() + 1;  // "<prefix>-"
  if (client_order_id.size() <= want) return std::nullopt;
  if (client_order_id.substr(0, prefix.size()) != prefix) return std::nullopt;
  if (client_order_id[prefix.size()] != '-') return std::nullopt;
  const std::string_view digits = client_order_id.substr(want);
  u64 value = 0;
  const auto* first = digits.data();
  const auto* last = digits.data() + digits.size();
  const auto res = std::from_chars(first, last, value);
  if (res.ec != std::errc{} || res.ptr != last) return std::nullopt;
  return value;
}

std::string ScaledToDecimal(i64 value, int scale) {
  const bool neg = value < 0;
  u64 mag = neg ? static_cast<u64>(-(value + 1)) + 1 : static_cast<u64>(value);  // safe |INT64_MIN|
  if (scale <= 0) {
    return (neg ? "-" : "") + std::to_string(mag);
  }
  u64 divisor = 1;
  for (int i = 0; i < scale; ++i) divisor *= 10;
  const u64 whole = mag / divisor;
  const u64 frac = mag % divisor;
  std::string frac_str = std::to_string(frac);
  if (static_cast<int>(frac_str.size()) < scale) {
    frac_str.insert(0, scale - static_cast<int>(frac_str.size()), '0');
  }
  return (neg ? "-" : "") + std::to_string(whole) + "." + frac_str;
}

}  // namespace asmm
