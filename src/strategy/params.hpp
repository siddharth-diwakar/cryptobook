#pragma once

#include "core/types.hpp"

namespace asmm {

// Avellaneda-Stoikov strategy parameters (docs/MODEL.md). Plain POD; toml parsing
// lives in engine/config, not here.
struct StrategyParams {
  double gamma = 0.1;                   // risk aversion (swept; NOT estimated)
  double kappa = 1.5;                   // order-book liquidity/decay (placeholder until fit)
  double tau_days = 1.0;                // rolling horizon in day units
  double sigma_halflife_s = 60.0;       // EWMA halflife of 1s log-return variance
  int sigma_min_samples = 30;           // warmup before quoting
  double sigma_spike_threshold = 5e-4;  // per-sqrt-second relative vol -> pull quotes
  i64 hysteresis_ticks = 2;             // re-quote only if price moves >= H ticks
  i64 q_max_lots = 100;                 // soft inventory limit -> quote one side only
  i64 quote_size_lots = 10;             // size per side
  double maker_fee_bps = 10.0;          // Binance spot standard 0.1%
  double min_notional_usdt = 5.0;       // exchange minNotional floor
  int px_scale = 2;                     // BTCUSDT tickSize 0.01
  int qty_scale = 5;                    // stepSize 0.00001
};

}  // namespace asmm
