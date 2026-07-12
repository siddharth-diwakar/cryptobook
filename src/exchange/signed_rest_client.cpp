#include "exchange/signed_rest_client.hpp"

#include <atomic>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <charconv>
#include <chrono>
#include <exception>
#include <memory>
#include <string>

#include "exchange/signing.hpp"

namespace asmm {
namespace {
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

int HeaderInt(const http::response<http::string_body>& res, const char* name) {
  const auto it = res.find(name);
  if (it == res.end()) return 0;
  const std::string_view v = it->value();
  int out = 0;
  std::from_chars(v.data(), v.data() + v.size(), out);
  return out;
}

http::verb ToVerb(HttpMethod m) {
  switch (m) {
    case HttpMethod::kPost:
      return http::verb::post;
    case HttpMethod::kDelete:
      return http::verb::delete_;
    case HttpMethod::kGet:
    default:
      return http::verb::get;
  }
}

class BeastSignedRestClient : public ISignedRestClient {
 public:
  BeastSignedRestClient(std::string api_key, std::string api_secret, int recv_window_ms,
                        int timeout_s)
      : api_key_(std::move(api_key)),
        api_secret_(std::move(api_secret)),
        recv_window_ms_(recv_window_ms),
        timeout_s_(timeout_s) {}

  void SetTimeOffsetMs(i64 offset_ms) override {
    time_offset_ms_.store(offset_ms, std::memory_order_relaxed);
  }

  HttpResult Request(HttpMethod method, const std::string& host, const std::string& path,
                     const std::string& query, bool sign) override {
    HttpResult r;

    // Golden rule #1: refuse any non-testnet order host BEFORE opening a socket.
    if (!IsTestnetOrderHost(host)) {
      r.err = "refused non-testnet order host '" + host + "'";
      return r;  // status == 0
    }

    std::string params = query;
    if (sign) {
      if (!params.empty()) params += '&';
      const i64 ts = NowMs() + time_offset_ms_.load(std::memory_order_relaxed);
      params +=
          "recvWindow=" + std::to_string(recv_window_ms_) + "&timestamp=" + std::to_string(ts);
      params += "&signature=" + HmacSha256Hex(api_secret_, params);
    }
    const std::string target = params.empty() ? path : path + "?" + params;

    try {
      net::io_context ioc;
      ssl::context ctx(ssl::context::tlsv12_client);
      ctx.set_default_verify_paths();
      ctx.set_verify_mode(ssl::verify_peer);

      tcp::resolver resolver(ioc);
      beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);
      if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
        r.err = "SNI setup failed";
        return r;
      }
      stream.set_verify_callback(ssl::host_name_verification(host));

      const auto results = resolver.resolve(host, "443");
      beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(timeout_s_));
      beast::get_lowest_layer(stream).connect(results);
      stream.handshake(ssl::stream_base::client);

      http::request<http::empty_body> req{ToVerb(method), target, 11};
      req.set(http::field::host, host);
      req.set(http::field::user_agent, "asmm/0.1");
      req.set("X-MBX-APIKEY", api_key_);
      beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(timeout_s_));
      http::write(stream, req);

      beast::flat_buffer buffer;
      http::response<http::string_body> res;
      http::read(stream, buffer, res);

      beast::error_code ec;
      stream.shutdown(ec);  // best-effort

      r.status = res.result_int();
      r.body = std::move(res.body());
      r.retry_after_s = HeaderInt(res, "Retry-After");
      r.used_weight = HeaderInt(res, "X-MBX-USED-WEIGHT-1M");
      r.order_count = HeaderInt(res, "X-MBX-ORDER-COUNT-10S");
      return r;
    } catch (const std::exception& e) {
      r.status = 0;
      r.err = e.what();
      return r;
    }
  }

 private:
  static i64 NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
  }

  std::string api_key_;
  std::string api_secret_;
  int recv_window_ms_;
  int timeout_s_;
  std::atomic<i64> time_offset_ms_{0};
};

}  // namespace

std::unique_ptr<ISignedRestClient> MakeBeastSignedRestClient(std::string api_key,
                                                             std::string api_secret,
                                                             int recv_window_ms, int timeout_s) {
  return std::make_unique<BeastSignedRestClient>(std::move(api_key), std::move(api_secret),
                                                 recv_window_ms, timeout_s);
}

}  // namespace asmm
