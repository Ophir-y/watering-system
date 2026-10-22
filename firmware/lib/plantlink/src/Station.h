// The main station's brain for the humans: plant profiles and settings, which node
// waters which plant, alerts gathered from every node, commands from the dashboard,
// and the /api/status document. Host-testable: the firmware only adds Wi-Fi, flash,
// the SD card and the web server around it.
#pragma once

#include <ArduinoJson.h>

#include <array>
#include <string>
#include <vector>

#include "Alerts.h"
#include "Fleet.h"
#include "History.h"
#include "Types.h"

namespace plantlink {

// Device/network facts the station core does not know about, filled in by the firmware.
struct SystemInfo {
  std::string version;
  int64_t uptimeS = 0;
  uint32_t heapFree = 0;
  std::string resetReason;
  std::string wifiMode;  // "sta", "ap", "sta+ap", "off"
  std::string ssid;
  std::string ip;
  int rssi = 0;
  bool apActive = false;
  std::string apSsid;
  bool internet = false;
  bool sdOk = false;
  float sdUsedMb = 0;
  float sdTotalMb = 0;
};

struct ClockInfo {
  bool valid = false;
  plantcore::EpochS now = 0;
  int32_t utcOffsetS = 0;
};

class IStationSink {
 public:
  virtual ~IStationSink() = default;
  virtual void onEvent(const plantcore::Event& e, const std::string& plantId) = 0;  // log + live push
  virtual void onReading(const plantcore::ReadingRow& r) = 0;
  virtual bool persistPlants(const std::vector<plantcore::PlantProfile>& plants) = 0;
  virtual bool persistSettings(const plantcore::Settings& s) = 0;
  virtual bool persistNodes(const std::vector<NodeRecord>& nodes) = 0;
  virtual bool persistStopped(bool stopped) = 0;
  virtual Key newKey() = 0;
};

struct CommandResult {
  std::string error;  // "" = queued
  uint16_t id = 0;
  int64_t etaS = -1;  // seconds until the node is expected to check in, -1 = unknown
};

constexpr int64_t kButtonHighlightMs = 60000;
constexpr float kBatteryEmptyMv = 3300;  // 3xAA alkaline
constexpr float kBatteryFullMv = 4650;

float batteryPct(float mv);

class Station : public IFleetSink {
 public:
  explicit Station(IStationSink& sink) : sink_(sink), fleet_(*this) {}

  void begin(const plantcore::Settings& s, const std::vector<plantcore::PlantProfile>& plants,
             const std::vector<NodeRecord>& nodes, bool stopped);

  void setClock(const ClockInfo& c) { clock_ = c; }
  void setEnvironment(const plantcore::EnvSnapshot& e) { env_ = e; }
  void setRadioChannel(uint8_t ch) { channel_ = ch; }
  uint8_t radioChannel() const { return channel_; }
  size_t onFrame(const Mac& from, const uint8_t* d, size_t n, int64_t nowMs, uint8_t* out);
  void tick(int64_t nowMs);

  // Configuration. Return "" or an error message.
  const plantcore::Settings& settings() const { return settings_; }
  const std::vector<plantcore::PlantProfile>& plants() const { return plants_; }
  const plantcore::PlantProfile* plant(const std::string& id) const;
  // p.node set to a waiting node adds that node; p receives the saved profile.
  std::string savePlant(plantcore::PlantProfile& p, const std::string& replaceId);
  std::string deletePlant(const std::string& id);
  std::string saveSettings(const plantcore::Settings& s);
  std::string restoreConfig(const plantcore::Settings& s, const std::vector<plantcore::PlantProfile>& plants);
  // A plant's node lost its key and asks to pair again: hand it a new one.
  std::string repairNode(const std::string& nodeId);

  // Commands for a plant's node, delivered at its next check-in.
  CommandResult water(const std::string& plantId, float ml);
  CommandResult timedRun(const std::string& plantId, float seconds);
  CommandResult refilled(const std::string& plantId, float ml);
  CommandResult clearFaults(const std::string& plantId);
  CommandResult identify(const std::string& plantId);
  // Uses the node's latest reading; ageS tells how old it was.
  std::string calibrate(const std::string& plantId, const std::string& point, int32_t& raw, int64_t& ageS);
  void emergencyStop(const std::string& why);
  void resume();
  std::string setAutomation(bool enabled);
  bool emergencyStopped() const { return stopped_; }

  const plantcore::AlertManager& alerts() const { return alerts_; }
  bool ackAlert(uint32_t id) { return alerts_.ack(id); }
  void ackAll();
  void markNotified(uint32_t id) { alerts_.markNotified(id, clock_.now); }
  void setSystemAlert(plantcore::AlertCode code, plantcore::Severity sev, bool active, const std::string& message);
  std::string plantIdForChannel(int8_t ch) const;

  const Fleet& fleet() const { return fleet_; }
  void statusToJson(const SystemInfo& sys, int64_t nowMs, JsonObject out) const;

  // IFleetSink
  void onNodeEvent(const NodeView& n, const plantcore::Event& e) override;
  void onNodeReport(const NodeView& n) override;
  void onNodeHello(const NodeView& n) override;
  void onNodeRecordsChanged() override;
  void onNodeForgotten(const std::string& id) override;

 private:
  int plantIndex(const std::string& id) const;
  int plantForNode(const std::string& nodeId) const;
  int slotOf(const std::string& plantId) const;
  void assignSlots();
  void pushConfigs();
  std::string validatePlants(const std::vector<plantcore::PlantProfile>& plants) const;
  std::string applyPlants(std::vector<plantcore::PlantProfile> plants);
  CommandResult command(const std::string& plantId, CommandKind kind, float arg);
  ReplyContext replyContext() const;
  void raise(plantcore::AlertCode code, plantcore::Severity sev, int slot, const std::string& msg);
  void clear(plantcore::AlertCode code, int slot);
  void log(plantcore::EventType type, const std::string& plantId, const std::string& code, float ml,
           const std::string& detail);
  plantcore::EpochS epochAt(int64_t ms, int64_t nowMs) const;

  IStationSink& sink_;
  Fleet fleet_;
  plantcore::Settings settings_;
  std::vector<plantcore::PlantProfile> plants_;
  std::array<std::string, plantcore::kMaxNodes> slots_;  // alert channel -> plant id
  std::array<int64_t, plantcore::kMaxNodes> lastLogMs_{};
  std::array<int64_t, plantcore::kMaxNodes> buttonMs_{};
  plantcore::AlertManager alerts_;
  ClockInfo clock_;
  plantcore::EnvSnapshot env_;
  uint8_t channel_ = 1;
  bool stopped_ = false;
  int64_t nowMs_ = 0;
  std::vector<std::string> announced_;  // waiting nodes already logged
};

}  // namespace plantlink
