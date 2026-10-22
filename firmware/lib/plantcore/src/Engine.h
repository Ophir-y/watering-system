// The plant node's control engine: turns sensor snapshots into watering decisions
// for its one plant, enforces the safety rules, keeps the bottle's fuel gauge,
// judges the pump by its current, and tracks faults and alerts. It owns no
// hardware: the pump is driven through IPump and persistence/logging go through
// IEngineSink, so the whole engine runs unchanged in host tests.
#pragma once

#include <array>
#include <string>

#include "Alerts.h"
#include "Bottle.h"
#include "Dose.h"
#include "EnvModel.h"
#include "Planner.h"
#include "PumpMonitor.h"
#include "Safety.h"
#include "SensorMonitor.h"
#include "Signal.h"
#include "Types.h"

namespace plantcore {

class IEngineSink {
 public:
  virtual ~IEngineSink() = default;
  virtual void onEvent(const Event& e) = 0;
  // Must complete before returning: called right before the pump starts.
  virtual void onPersist(const PersistedState& s) = 0;
};

// The most recent completed dose: used to calibrate the pump flow.
struct LastDose {
  bool valid = false;
  float ml = 0;
  uint32_t onTimeMs = 0;
  bool timed = false;
  DoseFault fault = DoseFault::None;
  EpochS at = 0;
};

struct NodeStatus {
  bool hasPlant = false;
  std::string plantId;
  bool haveMoisture = false;
  float moisturePct = 0;
  int32_t raw = 0;
  SensorHealth health = SensorHealth::Ok;
  SensorIssue issue = SensorIssue::None;
  PlannerState planner = PlannerState::Idle;
  float triggerPct = 0;
  float targetPct = 0;
  float mlToday = 0;
  EpochS lastWateredAt = 0;
  bool lockedOut = false;
  float mlPerPct = 0;
  MonoMs soakRemainingMs = 0;
  BottleStatus bottle;
  bool doseActive = false;
  float doseMl = 0;
  float doseDispensedMl = 0;
  LastDose lastDose;
  PumpVerdict pumpVerdict = PumpVerdict::Ok;  // of the last run
  bool pumpReady = false;                     // dry/jam detection learned
  float pumpWetMaPerV = 0;
  float batteryMv = 0;
  bool batteryLow = false;
  bool stopped = false;
  bool automationEnabled = true;
  uint32_t activeAlerts = 0;  // bit (1 << AlertCode) per active alert
};

constexpr MonoMs kRecentlyWateredMs = 30LL * 60 * 1000;
constexpr MonoMs kEnvAlertAfterMs = 2LL * 3600 * 1000;
constexpr MonoMs kTooWetAfterMs = 48LL * 3600 * 1000;
constexpr MonoMs kSessionSampleMs = 60000;   // sample every minute during a session
constexpr float kDetectedWateringRisePct = 8.0f;
constexpr float kPumpLearnMinBottlePct = 15; // the gauge must think there is water to learn the wet current
constexpr float kBatteryRecoverMv = 150;     // hysteresis for the battery alerts

const char* sessionKindName(SessionKind k);
const char* sessionEndName(SessionEnd e);
const char* plannerStateName(PlannerState s);

class Engine {
 public:
  Engine(IPump& pump, IEngineSink& sink);

  // Apply settings and the plant profile (nullptr: the node has no plant yet). Safe to
  // call at any time; a different plant id resets the per-plant runtime, while the
  // bottle and pump state stay (they belong to the hardware).
  void configure(const Settings& settings, const PlantProfile* plant);
  // Restore persisted state once at boot, after configure and before the first tick.
  void restore(const PersistedState& state, const Inputs& in);
  void tick(const Inputs& in);

  // Commands. Return "" on success, otherwise a short error code. An accepted dose
  // starts on the next tick (which is when the pump's clock is known).
  std::string manualWater(float ml);
  // Runs the pump for a fixed time so its flow can be measured into a cup. The water
  // leaves the bottle but does not count as watering the plant.
  std::string timedRun(float seconds);
  std::string clearFaults();
  void refilled(float ml);  // ml <= 0: filled to capacity
  void setStopped(bool stopped, const std::string& why);
  void setAutomation(bool enabled);
  // Node-level conditions detected outside the engine (resets, ...).
  void setSystemAlert(AlertCode code, Severity sev, bool active, const std::string& message);

