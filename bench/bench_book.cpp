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

  // Time repeated passes until we have >= 1e6 samples.
  const std::size_t passes = std::max<std::size_t>(1, 1'000'000 / events.size());
  std::vector<i64> samples;
  samples.reserve(passes * events.size());
  for (std::size_t p = 0; p < passes; ++p) {
    for (const auto& ev : events) {
      const i64 t0 = NowNs();
      book.ApplyEvent(ev);
      const i64 t1 = NowNs();
      samples.push_back(t1 - t0);
    }
  }
  std::sort(samples.begin(), samples.end());

  const i64 p50 = Percentile(samples, 0.50);
  const i64 p90 = Percentile(samples, 0.90);
  const i64 p99 = Percentile(samples, 0.99);
  const i64 p999 = Percentile(samples, 0.999);
  const i64 pmax = samples.back();

  std::printf("book update latency over %zu samples (%zu events x %zu passes):\n", samples.size(),
              events.size(), passes);
  std::printf("  p50   = %5lld ns\n", static_cast<long long>(p50));
  std::printf("  p90   = %5lld ns\n", static_cast<long long>(p90));
  std::printf("  p99   = %5lld ns\n", static_cast<long long>(p99));
  std::printf("  p99.9 = %5lld ns\n", static_cast<long long>(p999));
  std::printf("  max   = %5lld ns\n", static_cast<long long>(pmax));

  if (p99 >= 5000) {
    std::printf("FAIL: p99 %lld ns >= 5000 ns target\n", static_cast<long long>(p99));
    return 1;
  }
  std::printf("PASS: p99 < 5us\n");
  return 0;
}
