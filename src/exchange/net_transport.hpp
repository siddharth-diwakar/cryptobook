#pragma once

#include <optional>
#include <string>

#include "core/types.hpp"

namespace asmm {

enum class WsReadStatus { kOk, kTimeout, kClosed, kError };

struct WsFrame {
  WsReadStatus status;
  std::string payload;  // valid only when status == kOk
};

// Abstract WS transport, so the market-data driver can be tested against an
// in-memory fake with scripted frames (no network in CI).
class IWsTransport {
 public:
  virtual ~IWsTransport() = default;
  virtual bool Connect(const std::string& host, const std::string& target, std::string& err) = 0;
  virtual WsFrame Read() = 0;  // blocks up to the read timeout
  virtual void Close() = 0;
};

// Abstract blocking HTTPS GET client.
class IRestClient {
 public:
  virtual ~IRestClient() = default;
  // Response body on HTTP 200, or nullopt on any error/non-200 (err populated).
  virtual std::optional<std::string> Get(const std::string& host, const std::string& target,
                                         std::string& err) = 0;
};

enum class HttpMethod { kGet, kPost, kDelete };

// Full result of a signed request. `status == 0` means the request never reached
// the exchange (transport error, or a non-testnet host refused before any socket).
struct HttpResult {
  int status = 0;         // HTTP status code (0 = no HTTP response)
  std::string body;       // response body (may be an error JSON on 4xx)
  int retry_after_s = 0;  // parsed Retry-After header (429/418)
  int used_weight = 0;    // X-MBX-USED-WEIGHT-1M
  int order_count = 0;    // X-MBX-ORDER-COUNT-10S
  std::string err;        // populated when status == 0

  bool ok() const { return status == 200; }
};

// Abstract signed REST client for the Phase 5 order path. The concrete Beast
// implementation refuses any non-testnet host BEFORE opening a socket (golden
// rule #1), so signing production is structurally impossible. Tests drive an
// in-memory FakeSignedRest instead.
class ISignedRestClient {
 public:
  virtual ~ISignedRestClient() = default;

  // Issue a request to `host``path` with `query` params (URL-encoded, without a
  // leading '?'). When `sign` is true, the client appends recvWindow + timestamp
  // (with any clock offset), signs the whole query with HMAC-SHA256, appends the
  // signature, and sets X-MBX-APIKEY. When false, it sends only X-MBX-APIKEY
  // (USER_STREAM endpoints need the key but no signature).
  virtual HttpResult Request(HttpMethod method, const std::string& host, const std::string& path,
                             const std::string& query, bool sign) = 0;

  // Server-minus-local clock offset (ms), applied to every signed timestamp so a
  // skewed local clock doesn't trip -1021. Set from GET /api/v3/time at startup.
  virtual void SetTimeOffsetMs(i64 offset_ms) = 0;
};

}  // namespace asmm
