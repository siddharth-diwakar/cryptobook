#include "exchange/rest_client.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <chrono>
#include <exception>
#include <memory>
#include <string>

namespace asmm {
namespace {
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

class BeastRestClient : public IRestClient {
 public:
  explicit BeastRestClient(int timeout_s) : timeout_s_(timeout_s) {}

  std::optional<std::string> Get(const std::string& host, const std::string& target,
                                 std::string& err) override {
    try {
      net::io_context ioc;
      ssl::context ctx(ssl::context::tlsv12_client);
      ctx.set_default_verify_paths();
      ctx.set_verify_mode(ssl::verify_peer);

      tcp::resolver resolver(ioc);
      beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);

      if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
        err = "SNI setup failed";
        return std::nullopt;
      }
      stream.set_verify_callback(ssl::host_name_verification(host));

      const auto results = resolver.resolve(host, "443");
      beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(timeout_s_));
      beast::get_lowest_layer(stream).connect(results);
      stream.handshake(ssl::stream_base::client);

      http::request<http::empty_body> req{http::verb::get, target, 11};
      req.set(http::field::host, host);
      req.set(http::field::user_agent, "asmm/0.1");
      beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(timeout_s_));
      http::write(stream, req);

      beast::flat_buffer buffer;
      http::response<http::string_body> res;
      http::read(stream, buffer, res);

      beast::error_code ec;
      stream.shutdown(ec);  // best-effort; server often closes first

      if (res.result_int() != 200) {
        err = "HTTP " + std::to_string(res.result_int());
        return std::nullopt;
      }
      return std::move(res.body());
    } catch (const std::exception& e) {
      err = e.what();
      return std::nullopt;
    }
  }

 private:
  int timeout_s_;
};

}  // namespace

std::unique_ptr<IRestClient> MakeBeastRestClient(int timeout_s) {
  return std::make_unique<BeastRestClient>(timeout_s);
}

}  // namespace asmm
