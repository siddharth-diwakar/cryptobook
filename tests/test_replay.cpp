#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

#include "engine/replay.hpp"
#include "exchange/binance_depth.hpp"

#ifndef ASMM_TMP_DIR
#define ASMM_TMP_DIR "."
#endif
#ifndef ASMM_SOURCE_DIR
#define ASMM_SOURCE_DIR "."
#endif
#ifndef ASMM_DATA_DIR
#define ASMM_DATA_DIR "."
#endif

using namespace asmm;

namespace {

// A fixed, deterministic input: snapshot-start then bracket + live diffs.
std::vector<MarketEvent> Synthetic() {
  std::vector<MarketEvent> v;
  {
    MarketEvent e{};
    e.kind = EventKind::kSnapshot;
    e.flags = kFlagSnapshotStart;
    e.first_update_id = 1000;
    e.final_update_id = 1000;
    e.num_bids = 2;
    e.num_asks = 2;
    e.levels[0] = {100, 5};
    e.levels[1] = {99, 4};
    e.levels[2] = {101, 6};
    e.levels[3] = {102, 7};
    v.push_back(e);
  }
  auto diff = [](u64 U, u64 u, int nb, int na, DepthLevel a, DepthLevel b) {
    MarketEvent e{};
    e.kind = EventKind::kDepthDiff;
    e.first_update_id = U;
    e.final_update_id = u;
    e.num_bids = static_cast<u8>(nb);
    e.num_asks = static_cast<u8>(na);
    e.levels[0] = a;
    if (nb + na > 1) e.levels[1] = b;
    return e;
  };
  v.push_back(diff(1001, 1005, 1, 0, {100, 8}, {}));  // bracket, update best bid
  v.push_back(diff(1006, 1010, 0, 1, {101, 9}, {}));  // update best ask
  v.push_back(diff(1011, 1011, 1, 0, {103, 1}, {}));  // new best bid
  return v;
}

std::string Bytes(const std::vector<DecisionRecord>& d) {
  return std::string(reinterpret_cast<const char*>(d.data()), d.size() * sizeof(DecisionRecord));
}

}  // namespace

TEST_CASE("replay: identical decision stream across two runs", "[replay]") {
  const auto ev = Synthetic();
  REQUIRE(Bytes(ReplayEvents(ev)) == Bytes(ReplayEvents(ev)));
}

TEST_CASE("replay: log file replay equals direct replay, deterministically", "[replay]") {
  const std::string path = std::string(ASMM_TMP_DIR) + "/replay_in.bin";
  const auto ev = Synthetic();
  WriteEventsToLog(path, std::span<const MarketEvent>(ev));
  const auto from_log = ReplayLogFile(path);
  REQUIRE(Bytes(from_log) == Bytes(ReplayEvents(ev)));
  REQUIRE(Bytes(from_log) == Bytes(ReplayLogFile(path)));
}

// The cross-machine acceptance: a golden decision stream generated on one arch
// (arm64 dev) must byte-match replay on another (x86_64 CI).
TEST_CASE("replay: matches committed golden decision stream", "[replay][golden]") {
  const std::string bytes = Bytes(ReplayEvents(Synthetic()));
  const std::string golden_path = std::string(ASMM_SOURCE_DIR) + "/tests/data/replay_golden.dat";

  if (std::getenv("ASMM_WRITE_GOLDEN")) {
    std::ofstream out(golden_path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    SUCCEED("wrote golden to " + golden_path);
    return;
  }
  std::ifstream in(golden_path, std::ios::binary);
  REQUIRE(in.good());  // the golden must be committed
  const std::string golden((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  REQUIRE(bytes == golden);
}

TEST_CASE("replay: recorded fixture replays deterministically", "[replay]") {
  const std::string jsonl = std::string(ASMM_DATA_DIR) + "/depth_btcusdt.jsonl";
  {
    std::ifstream probe(jsonl);
    if (!probe) SKIP("no recorded fixture");
  }
  const SymbolFilters f{2, 5};
  std::vector<MarketEvent> events;
  std::ifstream in(jsonl);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty()) ParseDepthDiff(line, f, 0, events);
  }
  REQUIRE(events.size() > 1000);
  REQUIRE(Bytes(ReplayEvents(events)) == Bytes(ReplayEvents(events)));
}
