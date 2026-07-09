#include "engine/market_data_thread.hpp"

#include <spdlog/spdlog.h>

#include <chrono>
#include <span>
#include <thread>

#include "core/clock.hpp"
#include "engine/backoff.hpp"

namespace asmm {

MarketDataThread::MarketDataThread(IWsTransport& ws, IRestClient& rest, MarketDataParams params,
                                   MarketQueue& out)
    : ws_(ws), rest_(rest), params_(std::move(params)), out_(out) {}

void MarketDataThread::SleepMs(i64 ms, std::atomic<bool>& stop) {
  const i64 step = 50;
  for (i64 slept = 0; slept < ms && !stop.load(std::memory_order_relaxed); slept += step) {
    std::this_thread::sleep_for(std::chrono::milliseconds(std::min(step, ms - slept)));
  }
}

bool MarketDataThread::PushGroup(const std::vector<MarketEvent>& group, std::atomic<bool>& stop) {
  for (const auto& ev : group) {
    while (!out_.try_push(ev)) {
      ++counters_.queue_full_spins;
      if (stop.load(std::memory_order_relaxed)) return false;
      std::this_thread::yield();
    }
  }
  counters_.events_emitted += group.size();
  return true;
}

MarketDataThread::BootResult MarketDataThread::Bootstrap(std::atomic<bool>& stop) {
  for (int attempt = 0; attempt < params_.max_snapshot_refetch; ++attempt) {
    if (stop.load(std::memory_order_relaxed)) return BootResult::kStopped;

    std::string err;
    const auto body = rest_.Get(params_.rest_host, params_.snapshot_target, err);
    if (!body) {
      spdlog::warn("md: snapshot fetch failed: {}", err);
      return BootResult::kError;
    }
    DepthSnapshot snap;
    if (!ParseDepthSnapshot(*body, params_.filters, snap)) {
      spdlog::warn("md: snapshot parse failed");
      return BootResult::kError;
    }
    ++counters_.snapshots_fetched;

    std::vector<MarketEvent> snap_events;
    recon_.OnSnapshot(snap, NowNs(), snap_events);
    if (!PushGroup(snap_events, stop)) return BootResult::kStopped;
    spdlog::info("md: snapshot lastUpdateId={} ({} fragments)", snap.last_update_id,
                 snap_events.size());

    // Read frames, dropping pre-snapshot events until the bracket event.
    while (!stop.load(std::memory_order_relaxed)) {
      const WsFrame frame = ws_.Read();
      if (frame.status != WsReadStatus::kOk) return BootResult::kError;

      scratch_.clear();
      if (!ParseDepthDiff(frame.payload, params_.filters, NowNs(), scratch_) || scratch_.empty()) {
        continue;  // non-depth frame (e.g. subscription ack); ignore
      }
      const ReconAction action = recon_.OnDiffGroup(std::span<const MarketEvent>(scratch_));
      if (action == ReconAction::kDrop) continue;
      if (action == ReconAction::kEmitAndGoLive) {
        if (!PushGroup(scratch_, stop)) return BootResult::kStopped;
        spdlog::info("md: live (bracketed at U={} u={})", scratch_.front().first_update_id,
                     scratch_.front().final_update_id);
        return BootResult::kLive;
      }
      if (action == ReconAction::kSnapshotTooOld) {
        ++counters_.snapshot_too_old;
        spdlog::warn("md: snapshot too old (U={} > L+1); refetching",
                     scratch_.front().first_update_id);
        break;  // refetch a newer snapshot
      }
      return BootResult::kError;  // gap/need-snapshot during bootstrap
    }
    if (stop.load(std::memory_order_relaxed)) return BootResult::kStopped;
  }
  return BootResult::kSnapshotTooOld;  // exhausted refetches
}

bool MarketDataThread::RunLive(std::atomic<bool>& stop) {
  while (!stop.load(std::memory_order_relaxed)) {
    const WsFrame frame = ws_.Read();
    if (frame.status != WsReadStatus::kOk) {
      spdlog::warn("md: live read ended (status={})", static_cast<int>(frame.status));
      return false;  // transport issue -> full reconnect
    }
    scratch_.clear();
    if (!ParseDepthDiff(frame.payload, params_.filters, NowNs(), scratch_) || scratch_.empty()) {
      continue;
    }
    const ReconAction action = recon_.OnDiffGroup(std::span<const MarketEvent>(scratch_));
    if (action == ReconAction::kEmit || action == ReconAction::kEmitAndGoLive) {
      if (!PushGroup(scratch_, stop)) return false;
    } else if (action == ReconAction::kGap) {
      ++counters_.gaps_detected;
      spdlog::warn("md: sequence gap (U={} u={}); resyncing", scratch_.front().first_update_id,
                   scratch_.front().final_update_id);
      return true;  // rebootstrap on the same connection
    }
  }
  return false;
}

void MarketDataThread::Run(std::atomic<bool>& stop) {
  Backoff backoff(params_.backoff_initial_ms, params_.backoff_max_ms, params_.backoff_multiplier,
                  params_.backoff_seed);
  bool connected = false;

  while (!stop.load(std::memory_order_relaxed)) {
    if (!connected) {
      std::string err;
      if (!ws_.Connect(params_.ws_host, params_.ws_target, err)) {
        const i64 delay = backoff.NextDelayMs();
        spdlog::warn("md: connect failed: {} (retry in {} ms)", err, delay);
        SleepMs(delay, stop);
        continue;
      }
      connected = true;
      spdlog::info("md: connected to {}{}", params_.ws_host, params_.ws_target);
    }

    const BootResult boot = Bootstrap(stop);
    if (boot == BootResult::kStopped) break;
    if (boot != BootResult::kLive) {
      ws_.Close();
      connected = false;
      ++counters_.ws_reconnects;
      SleepMs(backoff.NextDelayMs(), stop);
      continue;
    }
    backoff.Reset();  // a healthy connection reached live

    const bool gap = RunLive(stop);
    if (stop.load(std::memory_order_relaxed)) break;
    if (gap) {
      ++counters_.resyncs;
      recon_.Reset();
      // keep the connection; loop back to Bootstrap on the same WS
    } else {
      ws_.Close();
      connected = false;
      recon_.Reset();
      ++counters_.ws_reconnects;
      SleepMs(backoff.NextDelayMs(), stop);
    }
  }
  ws_.Close();
}

}  // namespace asmm
