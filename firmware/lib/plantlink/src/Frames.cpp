#include "Frames.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "Sha256.h"

namespace plantlink {

namespace {

class Writer {
 public:
  Writer(uint8_t* buf, size_t cap) : buf_(buf), cap_(cap) {}
  void u8(uint8_t v) { put(&v, 1); }
  void u16(uint16_t v) {
    const uint8_t b[2] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8)};
    put(b, 2);
  }
  void u32(uint32_t v) {
    const uint8_t b[4] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v >> 16),
                          static_cast<uint8_t>(v >> 24)};
    put(b, 4);
  }
  void i16(int16_t v) { u16(static_cast<uint16_t>(v)); }
  void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
  void f32(float v) {
    uint32_t x;
    memcpy(&x, &v, 4);
    u32(x);
  }
  // Scaled, clamped fixed point.
  void u16x(float v, float scale) { u16(static_cast<uint16_t>(std::clamp(std::lround(v * scale), 0L, 65535L))); }
  void i16x(float v, float scale) { i16(static_cast<int16_t>(std::clamp(std::lround(v * scale), -32768L, 32767L))); }
  void str(const std::string& s, size_t maxLen) {
    const std::string c = clipUtf8(s, std::min<size_t>(maxLen, 255));
    u8(static_cast<uint8_t>(c.size()));
    put(reinterpret_cast<const uint8_t*>(c.data()), c.size());
  }
  void bytes(const uint8_t* p, size_t n) { put(p, n); }
  bool ok() const { return ok_; }
  size_t len() const { return len_; }

 private:
  void put(const uint8_t* p, size_t n) {
    if (!ok_ || len_ + n > cap_) {
      ok_ = false;
      return;
    }
    memcpy(buf_ + len_, p, n);
    len_ += n;
  }
  uint8_t* buf_;
  size_t cap_;
  size_t len_ = 0;
  bool ok_ = true;
};

class Reader {
 public:
  Reader(const uint8_t* d, size_t n) : d_(d), n_(n) {}
  uint8_t u8() {
    uint8_t v = 0;
    get(&v, 1);
    return v;
  }
  uint16_t u16() {
    uint8_t b[2] = {0, 0};
    get(b, 2);
    return static_cast<uint16_t>(b[0] | (b[1] << 8));
  }
  uint32_t u32() {
    uint8_t b[4] = {0, 0, 0, 0};
    get(b, 4);
    return uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24);
  }
  int16_t i16() { return static_cast<int16_t>(u16()); }
  int32_t i32() { return static_cast<int32_t>(u32()); }
  float f32() {
    const uint32_t x = u32();
    float v;
    memcpy(&v, &x, 4);
    return std::isfinite(v) ? v : 0;
  }
  float u16x(float scale) { return u16() / scale; }
  float i16x(float scale) { return i16() / scale; }
  std::string str() {
    const uint8_t n = u8();
    if (!ok_ || pos_ + n > n_) {
      ok_ = false;
      return {};
    }
    std::string s(reinterpret_cast<const char*>(d_ + pos_), n);
    pos_ += n;
    return s;
  }
  void bytes(uint8_t* p, size_t n) { get(p, n); }
  bool ok() const { return ok_; }
  bool done() const { return ok_ && pos_ == n_; }

 private:
  void get(uint8_t* p, size_t n) {
    if (!ok_ || pos_ + n > n_) {
      ok_ = false;
      memset(p, 0, n);
      return;
    }
    memcpy(p, d_ + pos_, n);
    pos_ += n;
  }
  const uint8_t* d_;
  size_t n_;
  size_t pos_ = 0;
  bool ok_ = true;
};

void writeHeader(Writer& w, MsgType t, uint8_t flags, uint32_t seq) {
  w.u8(kMagic);
  w.u8(kProtocol);
  w.u8(static_cast<uint8_t>(t));
  w.u8(flags);
  w.u32(seq);
}