  NodeStatus status() const;
  PersistedState persisted() const;
  const AlertManager& alerts() const { return alerts_; }
  const Settings& settings() const { return settings_; }
  bool hasPlant() const { return havePlant_; }
  const PlantProfile& plant() const { return plant_; }
  bool doseActive() const { return !dose_.idle() || pending_.active; }
  bool pumpRunning() const { return dose_.running(); }
  bool stopped() const { return stopped_; }
  // When the engine next needs a tick: -1 = nothing before the next regular check-in.
  MonoMs wantTickBy(MonoMs now) const;

 private:
  static void pushTrend(float pct, MonoMs now, std::array<float, 3>& pcts, std::array<MonoMs, 3>& ms, int& n,
                        float& rate);
  void resetPlant();
  void rolloverDay(const Inputs& in);
  void processBattery(const Inputs& in);
  void processMoisture(const Inputs& in);
  void processEnvironment(const Inputs& in);
  void processBottle();
  void stepDose(const Inputs& in);
  void finishDose(const Inputs& in);
  void stepPlanner(const Inputs& in);
  void handleReport(const SessionReport& r, const Inputs& in);
  void startDose(float ml, SessionKind kind, bool timed);
  std::string manualDose(float ml, bool timed);

  bool sensorUsable(const Inputs& in) const;
  float secondsSinceLastSession(const Inputs& in) const;
  float effectiveFlow() const;
  // Caps an allowed dose to the water the gauge says is above the reserve.
  bool fitToGauge(Verdict& v);
  NodeState nodeState() const;

  void raise(AlertCode code, Severity sev, int ch, const std::string& msg);
  void clear(AlertCode code, int ch);
  void event(EventType type, int ch, const std::string& code, float ml, const std::string& detail);
  void persist();
  std::string label() const;

  IPump& pump_;
  IEngineSink& sink_;
  Settings settings_;
  PlantProfile plant_;
  bool havePlant_ = false;

  // The plant.
  SensorMonitor monitor_;
  Ema ema_{0.3f};
  Planner planner_;
  PlantRuntime runtime_;
  bool haveMoisture_ = false;
  float moisturePct_ = 0;
  int32_t raw_ = 0;
  std::array<float, 3> trendPct_{};  // last three smoothed readings, oldest first
  std::array<MonoMs, 3> trendMs_{};
  int trendN_ = 0;
  float trendPctPerMin_ = 0;
  MonoMs lastSampleMs_ = 0;
  MonoMs lastDoseEndMs_ = -kRecentlyWateredMs;
  bool sessionMono_ = false;  // lastSessionMonoMs_ valid
  MonoMs lastSessionMonoMs_ = 0;
  MonoMs wetSinceMs_ = -1;
  MonoMs tempOutSinceMs_ = -1;
  MonoMs humOutSinceMs_ = -1;
  BlockReason lastLoggedBlock_ = BlockReason::None;

  // The hardware.
  struct PendingDose {
    bool active = false;
    float ml = 0;
    bool timed = false;
  };
  PendingDose pending_;  // accepted manual dose, started by the next tick
  DoseController dose_;
  SessionKind doseKind_ = SessionKind::Auto;
  bool doseTimed_ = false;
  LastDose lastDose_;
  BottleModel bottle_;
  PumpMonitor pumpMon_;
  PumpVerdict lastPumpVerdict_ = PumpVerdict::Ok;
  float batteryMv_ = 0;
  bool batteryLow_ = false;
  bool batteryCritical_ = false;
  MonoMs batteryQuietUntilMs_ = 0;  // the cells recover for a moment after pumping

  AlertManager alerts_{10};
  Inputs last_;
  int32_t day_ = 0;
  MonoMs lastRolloverMs_ = 0;
  bool stopped_ = false;
  bool dispenseInProgress_ = false;
  float inProgressMl_ = 0;
};

}  // namespace plantcore
