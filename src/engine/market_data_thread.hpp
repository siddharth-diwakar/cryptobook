#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "core/events.hpp"
#include "core/spsc_queue.hpp"
#include "core/types.hpp"
#include "exchange/binance_depth.hpp"
#include "exchange/net_transport.hpp"
#include "exchange/reconstructor.hpp"

namespace asmm {

// Queue A: market-data thread -> engine thread. ~4.4 MB; heap-allocate once.
using MarketQueue = SpscQueue<MarketEvent, 4096>;

struct MdCounters {
  u64 events_emitted = 0;
  u64 snapshots_fetched = 0;
  u64 gaps_detected = 0;
  u64 resyncs = 0;
  u64 ws_reconnects = 0;
  u64 snapshot_too_old = 0;
  u64 queue_full_spins = 0;
};

struct MarketDataParams {
  std::string ws_host;          // e.g. data-stream.binance.vision
  std::string ws_target;        // e.g. /ws/btcusdt@depth@100ms
  std::string rest_host;        // e.g. data-api.binance.vision
  std::string snapshot_target;  // e.g. /api/v3/depth?symbol=BTCUSDT&limit=5000
  SymbolFilters filters;
  i64 backoff_initial_ms = 500;
  i64 backoff_max_ms = 30000;
  double backoff_multiplier = 2.0;
  u32 backoff_seed = 1;
  int max_snapshot_refetch = 5;
};

// Drives Binance L2 reconstruction over injected transports (real Beast clients
// in production, fakes in tests) and emits a normalized, gap-free MarketEvent
// stream into queue A. Owns the Reconstructor; the engine thread owns the book.
class MarketDataThread {
 public:
  MarketDataThread(IWsTransport& ws, IRestClient& rest, MarketDataParams params, MarketQueue& out);

  // Runs until *stop becomes true.
  void Run(std::atomic<bool>& stop);

  const MdCounters& counters() const { return counters_; }

 private:
  enum class BootResult { kLive, kSnapshotTooOld, kError, kStopped };

  BootResult Bootstrap(std::atomic<bool>& stop);
  // Live read loop; returns true on a detected gap (rebootstrap on same conn),
  // false on transport error/close (full reconnect).
  bool RunLive(std::atomic<bool>& stop);
  // Push every event of a group, spin-waiting on a full queue. Returns false if
  // stop was requested mid-push.
  bool PushGroup(const std::vector<MarketEvent>& group, std::atomic<bool>& stop);
  void SleepMs(i64 ms, std::atomic<bool>& stop);

  IWsTransport& ws_;
  IRestClient& rest_;
  MarketDataParams params_;
  MarketQueue& out_;
  Reconstructor recon_;
  std::vector<MarketEvent> scratch_;  // parse target, reused per frame
  MdCounters counters_;
};

}  // namespace asmm
