// The plant node: one plant, its bottle and pump, run by the plantcore engine, and a
// check-in with the main station every few minutes. Between check-ins it sleeps.
//
//   wake -> (button?) -> read soil -> engine tick -> check in if due -> sleep
//
// While the pump runs the node stays awake and ticks every 100 ms.
#pragma once

#include <string>

#include "Engine.h"
#include "NodeHw.h"
#include "NodeRadio.h"
#include "NodeSession.h"
#include "NodeStore.h"

class Node : public plantlink::INodeApp, public plantcore::IEngineSink {
 public:
  Node();
  void begin();
  // One pass; returns how long to sleep (ms).
  uint32_t step();

  // INodeApp
  void fillReport(plantlink::ReportMsg& r) override;
  void onStationState(const plantlink::ReplyMsg& r) override;
  void onCommand(const plantlink::Command& c) override;
  bool onConfig(const std::string& json) override;
  void onPairingChanged() override;
  void onUnpaired() override;

  // IEngineSink
  void onEvent(const plantcore::Event& e) override;
  void onPersist(const plantcore::PersistedState& s) override;

 private:
  int64_t nowMs() const;
  plantcore::Inputs inputs(int64_t now, bool sampleSoil);
  void handleButton(NodeHw::Press p, int64_t now);
  void checkIn(int64_t now);
  bool converse();
  uint32_t checkInIntervalMs(int64_t now) const;
  void applyConfig();

  NodeHw hw_;
  NodeStore store_;
  NodeRadio radio_;
  plantlink::NodeSession session_;
  plantcore::Engine engine_;
  plantcore::Settings settings_;
  plantcore::PlantProfile plant_;
  bool havePlant_ = false;

  // Station clock and room climate, from the last reply.
  uint32_t epochAt_ = 0;
  int64_t epochAtMs_ = 0;
  int32_t utcOffsetS_ = 0;
  plantcore::EnvSnapshot env_;
  int64_t envAtMs_ = -1;

  int64_t lastSoilMs_ = -1000000;
  int64_t nextCheckInMs_ = 0;
  int64_t unpairedSinceMs_ = 0;
  bool checkInNow_ = true;
  float lastBatteryMv_ = 0;
};
