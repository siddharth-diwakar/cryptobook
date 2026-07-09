#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <deque>
#include <string>
#include <thread>
#include <vector>

#include "engine/engine.hpp"
#include "engine/market_data_thread.hpp"
#include "exchange/net_transport.hpp"

using namespace asmm;

namespace {

// Scripted fake WS: returns queued frames, then a terminal status forever.
class FakeWs : public IWsTransport {
 public:
  std::deque<WsFrame> frames;
  int connect_calls = 0;
  bool connect_ok = true;

  bool Connect(const std::string&, const std::string&, std::string& err) override {
    ++connect_calls;
    if (!connect_ok) {
      err = "refused";
      return false;
    }
    return true;
  }
  WsFrame Read() override {
    if (frames.empty()) return {WsReadStatus::kClosed, ""};
    WsFrame f = frames.front();
    frames.pop_front();
    return f;
  }
  void Close() override {}
};

class FakeRest : public IRestClient {
 public:
  std::deque<std::string> bodies;
  std::optional<std::string> Get(const std::string&, const std::string&, std::string&) override {
    if (bodies.empty()) return std::nullopt;
    std::string b = bodies.front();
    bodies.pop_front();
    return b;
  }
};

std::string Snapshot(u64 last_id) {
  return R"({"lastUpdateId":)" + std::to_string(last_id) +
         R"(,"bids":[["100.00","1.0"]],"asks":[["101.00","2.0"]]})";
}

WsFrame Diff(u64 U, u64 u) {
  return {WsReadStatus::kOk, R"({"e":"depthUpdate","E":1,"s":"BTCUSDT","U":)" + std::to_string(U) +
                                 R"(,"u":)" + std::to_string(u) +
                                 R"(,"b":[["100.00","3.0"]],"a":[]})"};
}

MarketDataParams Params() {
  MarketDataParams p;
  p.filters = SymbolFilters{2, 5};
  p.backoff_initial_ms = 1;  // keep tests fast
  p.backoff_max_ms = 2;
  return p;
}

}  // namespace

TEST_CASE("md: bootstraps, drops stale, brackets, emits live", "[md]") {
  FakeWs ws;
  FakeRest rest;
  rest.bodies.push_back(Snapshot(1000));
  ws.frames.push_back(Diff(990, 995));    // u < L -> drop
  ws.frames.push_back(Diff(1000, 1005));  // bracket -> emit + live
  ws.frames.push_back(Diff(1006, 1010));  // live emit
  ws.frames.push_back({WsReadStatus::kClosed, ""});

  auto queue = std::make_unique<MarketQueue>();
  MarketDataThread md(ws, rest, Params(), *queue);
  std::atomic<bool> stop{false};
  std::thread t([&] { md.Run(stop); });

  // Let it process, then stop.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  stop = true;
  t.join();

  REQUIRE(md.counters().snapshots_fetched >= 1);
  REQUIRE(md.counters().events_emitted >= 3);  // snapshot frag + bracket + 1 live

  // Drain queue and confirm a snapshot-start then diffs arrived.
  MarketEvent ev;
  bool saw_snapshot_start = false;
  bool saw_diff = false;
  while (queue->try_pop(ev)) {
    if (ev.flags & kFlagSnapshotStart) saw_snapshot_start = true;
    if (ev.kind == EventKind::kDepthDiff) saw_diff = true;
  }
  REQUIRE(saw_snapshot_start);
  REQUIRE(saw_diff);
}

TEST_CASE("md: detects a gap and resyncs with a fresh snapshot", "[md]") {
  FakeWs ws;
  FakeRest rest;
  rest.bodies.push_back(Snapshot(1000));  // first bootstrap
  rest.bodies.push_back(Snapshot(2000));  // resync bootstrap
  ws.frames.push_back(Diff(1000, 1005));  // bracket -> live
  ws.frames.push_back(Diff(1007, 1010));  // GAP (expected 1006)
  ws.frames.push_back(Diff(2000, 2005));  // after resync: bracket for L=2000
  ws.frames.push_back({WsReadStatus::kClosed, ""});

  auto queue = std::make_unique<MarketQueue>();
  MarketDataThread md(ws, rest, Params(), *queue);
  std::atomic<bool> stop{false};
  std::thread t([&] { md.Run(stop); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  stop = true;
  t.join();

  REQUIRE(md.counters().gaps_detected >= 1);
  REQUIRE(md.counters().resyncs >= 1);
  REQUIRE(md.counters().snapshots_fetched >= 2);
}

TEST_CASE("md: reconnects with backoff after connect failures", "[md]") {
  FakeWs ws;
  FakeRest rest;
  ws.connect_ok = false;  // every connect fails
  auto queue = std::make_unique<MarketQueue>();
  MarketDataThread md(ws, rest, Params(), *queue);
  std::atomic<bool> stop{false};
  std::thread t([&] { md.Run(stop); });
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  stop = true;
  t.join();
  REQUIRE(ws.connect_calls >= 2);  // retried
}

TEST_CASE("md->engine: end-to-end book reflects emitted events", "[md][engine]") {
  FakeWs ws;
  FakeRest rest;
  rest.bodies.push_back(Snapshot(1000));
  ws.frames.push_back(Diff(1000, 1005));  // bracket: bid 100 -> qty 3.0
  ws.frames.push_back({WsReadStatus::kClosed, ""});

  auto queue = std::make_unique<MarketQueue>();
  MarketDataThread md(ws, rest, Params(), *queue);
  Engine engine(*queue, /*stale_threshold_ms=*/500);

  std::atomic<bool> stop{false};
  std::thread mt([&] { md.Run(stop); });
  std::thread et([&] { engine.Run(stop); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  stop = true;
  mt.join();
  et.join();

  REQUIRE(engine.counters().snapshots_applied >= 1);
  REQUIRE(engine.undetected_gaps() == 0);  // clean stream -> verifier never fires
  REQUIRE(engine.book().BestBid().has_value());
  REQUIRE(engine.book().BestBid()->px_ticks == 10000);   // 100.00
  REQUIRE(engine.book().BestBid()->qty_lots == 300000);  // 3.0 (bracket overwrote snapshot 1.0)
  REQUIRE(engine.book().BestAsk()->px_ticks == 10100);   // 101.00 from snapshot
}
