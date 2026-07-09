#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <thread>

#include "core/spsc_queue.hpp"

using asmm::SpscQueue;
using U64 = std::uint64_t;

TEST_CASE("spsc: empty pop fails; single-thread FIFO", "[spsc]") {
  SpscQueue<int, 4> q;
  int out = -1;
  REQUIRE_FALSE(q.try_pop(out));
  REQUIRE(q.try_push(10));
  REQUIRE(q.try_push(20));
  REQUIRE(q.try_pop(out));
  REQUIRE(out == 10);
  REQUIRE(q.try_pop(out));
  REQUIRE(out == 20);
  REQUIRE_FALSE(q.try_pop(out));
}

TEST_CASE("spsc: fills to full capacity then rejects", "[spsc]") {
  SpscQueue<int, 4> q;
  REQUIRE(q.try_push(1));
  REQUIRE(q.try_push(2));
  REQUIRE(q.try_push(3));
  REQUIRE(q.try_push(4));        // all Capacity slots usable
  REQUIRE_FALSE(q.try_push(5));  // full
  int out = 0;
  REQUIRE(q.try_pop(out));
  REQUIRE(out == 1);
  REQUIRE(q.try_push(5));  // space freed
}

TEST_CASE("spsc: wraparound preserves order across many cycles", "[spsc]") {
  SpscQueue<int, 4> q;
  int out = 0;
  for (int i = 0; i < 1000; ++i) {
    REQUIRE(q.try_push(i));
    REQUIRE(q.try_pop(out));
    REQUIRE(out == i);
  }
}

TEST_CASE("spsc: 2-thread stress, 1e8 messages, no loss or reorder", "[spsc][stress]") {
  constexpr U64 kN = 100'000'000ULL;
  SpscQueue<U64, 4096> q;
  bool order_ok = true;
  U64 received = 0;

  std::thread consumer([&] {
    U64 expected = 0;
    U64 v = 0;
    while (expected < kN) {
      if (q.try_pop(v)) {
        if (v != expected) order_ok = false;  // plain check; 1e8 REQUIREs is too slow
        ++expected;
      }
    }
    received = expected;
  });

  for (U64 i = 0; i < kN; ++i) {
    while (!q.try_push(i)) { /* spin until the consumer frees a slot */
    }
  }
  consumer.join();

  REQUIRE(order_ok);        // every value arrived in send order (no reorder)
  REQUIRE(received == kN);  // and all of them arrived (no loss)
}
