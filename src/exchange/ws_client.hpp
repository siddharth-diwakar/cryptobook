#pragma once

#include <memory>

#include "exchange/net_transport.hpp"

namespace asmm {

// Factory for a synchronous WSS transport (Boost.Beast + OpenSSL). Beast/OpenSSL
// are fully hidden in the .cpp so consumers (and their -Werror builds) never see
// those headers. Dead-connection detection is via SO_RCVTIMEO on the socket.
std::unique_ptr<IWsTransport> MakeBeastWsClient(int read_timeout_s);

}  // namespace asmm
