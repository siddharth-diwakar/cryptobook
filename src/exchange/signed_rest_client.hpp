#pragma once

#include <memory>
#include <string>

#include "exchange/net_transport.hpp"

namespace asmm {

// Factory for a blocking signed HTTPS client (Boost.Beast + OpenSSL). Beast is
// hidden in the .cpp. One-shot TLS connection per request — used for order
// placement/cancel and the user-data listenKey lifecycle, never on a latency-
// critical path. The client refuses any non-testnet order host before connecting.
std::unique_ptr<ISignedRestClient> MakeBeastSignedRestClient(std::string api_key,
                                                             std::string api_secret,
                                                             int recv_window_ms, int timeout_s);

}  // namespace asmm
