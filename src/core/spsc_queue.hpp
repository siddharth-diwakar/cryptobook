#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <type_traits>

#include "core/types.hpp"

namespace asmm {

// Single-producer single-consumer lock-free ring buffer. Fixed power-of-two
// capacity, storage inline in a std::array — never allocates after construction.
// Exactly one thread calls try_push; exactly one (other) thread calls try_pop.
// Both are non-blocking and exception-free.
//
// Correctness rests on monotonically increasing 64-bit head/tail counters (never
// wrapped), so `tail - head == Capacity` means full with no wasted slot and no
// ABA. Each side caches the other's counter and only re-reads it (an acquire
// load, the one cross-core sync point) when the queue looks full/empty.
template <typename T, std::size_t Capacity>
class SpscQueue {
  static_assert(std::has_single_bit(Capacity), "Capacity must be a power of two");
  static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");

 public:
  bool try_push(const T& value) {
    const u64 tail = tail_.load(std::memory_order_relaxed);
    if (tail - cached_head_ >= Capacity) {
      cached_head_ = head_.load(std::memory_order_acquire);
      if (tail - cached_head_ >= Capacity) return false;  // full
    }
    buf_[tail & kMask] = value;
    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }

  bool try_pop(T& out) {
    const u64 head = head_.load(std::memory_order_relaxed);
    if (head >= cached_tail_) {
      cached_tail_ = tail_.load(std::memory_order_acquire);
      if (head >= cached_tail_) return false;  // empty
    }
    out = buf_[head & kMask];
    head_.store(head + 1, std::memory_order_release);
    return true;
  }

  // Approximate size; safe to call from either thread, exact only when quiescent.
  std::size_t size_approx() const {
    const u64 t = tail_.load(std::memory_order_acquire);
    const u64 h = head_.load(std::memory_order_acquire);
    return static_cast<std::size_t>(t - h);
  }

  static constexpr std::size_t capacity() { return Capacity; }

 private:
  static constexpr u64 kMask = Capacity - 1;

  // Consumer-owned cache line.
  alignas(kCacheLine) std::atomic<u64> head_{0};
  u64 cached_tail_{0};

  // Producer-owned cache line.
  alignas(kCacheLine) std::atomic<u64> tail_{0};
  u64 cached_head_{0};

  // Storage on its own line(s).
  alignas(kCacheLine) std::array<T, Capacity> buf_{};
};

}  // namespace asmm
