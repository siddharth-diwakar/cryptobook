#pragma once

#include <deque>
#include <string>
#include <vector>

#include "exchange/net_transport.hpp"

namespace asmm {

// In-memory ISignedRestClient for CI. Records every request it is handed (so a
// test can assert the exact signed query the gateway built) and returns scripted
// HttpResults in order (defaulting to a 200 "{}" when the script is exhausted).
class FakeSignedRest : public ISignedRestClient {
 public:
  struct Call {
    HttpMethod method;
    std::string host;
    std::string path;
    std::string query;
    bool sign;
  };

  std::vector<Call> calls;
  std::deque<HttpResult> scripted;
  i64 offset_ms = 0;

  void PushOk(const std::string& body = "{}") {
    HttpResult r;
    r.status = 200;
    r.body = body;
    scripted.push_back(r);
  }
  void PushResult(const HttpResult& r) { scripted.push_back(r); }

  HttpResult Request(HttpMethod method, const std::string& host, const std::string& path,
                     const std::string& query, bool sign) override {
    calls.push_back(Call{method, host, path, query, sign});
    if (!scripted.empty()) {
      HttpResult r = scripted.front();
      scripted.pop_front();
      return r;
    }
    HttpResult r;
    r.status = 200;
    r.body = "{}";
    return r;
  }

  void SetTimeOffsetMs(i64 offset) override { offset_ms = offset; }
};

}  // namespace asmm
