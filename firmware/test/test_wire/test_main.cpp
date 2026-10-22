// The radio frame codec: SHA-256/HMAC vectors, round trips, tampering, size limits.
#include <unity.h>

#include <cstring>
#include <string>

#include "Sha256.h"
#include "Wire.h"

using namespace plantlink;

void setUp() {}
void tearDown() {}

static std::string hex(const uint8_t* p, size_t n) {
  static const char* k = "0123456789abcdef";
  std::string s;
  for (size_t i = 0; i < n; ++i) {
    s += k[p[i] >> 4];
    s += k[p[i] & 15];
  }
  return s;
}

static const Mac kNode{0xa0, 0xb1, 0xc2, 0xd3, 0xe4, 0xf5};
static const Key kKey{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

void test_sha256_vectors() {
  uint8_t out[32];
  Sha256 sha;
  sha.update(reinterpret_cast<const uint8_t*>("abc"), 3);
  sha.finish(out);
  TEST_ASSERT_EQUAL_STRING("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", hex(out, 32).c_str());
  const std::string two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  sha.update(reinterpret_cast<const uint8_t*>(two.data()), two.size());
  sha.finish(out);
  TEST_ASSERT_EQUAL_STRING("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", hex(out, 32).c_str());
}

void test_hmac_rfc4231() {
  uint8_t out[32];
  uint8_t key1[20];
  memset(key1, 0x0b, sizeof key1);
  hmacSha256(key1, sizeof key1, reinterpret_cast<const uint8_t*>("Hi There"), 8, nullptr, 0, nullptr, 0, out);
  TEST_ASSERT_EQUAL_STRING("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7", hex(out, 32).c_str());
  // The same message split over parts gives the same tag.
  hmacSha256(key1, sizeof key1, reinterpret_cast<const uint8_t*>("Hi "), 3, reinterpret_cast<const uint8_t*>("The"), 3,
             reinterpret_cast<const uint8_t*>("re"), 2, out);
  TEST_ASSERT_EQUAL_STRING("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7", hex(out, 32).c_str());
  const char* jefe = "Jefe";
  const char* msg = "what do ya want for nothing?";
  hmacSha256(reinterpret_cast<const uint8_t*>(jefe), 4, reinterpret_cast<const uint8_t*>(msg), strlen(msg), nullptr, 0,
             nullptr, 0, out);
  TEST_ASSERT_EQUAL_STRING("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", hex(out, 32).c_str());
}

void test_mac_ids() {
  TEST_ASSERT_EQUAL_STRING("a0b1c2d3e4f5", macToId(kNode).c_str());
  Mac m;
  TEST_ASSERT_TRUE(idToMac("a0b1c2d3e4f5", m));
  TEST_ASSERT_TRUE(m == kNode);
  TEST_ASSERT_FALSE(idToMac("A0B1C2D3E4F5", m));
  TEST_ASSERT_FALSE(idToMac("a0b1c2", m));
}

void test_hello_and_welcome_round_trip() {
  uint8_t buf[kMaxFrame];
  HelloMsg h;
  h.board = static_cast<uint8_t>(Board::XiaoC3);
  h.fw = "0.2.0";
  size_t n = encode(h, 7, buf);
  TEST_ASSERT_TRUE(n > 0);
  Header hd;
  TEST_ASSERT_TRUE(parseHeader(buf, n, hd));
  TEST_ASSERT_EQUAL(MsgType::Hello, hd.type);
  TEST_ASSERT_EQUAL_UINT32(7, hd.seq);
  HelloMsg hb;
  TEST_ASSERT_TRUE(decode(buf, n, hb));
  TEST_ASSERT_EQUAL(h.board, hb.board);
  TEST_ASSERT_EQUAL_STRING("0.2.0", hb.fw.c_str());

  WelcomeMsg w;
  w.channel = 6;
  w.adopted = true;
  w.key = kKey;
  w.checkInS = 600;
  n = encode(w, 7, buf);
  WelcomeMsg wb;
  TEST_ASSERT_TRUE(decode(buf, n, wb));
  TEST_ASSERT_EQUAL(6, wb.channel);
  TEST_ASSERT_TRUE(wb.adopted);
  TEST_ASSERT_TRUE(wb.key == kKey);
  TEST_ASSERT_EQUAL(600, wb.checkInS);
  WelcomeMsg waiting;
  waiting.channel = 11;
  n = encode(waiting, 8, buf);
  TEST_ASSERT_TRUE(decode(buf, n, wb));
  TEST_ASSERT_FALSE(wb.adopted);
  TEST_ASSERT_FALSE(decode(buf, n, hb));  // wrong type
}

static ReportMsg sampleReport() {
  ReportMsg r;
  r.ack = NodeAck{0x11223344, 0x55667788, 360, 41};
  r.fw = "0.2.0";
  r.uptimeS = 86400 * 40;
  r.batteryMv = 4210;
  r.linkPct = 96;
  r.hasPlant = r.haveMoisture = r.pumpReady = r.bottleLow = r.button = r.timeValid = true;
  r.moisturePct = 41.3f;
  r.raw = 15210;
  r.health = 1;
  r.issue = 6;
  r.planner = 3;
  r.triggerPct = 36.5f;
  r.targetPct = 55;
  r.mlToday = 240;
  r.lastWateredAt = 1767258000;
  r.mlPerPct = 11.25f;
  r.soakRemainingS = 420;
  r.bottleMl = 312;
  r.bottleCapacityMl = 1500;
  r.daysLeft = 1.4f;
  r.emptyReason = 0;
  r.flowFactor = 1.125f;
  r.pumpVerdict = 0;
  r.lastDoseMl = 98.2f;
  r.lastDoseOnMs = 54500;
  r.lastDoseAt = 1767258000;
  r.activeAlerts = (1u << 0) | (1u << 20);
  r.outbox = 2;
  return r;
}

void test_report_round_trip_and_size() {
  uint8_t buf[kMaxFrame];
  const ReportMsg r = sampleReport();
  const size_t n = encode(r, 1000, kKey, kNode, buf);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_TRUE_MESSAGE(n <= 140, std::to_string(n).c_str());  // plenty of room in 250 bytes
  TEST_ASSERT_TRUE(checkTag(buf, n, kKey, kNode));
  ReportMsg b;
  TEST_ASSERT_TRUE(decode(buf, n, b));
  TEST_ASSERT_EQUAL_UINT32(0x11223344, b.ack.cfgVersion);
  TEST_ASSERT_EQUAL_UINT32(0x55667788, b.ack.cfgRxVersion);
  TEST_ASSERT_EQUAL(360, b.ack.cfgRxOffset);
  TEST_ASSERT_EQUAL(41, b.ack.lastCmdId);
  TEST_ASSERT_EQUAL_STRING("0.2.0", b.fw.c_str());
  TEST_ASSERT_EQUAL_UINT32(r.uptimeS, b.uptimeS);
  TEST_ASSERT_EQUAL(4210, b.batteryMv);
  TEST_ASSERT_EQUAL(96, b.linkPct);
  TEST_ASSERT_TRUE(b.hasPlant && b.haveMoisture && b.pumpReady && b.bottleLow && b.button && b.timeValid);
  TEST_ASSERT_FALSE(b.lockedOut || b.doseActive || b.stopped || b.bottleEmpty || b.batteryLow);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 41.3f, b.moisturePct);
  TEST_ASSERT_EQUAL_INT32(15210, b.raw);
  TEST_ASSERT_EQUAL(3, b.planner);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 36.5f, b.triggerPct);
  TEST_ASSERT_EQUAL_FLOAT(240, b.mlToday);
  TEST_ASSERT_EQUAL_UINT32(1767258000, b.lastWateredAt);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 11.25f, b.mlPerPct);
  TEST_ASSERT_EQUAL(420, b.soakRemainingS);
  TEST_ASSERT_EQUAL_FLOAT(312, b.bottleMl);
  TEST_ASSERT_EQUAL_FLOAT(1500, b.bottleCapacityMl);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 1.4f, b.daysLeft);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.125f, b.flowFactor);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 98.2f, b.lastDoseMl);
  TEST_ASSERT_EQUAL_UINT32(54500, b.lastDoseOnMs);
  TEST_ASSERT_EQUAL_HEX32(r.activeAlerts, b.activeAlerts);
  TEST_ASSERT_EQUAL(2, b.outbox);

  ReportMsg unknownDays = r;
  unknownDays.daysLeft = -1;
  const size_t n2 = encode(unknownDays, 1001, kKey, kNode, buf);
  TEST_ASSERT_TRUE(decode(buf, n2, b));
  TEST_ASSERT_EQUAL_FLOAT(-1, b.daysLeft);
}

