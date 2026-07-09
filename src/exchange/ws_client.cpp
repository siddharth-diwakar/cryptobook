#include "exchange/ws_client.hpp"

#include <sys/socket.h>
#include <sys/time.h>

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <chrono>
#include <exception>
#include <memory>
#include <string>

namespace asmm {
namespace {
namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

// Synchronous WSS client. depth@100ms guarantees >=10 msg/s, so a read timeout
// (SO_RCVTIMEO) means the connection is dead -> the caller reconnects. A timeout
// leaves the stream unusable, which is fine because the response is a full
// reconnect anyway.
class BeastWsClient : public IWsTransport {
 public:
  explicit BeastWsClient(int read_timeout_s) : read_timeout_s_(read_timeout_s), ctx_(SslCtx()) {}
  ~BeastWsClient() override { Close(); }

  bool Connect(const std::string& host, const std::string& target, std::string& err) override {
    try {
      ws_ = std::make_unique<Stream>(ioc_, ctx_);
      auto& lowest = beast::get_lowest_layer(*ws_);

      tcp::resolver resolver(ioc_);
      const auto results = resolver.resolve(host, "443");
      lowest.expires_after(std::chrono::seconds(30));  // connect + handshake budget
      lowest.connect(results);

      if (!SSL_set_tlsext_host_name(ws_->next_layer().native_handle(), host.c_str())) {
        err = "SNI setup failed";
        return false;
      }
      ws_->next_layer().set_verify_callback(ssl::host_name_verification(host));
      ws_->next_layer().handshake(ssl::stream_base::client);

      ws_->set_option(websocket::stream_base::decorator([](websocket::request_type& req) {
        req.set(beast::http::field::user_agent, "asmm/0.1");
      }));
      ws_->handshake(host, target);

      // Off Beast's timer; SO_RCVTIMEO handles per-read dead-connection detection.
      lowest.expires_never();
      struct timeval tv;
      tv.tv_sec = read_timeout_s_;
      tv.tv_usec = 0;
      ::setsockopt(lowest.socket().native_handle(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
      return true;
    } catch (const std::exception& e) {
      err = e.what();
      return false;
    }
  }

  WsFrame Read() override {
    if (!ws_) return {WsReadStatus::kError, ""};
    try {
      buffer_.clear();
      ws_->read(buffer_);
      return {WsReadStatus::kOk, beast::buffers_to_string(buffer_.data())};
    } catch (const beast::system_error& se) {
      const auto& ec = se.code();
      if (ec == websocket::error::closed) return {WsReadStatus::kClosed, ""};
      if (ec == net::error::timed_out || ec.value() == EAGAIN || ec.value() == EWOULDBLOCK) {
        return {WsReadStatus::kTimeout, ""};
      }
      return {WsReadStatus::kError, ""};
    } catch (const std::exception&) {
      return {WsReadStatus::kError, ""};
    }
  }

  void Close() override {
    if (!ws_) return;
    beast::error_code ec;
    ws_->close(websocket::close_code::normal, ec);
    ws_.reset();
  }

 private:
  using Stream = websocket::stream<beast::ssl_stream<beast::tcp_stream>>;

  static ssl::context SslCtx() {
    ssl::context ctx(ssl::context::tlsv12_client);
    ctx.set_default_verify_paths();
    ctx.set_verify_mode(ssl::verify_peer);
    return ctx;
  }

  int read_timeout_s_;
  net::io_context ioc_;
  ssl::context ctx_;
  std::unique_ptr<Stream> ws_;
  beast::flat_buffer buffer_;
};

}  // namespace

std::unique_ptr<IWsTransport> MakeBeastWsClient(int read_timeout_s) {
  return std::make_unique<BeastWsClient>(read_timeout_s);
}

}  // namespace asmm
