// The main station's side of the radio: every node it has heard, the latest report
// from each, and a mailbox per node that rides on the replies.
//
// Transport-free like NodeSession: the firmware passes in each received frame and
// sends back whatever onFrame() writes.
#pragma once

#include <array>
#include <deque>
#include <string>
#include <vector>

#include "Wire.h"

namespace plantlink {

// What survives a reboot of the main station.
struct NodeRecord {
  std::string id;  // macToId(mac)
  Key key{};
  bool keyPending = false;  // the node has not used its key yet: hand it over in the next Welcome
};

struct NodeView {
  std::string id;
  Mac mac{};
  bool adopted = false;
  Key key{};
  bool keyPending = false;
  bool wantsRepair = false;  // an added node said Hello again (it lost its key)
  uint8_t board = 0;
  std::string fw;
  int64_t firstSeenMs = 0;
  int64_t lastSeenMs = -1;   // -1 = not since the station started
  bool online = false;
  bool haveReport = false;
  ReportMsg report;
  int64_t reportMs = 0;
  NodeAck ack;
  // Mailbox.
  std::deque<Command> queue;
  uint16_t nextCmdId = 1;
  bool idsSynced = false;     // nextCmdId follows what the node has already carried out
  std::string config;
  uint32_t configVersion = 0; // FNV-1a of config, 0 = none
  bool forgetting = false;    // a Forget is queued: no new commands or config
  bool forgetSent = false;    // ...and went out in a reply: drop the record
  // Link.
  bool haveSeq = false;
  uint32_t lastSeq = 0;
  size_t lastReplyLen = 0;
  std::array<uint8_t, kMaxFrame> lastReply{};

  bool configPending() const { return configVersion != 0 && ack.cfgVersion != configVersion; }
};

// What every Reply tells a node.
struct ReplyContext {
  uint32_t epoch = 0;
  int32_t utcOffsetS = 0;
  plantcore::EnvSnapshot env;
  bool automation = true;
  bool stop = false;
  uint8_t channel = 1;
  uint16_t checkInS = 300;
};

class IFleetSink {
 public:
  virtual ~IFleetSink() = default;
  virtual void onNodeEvent(const NodeView& n, const plantcore::Event& e) = 0;
  virtual void onNodeReport(const NodeView& n) = 0;
  // A node said Hello: new, waiting to be added, or asking to pair again.
  virtual void onNodeHello(const NodeView& n) = 0;
  virtual void onNodeRecordsChanged() = 0;  // persist records()
  // A forgotten node was told and its record is gone.
  virtual void onNodeForgotten(const std::string& id) = 0;
};

constexpr size_t kMaxFleet = 24;                 // adopted + waiting nodes
constexpr int64_t kPendingForgetMs = 3600000;    // a waiting node not heard for an hour drops out
constexpr int kOfflineAfterCheckIns = 3;
constexpr size_t kMaxQueuedCommands = 8;

uint32_t fnv1a(const std::string& s);

class Fleet {
 public:
  explicit Fleet(IFleetSink& sink) : sink_(sink) {}

  void restore(const std::vector<NodeRecord>& records);
  std::vector<NodeRecord> records() const;

  // Handles one received frame; returns the reply length (0 = no reply).
  size_t onFrame(const Mac& from, const uint8_t* d, size_t n, int64_t nowMs, const ReplyContext& ctx, uint8_t* out);

  // Adds a waiting node (or re-pairs one): it gets this key in its next Welcome.
  bool adopt(const std::string& id, const Key& key);
  // Tells the node to unpair at its next check-in, then drops it.
  bool forget(const std::string& id);
  void setConfig(const std::string& id, const std::string& json);
  // Returns the command id, 0 if the node is unknown or its mailbox is full.
  uint16_t queue(const std::string& id, CommandKind kind, float arg);
  // Seconds until the node is expected to check in (0 = any moment, -1 = unknown).
  int64_t etaS(const std::string& id, int64_t nowMs, uint16_t checkInS) const;

  void tick(int64_t nowMs, uint16_t checkInS);

  const NodeView* find(const std::string& id) const;
  const std::vector<NodeView>& nodes() const { return nodes_; }

 private:
  NodeView* findMut(const std::string& id);
  NodeView* findMac(const Mac& mac);
  NodeView& add(const Mac& mac, int64_t nowMs);
  size_t welcome(NodeView& n, uint32_t seq, const ReplyContext& ctx, uint8_t* out);
  size_t reply(NodeView& n, uint32_t seq, const ReplyContext& ctx, uint8_t* out);
  void applyAck(NodeView& n, const NodeAck& a);
  void seen(NodeView& n, int64_t nowMs);

  IFleetSink& sink_;
  std::vector<NodeView> nodes_;
};

}  // namespace plantlink
