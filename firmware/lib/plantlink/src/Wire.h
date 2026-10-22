// The radio protocol between plant nodes and the main station (ESP-NOW frames of at
// most 250 bytes). Pure C++17 so both ends and the host tests share one codec.
//
// Nodes speak first, the station answers: every node frame gets exactly one reply.
//
//   unpaired node  Hello  ->  Welcome   (the key is handed over once, after you add
//                                        the plant in the dashboard)
//   paired node    Report ->  Reply     (time, room climate, stop/pause, and one
//                  Event  ->  Reply      mailbox item: a command or a config chunk)
//                  Poll   ->  Reply     ("anything more?")
//   unknown node   any    ->  Reject
//
// Header: magic, protocol, type, flags, seq (u32). Node frames carry the node's own
// increasing sequence number; a Reply echoes the seq it answers. Frames after
// pairing end with an 8-byte HMAC-SHA256 tag over (node MAC, header, payload).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "Types.h"

namespace plantlink {

constexpr uint8_t kMagic = 0xA7;
constexpr uint8_t kProtocol = 1;
constexpr size_t kMaxFrame = 250;
constexpr size_t kHeaderLen = 8;
constexpr size_t kTagLen = 8;
constexpr size_t kKeyLen = 16;
constexpr size_t kMaxChunk = 180;  // config bytes per Reply
constexpr size_t kMaxText = 170;   // event detail bytes (longer texts are cut at a UTF-8 boundary)

using Mac = std::array<uint8_t, 6>;
using Key = std::array<uint8_t, kKeyLen>;

std::string macToId(const Mac& mac);  // 12 lowercase hex digits
bool idToMac(const std::string& id, Mac& out);

enum class MsgType : uint8_t { Hello = 1, Welcome = 2, Report = 3, Event = 4, Poll = 5, Reply = 6, Reject = 7 };
constexpr uint8_t kFlagSigned = 0x01;

enum class Board : uint8_t { Unknown = 0, SuperMiniC3 = 1, XiaoC3 = 2, XiaoS3 = 3 };
const char* boardName(uint8_t board);

struct Header {
  MsgType type = MsgType::Hello;
  uint8_t flags = 0;
  uint32_t seq = 0;
};

// What the node has received so far; sent in every node frame.
struct NodeAck {
  uint32_t cfgVersion = 0;    // config the node runs (0 = none)
  uint32_t cfgRxVersion = 0;  // config being received...
  uint16_t cfgRxOffset = 0;   // ...and how many bytes of it have arrived
  uint16_t lastCmdId = 0;     // newest command carried out
};

struct HelloMsg {
  uint8_t board = 0;
  std::string fw;
};

struct WelcomeMsg {
  uint8_t channel = 0;
  bool adopted = false;  // key is valid: the node is paired from now on
  Key key{};
  uint16_t checkInS = 300;
};

struct RejectMsg {
  uint8_t reason = 1;  // 1 = unknown node
};

struct ReportMsg {
  NodeAck ack;
  std::string fw;
  uint32_t uptimeS = 0;
  uint16_t batteryMv = 0;  // 0 = unknown
  uint8_t linkPct = 255;   // replies heard / frames sent recently, 255 = unknown
  bool hasPlant = false;
  bool haveMoisture = false;
  bool lockedOut = false;
  bool doseActive = false;
  bool stopped = false;
  bool pumpReady = false;
  bool bottleEmpty = false;
  bool bottleLow = false;
  bool batteryLow = false;
  bool button = false;     // the node's button was pressed: highlight this plant
  bool timeValid = false;
  float moisturePct = 0;
  int32_t raw = 0;
  uint8_t health = 0;      // plantcore::SensorHealth
  uint8_t issue = 0;       // plantcore::SensorIssue
  uint8_t planner = 0;     // plantcore::PlannerState
  float triggerPct = 0;
  float targetPct = 0;
  float mlToday = 0;
  uint32_t lastWateredAt = 0;
  float mlPerPct = 0;
  uint16_t soakRemainingS = 0;
  float bottleMl = 0;
  float bottleCapacityMl = 0;
  float daysLeft = -1;
  uint8_t emptyReason = 0;  // plantcore::EmptyReason
  float flowFactor = 1;
  uint8_t pumpVerdict = 0;  // plantcore::PumpVerdict of the last run
  float doseMl = 0;
  float doseDispensedMl = 0;
  float lastDoseMl = 0;
  uint32_t lastDoseOnMs = 0;
  uint32_t lastDoseAt = 0;
  uint32_t activeAlerts = 0;
  uint8_t outbox = 0;       // events still queued on the node
};

struct EventMsg {
  NodeAck ack;
  plantcore::Event event;
};

struct PollMsg {
  NodeAck ack;
};

enum class CommandKind : uint8_t { None = 0, Water = 1, TimedRun = 2, Refilled = 3, ClearFaults = 4, Identify = 5, Forget = 6 };
const char* commandKindName(CommandKind k);

struct Command {
  uint16_t id = 0;
  CommandKind kind = CommandKind::None;
  float arg = 0;  // Water: ml; TimedRun: seconds; Refilled: ml (0 = full)
};

struct ConfigChunk {
  uint32_t version = 0;
  uint16_t total = 0;
  uint16_t offset = 0;
  std::string data;
};

enum class ReplyItem : uint8_t { None = 0, Command = 1, Config = 2 };

struct ReplyMsg {
  uint32_t epoch = 0;        // 0 = the station's clock is not set
  int32_t utcOffsetS = 0;    // local time = epoch + utcOffsetS
  bool automation = true;
  bool stop = false;         // emergency stop from the dashboard
  bool more = false;         // the station has more for this node: send a Poll
  plantcore::EnvSnapshot env;
  ReplyItem item = ReplyItem::None;
  Command command;
  ConfigChunk chunk;
};

// ---- encoding: returns the frame length, 0 if it does not fit.
size_t encode(const HelloMsg& m, uint32_t seq, uint8_t* out);
size_t encode(const WelcomeMsg& m, uint32_t seq, uint8_t* out);
size_t encode(const RejectMsg& m, uint32_t seq, uint8_t* out);
size_t encode(const ReportMsg& m, uint32_t seq, const Key& key, const Mac& node, uint8_t* out);
size_t encode(const EventMsg& m, uint32_t seq, const Key& key, const Mac& node, uint8_t* out);
size_t encode(const PollMsg& m, uint32_t seq, const Key& key, const Mac& node, uint8_t* out);
size_t encode(const ReplyMsg& m, uint32_t seq, const Key& key, const Mac& node, uint8_t* out);

// ---- decoding
bool parseHeader(const uint8_t* d, size_t n, Header& h);
// Signed frames: the tag matches this key and node MAC.
bool checkTag(const uint8_t* d, size_t n, const Key& key, const Mac& node);
bool decode(const uint8_t* d, size_t n, HelloMsg& m);
bool decode(const uint8_t* d, size_t n, WelcomeMsg& m);
bool decode(const uint8_t* d, size_t n, RejectMsg& m);
bool decode(const uint8_t* d, size_t n, ReportMsg& m);
bool decode(const uint8_t* d, size_t n, EventMsg& m);
bool decode(const uint8_t* d, size_t n, PollMsg& m);
bool decode(const uint8_t* d, size_t n, ReplyMsg& m);

// Cuts text to at most maxLen bytes without splitting a UTF-8 character.
std::string clipUtf8(const std::string& s, size_t maxLen);

}  // namespace plantlink
