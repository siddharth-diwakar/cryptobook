#pragma once

#include <memory>

#include "exchange/net_transport.hpp"

namespace asmm {

// Factory for a blocking HTTPS GET client (Boost.Beast + OpenSSL). Beast/OpenSSL
// are hidden in the .cpp. One-shot connection per call — used for the depth
// snapshot and periodic cross-check, never on a latency-critical path.
std::unique_ptr<IRestClient> MakeBeastRestClient(int timeout_s);

}  // namespace asmm
