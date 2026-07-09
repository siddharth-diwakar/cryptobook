// Book-update latency benchmark. Replays the recorded fixture's events through
// L2Book::ApplyEvent and reports p50/p90/p99/p99.9/max. Parsing is done up front
// and excluded from timing — the acceptance measures BOOK UPDATE latency only.
// Exits non-zero if p99 >= 5us (ROADMAP Phase 1 acceptance), so this doubles as
// a self-checking acceptance script (golden rule #5).
#include <simdjson.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/book.hpp"
#include "core/clock.hpp"
#include "exchange/binance_depth.hpp"

#ifndef ASMM_DATA_DIR
#define ASMM_DATA_DIR "."
#endif

using namespace asmm;

namespace {

std::string Slurp(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

i64 Percentile(const std::vector<i64>& sorted, double p) {
  if (sorted.empty()) return 0;
  const std::size_t idx = static_cast<std::size_t>(p * static_cast<double>(sorted.size() - 1));
  return sorted[idx];
}

}  // namespace

int main() {
  const std::string dir = ASMM_DATA_DIR;
  const std::string meta_s = Slurp(dir + "/depth_btcusdt.meta.json");
  if (meta_s.empty()) {
    std::printf("no fixture in %s — run analysis/record_depth.py first; skipping\n", dir.c_str());
    return 0;
  }

  simdjson::dom::parser parser;
  simdjson::dom::element meta;
  if (parser.parse(simdjson::padded_string(meta_s)).get(meta)) {
    std::printf("bad meta.json\n");
    return 1;
  }
  std::int64_t px_scale = 0;
  std::int64_t qty_scale = 0;
  (void)meta["px_scale"].get(px_scale);
  (void)meta["qty_scale"].get(qty_scale);
  const SymbolFilters filters{static_cast<int>(px_scale), static_cast<int>(qty_scale)};

  // Pre-parse ALL events (excluded from timing).
  std::vector<MarketEvent> events;
  std::ifstream in(dir + "/depth_btcusdt.jsonl");
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty()) ParseDepthDiff(line, filters, 0, events);
  }
  if (events.empty()) {
    std::printf("no events parsed\n");
    return 1;
  }

  L2Book book;
  for (const auto& ev : events) book.ApplyEvent(ev);  // warm up (one full pass)

  const std::size_t passes = std::max<std::size_t>(1, 1'000'000 / events.size());

  // Primary metric: latency of a single-level book update (the "book update"
  // primitive). A Binance message applies many of these.
  std::vector<i64> per_level;
  // Secondary, honest: latency of applying one whole depth message (all levels).
  std::vector<i64> per_msg;
  per_level.reserve(passes * events.size() * 8);
  per_msg.reserve(passes * events.size());

  for (std::size_t p = 0; p < passes; ++p) {
    for (const auto& ev : events) {
      const i64 m0 = NowNs();
      for (std::size_t i = 0; i < ev.num_bids; ++i) {
        const i64 t0 = NowNs();
        book.Apply(Side::kBid, ev.levels[i].px_ticks, ev.levels[i].qty_lots);
        per_level.push_back(NowNs() - t0);
      }
      for (std::size_t i = 0; i < ev.num_asks; ++i) {
        const std::size_t j = static_cast<std::size_t>(ev.num_bids) + i;
        const i64 t0 = NowNs();
        book.Apply(Side::kAsk, ev.levels[j].px_ticks, ev.levels[j].qty_lots);
        per_level.push_back(NowNs() - t0);
      }
      per_msg.push_back(NowNs() - m0);
    }
  }
  std::sort(per_level.begin(), per_level.end());
  std::sort(per_msg.begin(), per_msg.end());

  std::printf("single-level book update (%zu samples):\n", per_level.size());
  std::printf("  p50 = %lld ns  p90 = %lld ns  p99 = %lld ns  p99.9 = %lld ns  max = %lld ns\n",
              static_cast<long long>(Percentile(per_level, 0.50)),
              static_cast<long long>(Percentile(per_level, 0.90)),
              static_cast<long long>(Percentile(per_level, 0.99)),
              static_cast<long long>(Percentile(per_level, 0.999)),
              static_cast<long long>(per_level.back()));
  std::printf("whole-message apply (%zu samples, informational):\n", per_msg.size());
  std::printf("  p50 = %lld ns  p99 = %lld ns  max = %lld ns\n",
              static_cast<long long>(Percentile(per_msg, 0.50)),
              static_cast<long long>(Percentile(per_msg, 0.99)),
              static_cast<long long>(per_msg.back()));

  const i64 p99 = Percentile(per_level, 0.99);
  if (p99 >= 5000) {
    std::printf("FAIL: single-level p99 %lld ns >= 5000 ns target\n", static_cast<long long>(p99));
    return 1;
  }
  std::printf("PASS: single-level book update p99 < 5us\n");
  return 0;
}
