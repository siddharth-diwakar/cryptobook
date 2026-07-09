#pragma once

#include <cstddef>
#include <cstdint>

namespace asmm {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

enum class Side : u8 { kBid, kAsk };

// Outcome of applying a single book mutation. No exceptions on the hot path.
enum class ApplyResult : u8 {
  kOk,       // level inserted, updated, or deleted
  kNoop,     // delete of a level not present (normal per Binance)
  kDropped,  // book full and the new level is worse than the worst kept level
};

// Target is Linux x86_64 (docs/DECISIONS.md); 64-byte lines. We avoid
// std::hardware_destructive_interference_size because GCC's -Winterference-size
// warns under -Werror.
inline constexpr std::size_t kCacheLine = 64;

}  // namespace asmm