void test_tampering_and_wrong_key_are_detected() {
  uint8_t buf[kMaxFrame];
  const size_t n = encode(sampleReport(), 5, kKey, kNode, buf);
  Key other = kKey;
  other[0] ^= 1;
  TEST_ASSERT_FALSE(checkTag(buf, n, other, kNode));
  Mac otherNode = kNode;
  otherNode[5] ^= 1;
  TEST_ASSERT_FALSE(checkTag(buf, n, kKey, otherNode));  // a frame cannot be replayed as another node
  ReportMsg m;
  for (size_t i = 0; i < n; ++i) {
    uint8_t copy[kMaxFrame];
    memcpy(copy, buf, n);
    copy[i] ^= 0x40;
    TEST_ASSERT_FALSE(checkTag(copy, n, kKey, kNode) && decode(copy, n, m));
  }
  TEST_ASSERT_FALSE(decode(buf, n - 1, m));  // truncated
}

void test_event_round_trip_clips_long_text() {
  uint8_t buf[kMaxFrame];
  EventMsg m;
  m.ack.lastCmdId = 9;
  m.event.ts = 1767254400;
  m.event.type = plantcore::EventType::AlertRaised;
  m.event.channel = -1;
  m.event.severity = plantcore::Severity::Critical;
  m.event.ml = 0;
  m.event.code = "battery_low";
  m.event.detail = std::string(150, 'x') + "é°C" + std::string(100, 'y');
  const size_t n = encode(m, 77, kKey, kNode, buf);
  TEST_ASSERT_TRUE(n > 0 && n <= kMaxFrame);
  EventMsg b;
  TEST_ASSERT_TRUE(decode(buf, n, b));
  TEST_ASSERT_EQUAL_INT64(1767254400, b.event.ts);
  TEST_ASSERT_EQUAL(plantcore::EventType::AlertRaised, b.event.type);
  TEST_ASSERT_EQUAL(-1, b.event.channel);
  TEST_ASSERT_EQUAL(plantcore::Severity::Critical, b.event.severity);
  TEST_ASSERT_EQUAL_STRING("battery_low", b.event.code.c_str());
  TEST_ASSERT_TRUE(b.event.detail.size() <= kMaxText);
  TEST_ASSERT_EQUAL_STRING(clipUtf8(m.event.detail, kMaxText).c_str(), b.event.detail.c_str());
}

