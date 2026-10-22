// An in-memory radio between plant nodes and the main station, with lost frames on
// demand, for the protocol and station tests.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "Fleet.h"
#include "NodeSession.h"

namespace linksim {

using namespace plantlink;

inline const Mac kHubMac{0x24, 0x6f, 0x28, 0x00, 0x00, 0x01};

inline Mac nodeMac(uint8_t n) { return Mac{0xa0, 0xb1, 0xc2, 0xd3, 0xe4, n}; }

struct FakeApp : INodeApp {
  ReportMsg base;  // what fillReport reports
  std::vector<Command> commands;
  std::vector<std::string> configs;
  std::vector<ReplyMsg> states;
  int pairingWrites = 0;
  int unpaired = 0;
  void fillReport(ReportMsg& r) override { r = base; }
  void onStationState(const ReplyMsg& r) override { states.push_back(r); }
  void onCommand(const Command& c) override { commands.push_back(c); }
  bool onConfig(const std::string& json) override {
    configs.push_back(json);
    return true;
  }
  void onPairingChanged() override { pairingWrites++; }
  void onUnpaired() override { unpaired++; }
};

// One node with its session.
struct SimNode {
  Mac mac;
  FakeApp app;
  NodeSession session;
  explicit SimNode(uint8_t n, const char* fw = "0.2.0")
      : mac(nodeMac(n)), session(app, mac, static_cast<uint8_t>(Board::SuperMiniC3), fw) {
    app.base.hasPlant = true;
    app.base.haveMoisture = true;
    app.base.moisturePct = 40;
    app.base.raw = 15500;
    app.base.batteryMv = 4300;
    app.base.bottleMl = 900;
    app.base.bottleCapacityMl = 1500;
  }
};

using StationFn = std::function<size_t(const Mac& from, const uint8_t* d, size_t n, uint8_t* out)>;

// Which transmissions get lost: called with a running count of frames on the air
// (node frames and replies alike, starting at 0 for each check-in).
using DropFn = std::function<bool(int index)>;
inline DropFn noLoss() {
  return [](int) { return false; };
}

struct CheckIn {
  int frames = 0;   // node transmissions
  int replies = 0;  // replies the node heard
  bool done = false;
};

// Runs one check-in conversation to the end.
inline CheckIn checkIn(SimNode& node, const StationFn& station, const DropFn& drop = noLoss()) {
  CheckIn r;
  OutFrame f;
  node.session.start(f);
  int air = 0;
  for (int guard = 0; guard < 200; ++guard) {
    r.frames++;
    uint8_t reply[kMaxFrame];
    size_t rl = 0;
    if (!drop(air++) && f.len) rl = station(node.mac, f.data, f.len, reply);
    Step s;
    if (rl && !drop(air++)) {
      r.replies++;
      s = node.session.onReceive(kHubMac, reply, rl, f);
      if (s == Step::Wait) s = node.session.onTimeout(f);
    } else {
      s = node.session.onTimeout(f);
    }
    if (s == Step::Done) {
      r.done = true;
      break;
    }
  }
  return r;
}

struct RecordingFleetSink : IFleetSink {
  std::vector<plantcore::Event> events;
  int reports = 0;
  int hellos = 0;
  int recordWrites = 0;
  std::vector<std::string> forgotten;
  void onNodeEvent(const NodeView&, const plantcore::Event& e) override { events.push_back(e); }
  void onNodeReport(const NodeView&) override { reports++; }
  void onNodeHello(const NodeView&) override { hellos++; }
  void onNodeRecordsChanged() override { recordWrites++; }
  void onNodeForgotten(const std::string& id) override { forgotten.push_back(id); }
};

inline Key testKey(uint8_t seed) {
  Key k;
  for (size_t i = 0; i < k.size(); ++i) k[i] = static_cast<uint8_t>(seed * 31 + i * 7);
  return k;
}

}  // namespace linksim
