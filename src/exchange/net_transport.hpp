#pragma once

#include <optional>
#include <string>

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

}  // namespace asmm