void tag(const uint8_t* frame, size_t len, const Key& key, const Mac& node, uint8_t out[kTagLen]) {
  uint8_t mac[Sha256::kDigestLen];
  hmacSha256(key.data(), key.size(), node.data(), node.size(), frame, len, nullptr, 0, mac);
  memcpy(out, mac, kTagLen);
}

// Finishes a signed frame: appends the tag.
size_t sign(Writer& w, uint8_t* out, const Key& key, const Mac& node) {
  if (!w.ok() || w.len() + kTagLen > kMaxFrame) return 0;
  uint8_t t[kTagLen];
  tag(out, w.len(), key, node, t);
  w.bytes(t, kTagLen);
  return w.ok() ? w.len() : 0;
}

bool valid(const uint8_t* d, size_t n, MsgType expected, bool signedFrame) {
  Header h;
  if (!parseHeader(d, n, h) || h.type != expected) return false;
  if (((h.flags & kFlagSigned) != 0) != signedFrame) return false;
  return !signedFrame || n >= kHeaderLen + kTagLen;
}

void writeAck(Writer& w, const NodeAck& a) {
  w.u32(a.cfgVersion);
  w.u32(a.cfgRxVersion);
  w.u16(a.cfgRxOffset);
  w.u16(a.lastCmdId);
}

NodeAck readAck(Reader& r) {
  NodeAck a;
  a.cfgVersion = r.u32();
  a.cfgRxVersion = r.u32();
  a.cfgRxOffset = r.u16();
  a.lastCmdId = r.u16();
  return a;
}

}  // namespace

std::string macToId(const Mac& mac) {
  static const char* kHex = "0123456789abcdef";
  std::string s;
  for (uint8_t b : mac) {
    s += kHex[b >> 4];
    s += kHex[b & 15];
  }
  return s;
}

