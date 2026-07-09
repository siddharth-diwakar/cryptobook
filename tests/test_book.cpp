#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <map>
#include <random>

#include "core/book.hpp"

using namespace asmm;

TEST_CASE("book: insert / update / delete on both sides", "[book]") {
  L2Book b;
  REQUIRE_FALSE(b.BestBid().has_value());

  b.Apply(Side::kBid, 100, 5);
  b.Apply(Side::kBid, 101, 3);
  b.Apply(Side::kBid, 99, 2);
  REQUIRE(b.BestBid()->px_ticks == 101);
  REQUIRE(b.BestBid()->qty_lots == 3);

  b.Apply(Side::kBid, 101, 7);  // update in place
  REQUIRE(b.BestBid()->qty_lots == 7);

  b.Apply(Side::kBid, 101, 0);  // delete best
  REQUIRE(b.BestBid()->px_ticks == 100);
  REQUIRE(b.NumBids() == 2);

  b.Apply(Side::kAsk, 110, 4);
  b.Apply(Side::kAsk, 109, 6);
  b.Apply(Side::kAsk, 111, 1);
  REQUIRE(b.BestAsk()->px_ticks == 109);
  REQUIRE(b.DepthAt(Side::kAsk, 1)->px_ticks == 110);
  REQUIRE(b.DepthAt(Side::kBid, 0)->px_ticks == 100);
  REQUIRE(b.DepthAt(Side::kBid, 1)->px_ticks == 99);
  REQUIRE_FALSE(b.DepthAt(Side::kBid, 2).has_value());
}

TEST_CASE("book: delete of an absent level is a no-op", "[book]") {
  L2Book b;
  REQUIRE(b.Apply(Side::kBid, 100, 0) == ApplyResult::kNoop);
  REQUIRE(b.NumBids() == 0);
}

TEST_CASE("book: mid and microprice", "[book]") {
  L2Book b;
  REQUIRE_FALSE(b.MidX2().has_value());
  b.Apply(Side::kBid, 100, 10);
  b.Apply(Side::kAsk, 102, 30);
  REQUIRE(b.MidX2().value() == 202);
  // microprice = (100*30 + 102*10) / (10+30) = 4020 / 40 = 100.5
  REQUIRE(std::abs(b.Microprice().value() - 100.5) < 1e-9);
}

// Property test: the book must always agree with a std::map reference model.
TEST_CASE("book: matches std::map reference under random ops", "[book][property]") {
  L2Book book;
  std::map<i64, i64> ref;  // px -> qty (bids)
  std::mt19937_64 rng(0xA5A5A5A5ULL);
  std::uniform_int_distribution<i64> px_dist(1, 300);
  std::uniform_int_distribution<i64> qty_dist(0, 5);  // 0 => delete

  for (int i = 0; i < 50000; ++i) {
    const i64 px = px_dist(rng);
    const i64 qty = qty_dist(rng);
    book.Apply(Side::kBid, px, qty);
    if (qty == 0) {
      ref.erase(px);
    } else {
      ref[px] = qty;
    }
  }

  REQUIRE(book.NumBids() == ref.size());
  if (!ref.empty()) {
    // ref is ascending by px; the book returns levels best (highest) first.
    auto it = ref.rbegin();
    for (std::size_t k = 0; k < book.NumBids(); ++k, ++it) {
      const auto lvl = book.DepthAt(Side::kBid, k);
      REQUIRE(lvl.has_value());
      REQUIRE(lvl->px_ticks == it->first);
      REQUIRE(lvl->qty_lots == it->second);
    }
  }
}
