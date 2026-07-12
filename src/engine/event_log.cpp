#include "engine/event_log.hpp"

#include <cstring>
#include <stdexcept>

namespace asmm {

namespace {
constexpr std::size_t kFlushThreshold = 1 << 20;  // 1 MiB
}

EventLogWriter::EventLogWriter(const std::string& path) {
  f_ = std::fopen(path.c_str(), "wb");
  if (!f_) throw std::runtime_error("cannot open event log for write: " + path);
  buf_.reserve(kFlushThreshold + 4096);
  LogHeader h{{'A', 'S', 'M', 'L'},
              kLogVersion,
              static_cast<u32>(sizeof(MarketEvent)),
              static_cast<u32>(sizeof(DecisionRecord)),
              static_cast<u32>(sizeof(QuoteRecord)),
              static_cast<u32>(sizeof(FillRecord))};
  Append(&h, sizeof(h));
}

EventLogWriter::~EventLogWriter() {
  Close();
}

void EventLogWriter::Append(const void* p, std::size_t n) {
  const char* c = static_cast<const char*>(p);
  buf_.insert(buf_.end(), c, c + n);
  if (buf_.size() >= kFlushThreshold) Flush();
}

void EventLogWriter::WriteRecord(u32 type, const void* payload, u32 len) {
  RecordHeader hdr{type, len};
  Append(&hdr, sizeof(hdr));
  Append(payload, len);
}

void EventLogWriter::WriteMarketEvent(const MarketEvent& ev) {
  WriteRecord(kRecMarketEvent, &ev, sizeof(ev));
}

void EventLogWriter::WriteDecision(const DecisionRecord& d) {
  WriteRecord(kRecDecision, &d, sizeof(d));
}

void EventLogWriter::WriteQuote(const QuoteRecord& q) {
  WriteRecord(kRecQuote, &q, sizeof(q));
}

void EventLogWriter::WriteFill(const FillRecord& f) {
  WriteRecord(kRecFill, &f, sizeof(f));
}

void EventLogWriter::WriteOrderCommand(const OrderCommand& c) {
  WriteRecord(kRecOrderCmd, &c, sizeof(c));
}

void EventLogWriter::WriteExecEvent(const ExecEvent& e) {
  WriteRecord(kRecExecEvent, &e, sizeof(e));
}

void EventLogWriter::WriteReconcile(const ReconcileReport& r) {
  WriteRecord(kRecReconcile, &r, sizeof(r));
}

void EventLogWriter::Flush() {
  if (f_ && !buf_.empty()) {
    std::fwrite(buf_.data(), 1, buf_.size(), f_);
    buf_.clear();
    std::fflush(f_);
  }
}

void EventLogWriter::Close() {
  if (!f_) return;
  Flush();
  std::fclose(f_);
  f_ = nullptr;
}

EventLogReader::EventLogReader(const std::string& path) {
  f_ = std::fopen(path.c_str(), "rb");
  if (!f_) throw std::runtime_error("cannot open event log for read: " + path);
  LogHeader h{};
  if (std::fread(&h, 1, sizeof(h), f_) != sizeof(h)) {
    throw std::runtime_error("event log truncated header: " + path);
  }
  if (std::memcmp(h.magic, "ASML", 4) != 0) {
    throw std::runtime_error("event log bad magic: " + path);
  }
  if (h.version != kLogVersion) {
    throw std::runtime_error("event log version mismatch: " + path);
  }
  // Cross-machine / build compatibility: struct sizes must match this binary.
  if (h.market_event_size != sizeof(MarketEvent) || h.decision_size != sizeof(DecisionRecord) ||
      h.quote_size != sizeof(QuoteRecord) || h.fill_size != sizeof(FillRecord)) {
    throw std::runtime_error("event log struct-size mismatch (incompatible build): " + path);
  }
}

EventLogReader::~EventLogReader() {
  if (f_) std::fclose(f_);
}

bool EventLogReader::Next(u32& out_type, MarketEvent& ev, DecisionRecord& d) {
  RecordHeader hdr{};
  const std::size_t got = std::fread(&hdr, 1, sizeof(hdr), f_);
  if (got == 0) return false;  // clean EOF
  if (got != sizeof(hdr)) return false;

  out_type = hdr.type;
  if (hdr.type == kRecMarketEvent) {
    return std::fread(&ev, 1, sizeof(ev), f_) == sizeof(ev);
  }
  if (hdr.type == kRecDecision) {
    return std::fread(&d, 1, sizeof(d), f_) == sizeof(d);
  }
  // Unknown record type: skip its payload and read the next one.
  if (std::fseek(f_, static_cast<long>(hdr.len), SEEK_CUR) != 0) return false;
  return Next(out_type, ev, d);
}

bool EventLogReader::NextRaw(u32& out_type, std::vector<char>& buf) {
  RecordHeader hdr{};
  const std::size_t got = std::fread(&hdr, 1, sizeof(hdr), f_);
  if (got == 0) return false;  // clean EOF
  if (got != sizeof(hdr)) return false;
  out_type = hdr.type;
  buf.resize(hdr.len);
  if (hdr.len == 0) return true;
  return std::fread(buf.data(), 1, hdr.len, f_) == hdr.len;
}

}  // namespace asmm