bool idToMac(const std::string& id, Mac& out) {
  if (id.size() != 12) return false;
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
  };
  for (size_t i = 0; i < 6; ++i) {
    const int hi = nib(id[2 * i]), lo = nib(id[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<uint8_t>(hi << 4 | lo);
  }
  return true;
}

const char* boardName(uint8_t board) {
  switch (static_cast<Board>(board)) {
    case Board::SuperMiniC3: return "esp32-c3-supermini";
    case Board::XiaoC3: return "xiao-esp32c3";
    case Board::XiaoS3: return "xiao-esp32s3";
    case Board::Unknown: break;
  }
  return "unknown";
}

const char* commandKindName(CommandKind k) {
  switch (k) {
    case CommandKind::Water: return "water";
    case CommandKind::TimedRun: return "timed_run";
    case CommandKind::Refilled: return "refilled";
    case CommandKind::ClearFaults: return "clear_faults";
    case CommandKind::Identify: return "identify";
    case CommandKind::Forget: return "forget";
    case CommandKind::None: break;
  }
  return "none";
}

std::string clipUtf8(const std::string& s, size_t maxLen) {
  if (s.size() <= maxLen) return s;
  size_t n = maxLen;
  // Step back over continuation bytes (10xxxxxx) to the start of a character.
  while (n > 0 && (static_cast<uint8_t>(s[n]) & 0xC0) == 0x80) --n;
  return s.substr(0, n);
}

// ------------------------------------------------------------------ encode

size_t encode(const HelloMsg& m, uint32_t seq, uint8_t* out) {
  Writer w(out, kMaxFrame);
  writeHeader(w, MsgType::Hello, 0, seq);
  w.u8(m.board);
  w.str(m.fw, 15);
  return w.ok() ? w.len() : 0;
}

size_t encode(const WelcomeMsg& m, uint32_t seq, uint8_t* out) {
  Writer w(out, kMaxFrame);
  writeHeader(w, MsgType::Welcome, 0, seq);
  w.u8(m.channel);
  w.u8(m.adopted ? 1 : 0);
  if (m.adopted) w.bytes(m.key.data(), m.key.size());
  w.u16(m.checkInS);
  return w.ok() ? w.len() : 0;
}

size_t encode(const RejectMsg& m, uint32_t seq, uint8_t* out) {
  Writer w(out, kMaxFrame);
  writeHeader(w, MsgType::Reject, 0, seq);
  w.u8(m.reason);
  return w.ok() ? w.len() : 0;
}

size_t encode(const ReportMsg& m, uint32_t seq, const Key& key, const Mac& node, uint8_t* out) {
  Writer w(out, kMaxFrame);
  writeHeader(w, MsgType::Report, kFlagSigned, seq);
  writeAck(w, m.ack);
  w.str(m.fw, 15);
  w.u32(m.uptimeS);
  w.u16(m.batteryMv);
  w.u8(m.linkPct);
  uint16_t flags = 0;
  const bool bits[] = {m.hasPlant, m.haveMoisture, m.lockedOut,  m.doseActive, m.stopped, m.pumpReady,
                       m.bottleEmpty, m.bottleLow, m.batteryLow, m.button, m.timeValid};
  for (size_t i = 0; i < sizeof bits / sizeof bits[0]; ++i) {
    if (bits[i]) flags |= static_cast<uint16_t>(1u << i);
  }
  w.u16(flags);
  w.u16x(m.moisturePct, 10);
  w.i32(m.raw);
  w.u8(m.health);
  w.u8(m.issue);
  w.u8(m.planner);
  w.u16x(m.triggerPct, 10);
  w.u16x(m.targetPct, 10);
  w.u16x(m.mlToday, 1);
  w.u32(m.lastWateredAt);
  w.u16x(m.mlPerPct, 100);
  w.u16(m.soakRemainingS);
  w.u16x(m.bottleMl, 1);
  w.u16x(m.bottleCapacityMl, 1);
  w.i16x(m.daysLeft < 0 ? -0.1f : m.daysLeft, 10);
  w.u8(m.emptyReason);
  w.u16x(m.flowFactor, 1000);
  w.u8(m.pumpVerdict);
  w.u16x(m.doseMl, 10);
  w.u16x(m.doseDispensedMl, 10);
  w.u16x(m.lastDoseMl, 10);
  w.u32(m.lastDoseOnMs);
  w.u32(m.lastDoseAt);
  w.u32(m.activeAlerts);
  w.u8(m.outbox);
  return sign(w, out, key, node);
}

size_t encode(const EventMsg& m, uint32_t seq, const Key& key, const Mac& node, uint8_t* out) {
  Writer w(out, kMaxFrame);
  writeHeader(w, MsgType::Event, kFlagSigned, seq);
  writeAck(w, m.ack);
  const plantcore::Event& e = m.event;
  w.u32(static_cast<uint32_t>(std::max<int64_t>(0, e.ts)));
  w.u8(static_cast<uint8_t>(e.type));
  w.u8(static_cast<uint8_t>(e.channel));
  w.u8(static_cast<uint8_t>(e.severity));
  w.f32(e.ml);
  w.str(e.code, 24);
  w.str(e.detail, kMaxText);
  return sign(w, out, key, node);
}

size_t encode(const PollMsg& m, uint32_t seq, const Key& key, const Mac& node, uint8_t* out) {
  Writer w(out, kMaxFrame);
  writeHeader(w, MsgType::Poll, kFlagSigned, seq);
  writeAck(w, m.ack);
  return sign(w, out, key, node);
}

size_t encode(const ReplyMsg& m, uint32_t seq, const Key& key, const Mac& node, uint8_t* out) {
  Writer w(out, kMaxFrame);
  writeHeader(w, MsgType::Reply, kFlagSigned, seq);
  w.u32(m.epoch);
  w.i32(m.utcOffsetS);
  uint8_t flags = 0;
  if (m.automation) flags |= 0x01;
  if (m.stop) flags |= 0x02;
  if (m.more) flags |= 0x04;
  if (m.env.valid) flags |= 0x08;
  if (m.env.luxValid) flags |= 0x10;
  w.u8(flags);
  w.i16x(m.env.tempC, 10);
  w.u16x(m.env.humidityPct, 10);
  w.u32(static_cast<uint32_t>(std::max(0.0f, m.env.lux)));
  w.u8(static_cast<uint8_t>(m.item));
  if (m.item == ReplyItem::Command) {
    w.u16(m.command.id);
    w.u8(static_cast<uint8_t>(m.command.kind));
    w.f32(m.command.arg);
  } else if (m.item == ReplyItem::Config) {
    if (m.chunk.data.size() > kMaxChunk) return 0;
    w.u32(m.chunk.version);
    w.u16(m.chunk.total);
    w.u16(m.chunk.offset);
    w.u8(static_cast<uint8_t>(m.chunk.data.size()));
    w.bytes(reinterpret_cast<const uint8_t*>(m.chunk.data.data()), m.chunk.data.size());
  }
  return sign(w, out, key, node);
}

// ------------------------------------------------------------------ decode

bool parseHeader(const uint8_t* d, size_t n, Header& h) {
  if (!d || n < kHeaderLen || n > kMaxFrame || d[0] != kMagic || d[1] != kProtocol) return false;
  if (d[2] < static_cast<uint8_t>(MsgType::Hello) || d[2] > static_cast<uint8_t>(MsgType::Reject)) return false;
  h.type = static_cast<MsgType>(d[2]);
  h.flags = d[3];
  h.seq = uint32_t(d[4]) | (uint32_t(d[5]) << 8) | (uint32_t(d[6]) << 16) | (uint32_t(d[7]) << 24);
  return true;
}

bool checkTag(const uint8_t* d, size_t n, const Key& key, const Mac& node) {
  Header h;
  if (!parseHeader(d, n, h) || !(h.flags & kFlagSigned) || n < kHeaderLen + kTagLen) return false;
  uint8_t t[kTagLen];
  tag(d, n - kTagLen, key, node, t);
  return equalBytes(t, d + n - kTagLen, kTagLen);
}

bool decode(const uint8_t* d, size_t n, HelloMsg& m) {
  if (!valid(d, n, MsgType::Hello, false)) return false;
  Reader r(d + kHeaderLen, n - kHeaderLen);
  m.board = r.u8();
  m.fw = r.str();
  return r.done();
}

bool decode(const uint8_t* d, size_t n, WelcomeMsg& m) {
  if (!valid(d, n, MsgType::Welcome, false)) return false;
  Reader r(d + kHeaderLen, n - kHeaderLen);
  m.channel = r.u8();
  m.adopted = r.u8() != 0;
  if (m.adopted) r.bytes(m.key.data(), m.key.size());
  m.checkInS = r.u16();
  return r.done();
}

bool decode(const uint8_t* d, size_t n, RejectMsg& m) {
  if (!valid(d, n, MsgType::Reject, false)) return false;
  Reader r(d + kHeaderLen, n - kHeaderLen);
  m.reason = r.u8();
  return r.done();
}

bool decode(const uint8_t* d, size_t n, ReportMsg& m) {
  if (!valid(d, n, MsgType::Report, true)) return false;
  Reader r(d + kHeaderLen, n - kHeaderLen - kTagLen);
  m.ack = readAck(r);
  m.fw = r.str();
  m.uptimeS = r.u32();
  m.batteryMv = r.u16();
  m.linkPct = r.u8();
  const uint16_t f = r.u16();
  bool* bits[] = {&m.hasPlant, &m.haveMoisture, &m.lockedOut,  &m.doseActive, &m.stopped, &m.pumpReady,
                  &m.bottleEmpty, &m.bottleLow, &m.batteryLow, &m.button, &m.timeValid};
  for (size_t i = 0; i < sizeof bits / sizeof bits[0]; ++i) *bits[i] = (f >> i) & 1u;
  m.moisturePct = r.u16x(10);
  m.raw = r.i32();
  m.health = r.u8();
  m.issue = r.u8();
  m.planner = r.u8();
  m.triggerPct = r.u16x(10);
  m.targetPct = r.u16x(10);
  m.mlToday = r.u16x(1);
  m.lastWateredAt = r.u32();
  m.mlPerPct = r.u16x(100);
  m.soakRemainingS = r.u16();
  m.bottleMl = r.u16x(1);
  m.bottleCapacityMl = r.u16x(1);
  const float days = r.i16x(10);
  m.daysLeft = days < 0 ? -1 : days;
  m.emptyReason = r.u8();
  m.flowFactor = r.u16x(1000);
  m.pumpVerdict = r.u8();
  m.doseMl = r.u16x(10);
  m.doseDispensedMl = r.u16x(10);
  m.lastDoseMl = r.u16x(10);
  m.lastDoseOnMs = r.u32();
  m.lastDoseAt = r.u32();
  m.activeAlerts = r.u32();
  m.outbox = r.u8();
  return r.done();
}

bool decode(const uint8_t* d, size_t n, EventMsg& m) {
  if (!valid(d, n, MsgType::Event, true)) return false;
  Reader r(d + kHeaderLen, n - kHeaderLen - kTagLen);
  m.ack = readAck(r);
  plantcore::Event& e = m.event;
  e.ts = r.u32();
  const uint8_t type = r.u8();
  if (type > static_cast<uint8_t>(plantcore::EventType::System)) return false;
  e.type = static_cast<plantcore::EventType>(type);
  e.channel = static_cast<int8_t>(r.u8());
  const uint8_t sev = r.u8();
  e.severity = static_cast<plantcore::Severity>(std::min<uint8_t>(sev, 2));
  e.ml = r.f32();
  e.code = r.str();
  e.detail = r.str();
  return r.done();
}

bool decode(const uint8_t* d, size_t n, PollMsg& m) {
  if (!valid(d, n, MsgType::Poll, true)) return false;
  Reader r(d + kHeaderLen, n - kHeaderLen - kTagLen);
  m.ack = readAck(r);
  return r.done();
}

bool decode(const uint8_t* d, size_t n, ReplyMsg& m) {
  if (!valid(d, n, MsgType::Reply, true)) return false;
  Reader r(d + kHeaderLen, n - kHeaderLen - kTagLen);
  m.epoch = r.u32();
  m.utcOffsetS = r.i32();
  const uint8_t f = r.u8();
  m.automation = f & 0x01;
  m.stop = f & 0x02;
  m.more = f & 0x04;
  m.env.valid = f & 0x08;
  m.env.luxValid = f & 0x10;
  m.env.tempC = r.i16x(10);
  m.env.humidityPct = r.u16x(10);
  m.env.lux = static_cast<float>(r.u32());
  const uint8_t item = r.u8();
  if (item > static_cast<uint8_t>(ReplyItem::Config)) return false;
  m.item = static_cast<ReplyItem>(item);
  if (m.item == ReplyItem::Command) {
    m.command.id = r.u16();
    const uint8_t kind = r.u8();
    if (kind > static_cast<uint8_t>(CommandKind::Forget)) return false;
    m.command.kind = static_cast<CommandKind>(kind);
    m.command.arg = r.f32();
  } else if (m.item == ReplyItem::Config) {
    m.chunk.version = r.u32();
    m.chunk.total = r.u16();
    m.chunk.offset = r.u16();
    const uint8_t len = r.u8();
    if (len > kMaxChunk) return false;
    m.chunk.data.assign(len, '\0');
    r.bytes(reinterpret_cast<uint8_t*>(&m.chunk.data[0]), len);
  }
  return r.done();
}

}  // namespace plantlink
