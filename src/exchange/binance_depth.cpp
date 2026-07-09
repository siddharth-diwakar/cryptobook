#include "exchange/binance_depth.hpp"

#include <simdjson.h>

#include <cstdint>

namespace asmm {
namespace {

// Read a Binance level array (["px","qty"], ...) into scaled-integer levels.
bool ReadLevels(simdjson::dom::array arr, const SymbolFilters& f, std::vector<DepthLevel>& out) {
  for (simdjson::dom::element elem : arr) {
    simdjson::dom::array pair;
    if (elem.get(pair)) return false;
    std::string_view px_s;
    std::string_view qty_s;
    if (pair.at(0).get(px_s)) return false;
    if (pair.at(1).get(qty_s)) return false;
    i64 px = 0;
    i64 qty = 0;
    if (!DecimalToScaled(px_s, f.px_scale, px)) return false;
    if (!DecimalToScaled(qty_s, f.qty_scale, qty)) return false;
    out.push_back(DepthLevel{px, qty});
  }
  return true;
}

}  // namespace

bool DecimalToScaled(std::string_view s, int scale, i64& out) {
  if (s.empty()) return false;
  bool neg = false;
  std::size_t i = 0;
  if (s[0] == '-') {
    neg = true;
    i = 1;
  }
  i64 value = 0;
  int frac_digits = 0;
  bool seen_dot = false;
  bool any_digit = false;
  for (; i < s.size(); ++i) {
    const char c = s[i];
    if (c == '.') {
      if (seen_dot) return false;
      seen_dot = true;
      continue;
    }
    if (c < '0' || c > '9') return false;
    any_digit = true;
    if (seen_dot && frac_digits >= scale) {
      // Beyond requested precision: Binance pads with zeros; anything else is
      // finer than the tick and must be rejected.
      if (c != '0') return false;
      continue;
    }
    if (value > (INT64_MAX - 9) / 10) return false;  // overflow guard
    value = value * 10 + (c - '0');
    if (seen_dot) ++frac_digits;
  }
  if (!any_digit) return false;
  for (int k = frac_digits; k < scale; ++k) {
    if (value > INT64_MAX / 10) return false;
    value *= 10;
  }
  out = neg ? -value : value;
  return true;
}

bool ParseDepthDiff(std::string_view json, const SymbolFilters& filters, i64 ts_recv_ns,
                    std::vector<MarketEvent>& out) {
  simdjson::dom::parser parser;
  simdjson::dom::element doc;
  const simdjson::padded_string padded(json);
  if (parser.parse(padded).get(doc)) return false;

  std::uint64_t first_id = 0;
  std::uint64_t final_id = 0;
  if (doc["U"].get(first_id)) return false;
  if (doc["u"].get(final_id)) return false;
  std::int64_t exchange_ms = 0;
  if (doc["E"].get(exchange_ms)) exchange_ms = 0;  // optional; leave 0 if absent

  simdjson::dom::array b;
  simdjson::dom::array a;
  if (doc["b"].get(b)) return false;
  if (doc["a"].get(a)) return false;

  std::vector<DepthLevel> bids;
  std::vector<DepthLevel> asks;
  if (!ReadLevels(b, filters, bids)) return false;
  if (!ReadLevels(a, filters, asks)) return false;

  // Emit one or more fixed-size events, bids first then asks, fragmenting on the
  // per-event level cap. All fragments carry the same ids/ts.
  std::size_t bi = 0;
  std::size_t ai = 0;
  do {
    MarketEvent ev{};
    ev.ts_recv_ns = ts_recv_ns;
    ev.ts_exchange_ms = exchange_ms;
    ev.first_update_id = first_id;
    ev.final_update_id = final_id;
    ev.kind = EventKind::kDepthDiff;

    std::size_t slot = 0;
    while (bi < bids.size() && slot < kMaxLevelsPerEvent) {
      ev.levels[slot++] = bids[bi++];
      ++ev.num_bids;
    }
    while (ai < asks.size() && slot < kMaxLevelsPerEvent) {
      ev.levels[slot++] = asks[ai++];
      ++ev.num_asks;
    }
    if (bi < bids.size() || ai < asks.size()) ev.flags |= kFlagContinuation;
    out.push_back(ev);
  } while (bi < bids.size() || ai < asks.size());

  return true;
}

bool ParseDepthSnapshot(std::string_view json, const SymbolFilters& filters, DepthSnapshot& out) {
  simdjson::dom::parser parser;
  simdjson::dom::element doc;
  const simdjson::padded_string padded(json);
  if (parser.parse(padded).get(doc)) return false;

  std::uint64_t last_id = 0;
  if (doc["lastUpdateId"].get(last_id)) return false;
  out.last_update_id = last_id;

  simdjson::dom::array b;
  simdjson::dom::array a;
  if (doc["bids"].get(b)) return false;
  if (doc["asks"].get(a)) return false;

  out.bids.clear();
  out.asks.clear();
  if (!ReadLevels(b, filters, out.bids)) return false;
  if (!ReadLevels(a, filters, out.asks)) return false;
  return true;
}

}  // namespace asmm
