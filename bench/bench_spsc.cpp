// SPSC queue throughput benchmark with the real MarketEvent payload size —
// supporting evidence for Phase 2 queue sizing, not an acceptance gate.
#include <cstdint>
#include <cstdio>
#include <thread>

#include "core/clock.hpp"
#include "core/events.hpp"
#include "core/spsc_queue.hpp"

using namespace asmm;

int main() {
  constexpr std::uint64_t kN = 50'000'000ULL;
  static SpscQueue<MarketEvent, 4096> q;  // static: ~4.4 MB, keep off the stack

  const i64 t0 = NowNs();
  std::thread consumer([] {
    MarketEvent ev{};
    std::uint64_t got = 0;
    while (got < kN) {
      if (q.try_pop(ev)) ++got;
    }
  });

  MarketEvent ev{};
  for (std::uint64_t i = 0; i < kN; ++i) {
    ev.seq = i;
    while (!q.try_push(ev)) { /* spin */
    }
  }
  consumer.join();
  const i64 t1 = NowNs();

  const double secs = static_cast<double>(t1 - t0) / 1e9;
  const double mps = static_cast<double>(kN) / secs / 1e6;
  std::printf("spsc: %llu MarketEvents (%zu B each) in %.3f s = %.1f M msg/s\n",
              static_cast<unsigned long long>(kN), sizeof(MarketEvent), secs, mps);
  return 0;
}