void test_utf8_clipping_never_splits_a_character() {
  const std::string s = "ab°C";  // ° is two bytes
  TEST_ASSERT_EQUAL_STRING("ab", clipUtf8(s, 3).c_str());
  TEST_ASSERT_EQUAL_STRING("ab°", clipUtf8(s, 4).c_str());
  TEST_ASSERT_EQUAL_STRING(s.c_str(), clipUtf8(s, 10).c_str());
}

void test_reply_with_command_and_with_config() {
  uint8_t buf[kMaxFrame];
  ReplyMsg r;
  r.epoch = 1767254400;
  r.utcOffsetS = 7200;
  r.automation = false;
  r.stop = true;
  r.more = true;
  r.env = plantcore::EnvSnapshot{true, 23.4f, 48.5f, true, 5400};
  r.item = ReplyItem::Command;
  r.command = Command{12, CommandKind::Water, 80};
  size_t n = encode(r, 1000, kKey, kNode, buf);
  TEST_ASSERT_TRUE(checkTag(buf, n, kKey, kNode));
  ReplyMsg b;
  TEST_ASSERT_TRUE(decode(buf, n, b));
  TEST_ASSERT_EQUAL_UINT32(1767254400, b.epoch);
  TEST_ASSERT_EQUAL_INT32(7200, b.utcOffsetS);
  TEST_ASSERT_FALSE(b.automation);
  TEST_ASSERT_TRUE(b.stop && b.more && b.env.valid && b.env.luxValid);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 23.4f, b.env.tempC);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 48.5f, b.env.humidityPct);
  TEST_ASSERT_EQUAL_FLOAT(5400, b.env.lux);
  TEST_ASSERT_EQUAL(ReplyItem::Command, b.item);
  TEST_ASSERT_EQUAL(12, b.command.id);
  TEST_ASSERT_EQUAL(CommandKind::Water, b.command.kind);
  TEST_ASSERT_EQUAL_FLOAT(80, b.command.arg);

  r.item = ReplyItem::Config;
  r.chunk.version = 0xdeadbeef;
  r.chunk.total = 700;
  r.chunk.offset = 180;
  r.chunk.data = std::string(kMaxChunk, '{');
  n = encode(r, 1001, kKey, kNode, buf);
  TEST_ASSERT_TRUE(n > 0 && n <= kMaxFrame);
  TEST_ASSERT_TRUE(decode(buf, n, b));
  TEST_ASSERT_EQUAL(ReplyItem::Config, b.item);
  TEST_ASSERT_EQUAL_HEX32(0xdeadbeef, b.chunk.version);
  TEST_ASSERT_EQUAL(700, b.chunk.total);
  TEST_ASSERT_EQUAL(180, b.chunk.offset);
  TEST_ASSERT_EQUAL(kMaxChunk, b.chunk.data.size());

  r.chunk.data = std::string(kMaxChunk + 1, '{');
  TEST_ASSERT_EQUAL(0, encode(r, 1002, kKey, kNode, buf));  // would not fit
}

void test_rejects_foreign_frames() {
  uint8_t buf[kMaxFrame] = {0x12, 0x34, 1, 0, 0, 0, 0, 0};
  Header h;
  TEST_ASSERT_FALSE(parseHeader(buf, 8, h));  // not our magic
  RejectMsg rej;
  const size_t n = encode(rej, 3, buf);
  TEST_ASSERT_TRUE(parseHeader(buf, n, h));
  buf[1] = kProtocol + 1;
  TEST_ASSERT_FALSE(parseHeader(buf, n, h));  // a future protocol version
  TEST_ASSERT_FALSE(parseHeader(buf, 4, h));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_sha256_vectors);
  RUN_TEST(test_hmac_rfc4231);
  RUN_TEST(test_mac_ids);
  RUN_TEST(test_hello_and_welcome_round_trip);
  RUN_TEST(test_report_round_trip_and_size);
  RUN_TEST(test_tampering_and_wrong_key_are_detected);
  RUN_TEST(test_event_round_trip_clips_long_text);
  RUN_TEST(test_utf8_clipping_never_splits_a_character);
  RUN_TEST(test_reply_with_command_and_with_config);
  RUN_TEST(test_rejects_foreign_frames);
  return UNITY_END();
}
