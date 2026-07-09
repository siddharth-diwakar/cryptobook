#pragma once

#include <chrono>

#include "core/types.hpp"

namespace asmm {

// The only clock allowed inside the engine: steady_clock nanoseconds.
inline i64 NowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

}  // namespace asmm
