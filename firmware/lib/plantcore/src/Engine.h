// The control engine: turns sensor snapshots into watering decisions, enforces
// safety interlocks, tracks faults and alerts. It owns no hardware: outlets are
// driven through IOutlets and persistence/logging go through IEngineSink, so the
// whole engine runs unchanged in host tests.
#pragma once

#include <array>
#include <string>
#include <vector>

#include "Alerts.h"
#include "Dose.h"
#include "EnvModel.h"
#include "Planner.h"
#include "Reservoir.h"
#include "Safety.h"
#include "SensorMonitor.h"
#include "Signal.h"
#include "Types.h"

namespace plantcore {

class IEngineSink {
 public:
  virtual ~IEngineSink() = default;
  virtual void onEvent(const Event& e) = 0;
  // Must complete before returning: called right before an outlet opens.
  virtual void onPersist(const PersistedState& s) = 0;
};

struct PlantStatus {
  std::string id;
  std::string name;
  uint8_t channel = 0;
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
};

struct DoseStatus {
  bool active = false;
  uint8_t channel = 0;
  std::string plantId;
  SessionKind kind = SessionKind::Auto;
  float ml = 0;
  float dispensedMl = 0;
};

// The most recent completed dispense: used to calibrate outlet flow.
struct LastDose {
  bool valid = false;
  uint8_t channel = 0;
  std::string plantId;
  float ml = 0;
  uint32_t onTimeMs = 0;
  bool closedLoop = false;
  EpochS at = 0;
};

struct EngineStatus {
  bool automationEnabled = true;
  bool emergencyStop = false;
  bool leak = false;
  ReservoirStatus reservoir;
  EnvSnapshot env;
  float vpdKPa = 0;
  DoseStatus dose;
  LastDose lastDose;
  float mlTodayTotal = 0;
  std::vector<PlantStatus> plants;
  int alertsCritical = 0;
  int alertsWarning = 0;
  int alertsInfo = 0;
};

constexpr MonoMs kRecentlyWateredMs = 30LL * 60 * 1000;
constexpr MonoMs kEnvAlertAfterMs = 2LL * 3600 * 1000;
constexpr MonoMs kTooWetAfterMs = 48LL * 3600 * 1000;
constexpr MonoMs kScaleFaultAfterMs = 30000;
constexpr float kDetectedWateringRisePct = 8.0f;

const char* sessionKindName(SessionKind k);
const char* sessionEndName(SessionEnd e);
const char* plannerStateName(PlannerState s);

class Engine {
 public:
  Engine(IOutlets& outlets, IEngineSink& sink);

  // Apply settings and plant profiles. Safe to call at any time; channels whose
  // plant changed lose their per-plant runtime.
  void configure(const Settings& settings, const std::vector<PlantProfile>& plants);
  // Restore persisted state once at boot, before the first tick.
  void restore(const PersistedState& state, const Inputs& in);
  void tick(const Inputs& in);

  // Commands. Return "" on success, otherwise a short error code.
  std::string manualWater(const std::string& plantId, float ml);
  // Opens the outlet for a fixed time (open loop) so its flow can be measured.
  std::string timedRun(const std::string& plantId, float seconds);
  void emergencyStop(const std::string& why);
  std::string resume();
  void setAutomation(bool enabled);
  std::string clearLockout(const std::string& plantId);
  bool ackAlert(uint32_t id) { return alerts_.ack(id); }
  // System-level conditions detected outside the engine (SD card, clock, resets...).
  void setSystemAlert(AlertCode code, Severity sev, bool active, const std::string& message);
  void noteManualRefill(float ml);

  EngineStatus status() const;
  const AlertManager& alerts() const { return alerts_; }
  AlertManager& alerts() { return alerts_; }
  PersistedState persisted() const;
  const Settings& settings() const { return settings_; }
  const std::vector<PlantProfile>& plants() const { return plants_; }
  int plantIndex(const std::string& id) const;
  bool doseActive() const { return !dose_.idle(); }
  bool emergencyStopped() const { return emergencyStop_; }

 private:
  struct Channel {
    int plant = -1;  // index into plants_, -1 = unused
    SensorMonitor monitor;
    Ema ema{0.3f};
    Planner planner;
    PlantRuntime runtime;
    bool haveMoisture = false;
    float moisturePct = 0;
    int32_t raw = 0;
    std::array<float, 3> trendPct{};  // last three smoothed readings, oldest first
    std::array<MonoMs, 3> trendMs{};
    int trendN = 0;
    float trendPctPerMin = 0;
    MonoMs lastSampleMs = 0;
    MonoMs lastDoseEndMs = -kRecentlyWateredMs;
    bool sessionMono = false;  // lastSessionMonoMs valid
    MonoMs lastSessionMonoMs = 0;
    MonoMs wetSinceMs = -1;
    MonoMs tempOutSinceMs = -1;
    MonoMs humOutSinceMs = -1;
    BlockReason lastLoggedBlock = BlockReason::None;
  };

  static void pushTrend(Channel& chn, float pct, MonoMs now);
  void rolloverDay(const Inputs& in);
  void handleInterlocks(const Inputs& in);
  void processMoisture(const Inputs& in);
  void processEnvironment(const Inputs& in);
  void processReservoir(const Inputs& in);
  void stepDose(const Inputs& in);
  void stepPlanners(const Inputs& in);
  void handleReport(int ch, const SessionReport& r, const Inputs& in);
  void startDose(int ch, float ml, SessionKind kind, bool openLoop = false);
  std::string manualDose(const std::string& plantId, float ml, bool timed);

  bool sensorUsable(int ch, const Inputs& in) const;
  float secondsSinceLastSession(int ch, const Inputs& in) const;
  float effectiveFlow(const PlantProfile& p) const;
  GlobalState globalState() const;

  void raise(AlertCode code, Severity sev, int ch, const std::string& msg);
  void clear(AlertCode code, int ch);
  void event(EventType type, int ch, const std::string& code, float ml, const std::string& detail);
  void persist();
  const PlantProfile* plantAt(int ch) const;
  std::string plantLabel(int ch) const;

  IOutlets& outlets_;
  IEngineSink& sink_;
  Settings settings_;
  std::vector<PlantProfile> plants_;
  std::array<Channel, kMaxPlants> ch_{};
  DoseController dose_;
  SessionKind doseKind_ = SessionKind::Auto;
  bool doseTimed_ = false;
  LastDose lastDose_;
  ReservoirModel reservoir_;
  AlertManager alerts_;
  Inputs last_;

  int32_t day_ = 0;
  MonoMs lastRolloverMs_ = 0;
  float mlTodayTotal_ = 0;
  bool emergencyStop_ = false;
  bool leakLatched_ = false;
  bool dispenseInProgress_ = false;
  uint8_t inProgressCh_ = 0;
  float inProgressMl_ = 0;
  MonoMs scaleBadSinceMs_ = -1;
  int rr_ = 0;  // round-robin start for fairness between plants
};

}  // namespace plantcore
