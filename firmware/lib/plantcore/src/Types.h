// Core data types shared by the control engine, the JSON layer and the firmware.
// Pure C++17: no Arduino headers, so everything here also builds for host tests.
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace plantcore {

constexpr int kMaxPlants = 8;

using EpochS = int64_t;  // wall-clock unix seconds, 0 = unknown
using MonoMs = int64_t;  // monotonic milliseconds since boot

struct Range {
  float min = 0;
  float max = 0;
};

// ---------------------------------------------------------------- plants

enum class OutletType : uint8_t { Pump, Valve };
enum class WateringStyle : uint8_t { EvenlyMoist, DryBetween, SoakAndDry };
enum class SensorFaultPolicy : uint8_t { Pause, FallbackSchedule };

// Moisture thresholds in calibrated percent (0 = sensor dry point, 100 = wet point).
struct MoistureBand {
  float critical = 20;  // below: plant in danger, water even outside the window
  float min = 35;       // trigger: start a watering session below this
  float target = 55;    // a session stops once this is reached
  float max = 75;       // above: too wet
};

// Raw ADS1115 counts at the two calibration points. Capacitive sensors read
// higher when dry, but either direction is accepted.
struct SensorCalibration {
  int32_t rawDry = 21000;
  int32_t rawWet = 10000;
};

struct OutletConfig {
  OutletType type = OutletType::Pump;
  float flowMlPerS = 2.0f;     // measured open-loop flow (valves: at calFillPct)
  float calFillPct = 100.0f;   // valve only: reservoir fill level when flow was measured
  float dropMm = 300.0f;       // valve only: height of reservoir bottom above the drip outlet
};

struct PlantProfile {
  std::string id;
  std::string name;
  std::string species;
  bool enabled = true;
  bool autoWater = true;  // false: manual watering only
  uint8_t channel = 0;    // sensor input and outlet index, 0..kMaxPlants-1
  OutletConfig outlet;
  float potVolumeMl = 2000;
  std::string soil;
  MoistureBand moisture;
  Range lightLux{2000, 20000};
  Range tempC{16, 29};
  Range humidityPct{40, 70};
  WateringStyle style = WateringStyle::EvenlyMoist;
  float minIntervalH = 8;
  float maxDailyMl = 600;
  float maxPulseMl = 120;
  float soakMin = 10;
  uint16_t dormantMonths = 0;  // bit 0 = January (northern-hemisphere months)
  float dormancyFactor = 0.7f; // dose multiplier while dormant
  SensorFaultPolicy onSensorFault = SensorFaultPolicy::Pause;
  float fallbackEveryDays = 4;
  float fallbackMl = 150;
  SensorCalibration cal;
  std::string careNotes;
};

// Per-channel learned / bookkeeping state that must survive reboots.
struct PlantRuntime {
  EpochS lastSessionEndS = 0;  // 0 = never
  EpochS lastFallbackS = 0;
  float mlToday = 0;
  float mlPerPct = 0;          // learned ml needed for +1 % moisture, 0 = unknown
  bool lockedOut = false;      // outlet fault: no automatic or manual watering until cleared
};

// ---------------------------------------------------------------- settings

struct SafetyLimits {
  float hardMaxPulseMl = 250;
  float hardMaxDailyMlPerPlant = 1500;
  float hardMaxDailyMlTotal = 5000;
  uint32_t hardMaxOnTimeMs = 120000;
};

struct ReservoirConfig {
  bool scaleEnabled = true;
  bool floatSwitchEnabled = true;
  float capacityMl = 5000;
  float fullHeightMm = 250;  // water depth when full (valve head compensation)
  float lowPct = 20;       // warning below this fill level
  float emptyMl = 150;     // treated as empty below this (keeps pump intakes submerged)
  float lowDaysLeft = 2;   // warning when the forecast drops below this
};

struct NotifyConfig {
  bool enabled = false;
  std::string server = "https://ntfy.sh";
  std::string topic;
  std::string token;      // optional access token for protected topics
  uint8_t minSeverity = 1;  // 0 info, 1 warning, 2 critical
};

struct Settings {
  std::string deviceName = "PlantCare";
  std::string timezone = "UTC0";  // POSIX TZ string used by the device
  std::string tzName = "UTC";     // IANA name shown in the dashboard
  bool southernHemisphere = false;
  uint8_t waterWindowStartH = 7;
  uint8_t waterWindowEndH = 21;
  uint8_t quietStartH = 22;       // buzzer silent from..to
  uint8_t quietEndH = 8;
  uint16_t sampleIntervalS = 60;
  uint16_t logIntervalMin = 10;
  bool automationEnabled = true;
  bool buzzerEnabled = true;
  bool leakSensorEnabled = true;
  SafetyLimits limits;
  ReservoirConfig reservoir;
  NotifyConfig notify;
};

// ---------------------------------------------------------------- inputs

struct MoistureSample {
  bool fresh = false;  // a new reading arrived this tick
  bool ok = false;     // the ADC read succeeded
  int32_t raw = 0;
};

struct EnvSnapshot {
  bool valid = false;
  float tempC = 0;
  float humidityPct = 0;
  bool luxValid = false;
  float lux = 0;
};

struct Inputs {
  MonoMs monoMs = 0;
  bool timeValid = false;
  EpochS now = 0;
  int localHour = 12;
  int localMonth = 1;   // 1..12
  int32_t localDay = 0; // days since epoch in local time, used for daily limits
  std::array<MoistureSample, kMaxPlants> moisture{};
  EnvSnapshot env;
  bool scaleOk = false;
  float scaleGrams = 0;   // net water weight (container tared out)
  bool floatLow = false;
  bool leak = false;
};

// ---------------------------------------------------------------- outputs

enum class SensorHealth : uint8_t { Ok, Suspect, Failed };
enum class SensorIssue : uint8_t {
  None, Uncalibrated, BusError, Disconnected, OutOfRange, NoResponse, Stuck, Erratic
};

enum class PlannerState : uint8_t { Idle, WantPulse, Pulsing, Soaking };

enum class SessionKind : uint8_t { Auto, Manual, Fallback };
enum class SessionEnd : uint8_t {
  None, TargetReached, LimitReached, NoResponse, Blocked, DoseFailed, Aborted, Single
};

enum class Severity : uint8_t { Info = 0, Warning = 1, Critical = 2 };

enum class AlertCode : uint8_t {
  ReservoirLow,
  ReservoirEmpty,
  ReservoirUnexplainedLoss,
  FloatScaleDisagree,
  ScaleFault,
  LeakDetected,
  EmergencyStop,
  SensorFault,
  SensorNoResponse,
  OutletNoFlow,
  OutletStuckOpen,
  PlantTooDry,
  PlantTooWet,
  TargetNotReached,
  DailyLimitReached,
  TempOutOfRange,
  HumidityOutOfRange,
  EnvSensorFault,
  InterruptedDispense,
  TimeNotSet,
  StorageFault,
  UnexpectedReset,
  CalibrationNeeded,
  Count
};

struct Alert {
  uint32_t id = 0;
  AlertCode code = AlertCode::Count;
  Severity severity = Severity::Info;
  int8_t channel = -1;  // -1 = system-wide
  bool active = false;
  bool acked = false;
  EpochS raisedAt = 0;
  EpochS clearedAt = 0;
  EpochS notifiedAt = 0;
  std::string message;
};

enum class EventType : uint8_t { Watering, AlertRaised, AlertCleared, Refill, System };

struct Event {
  EpochS ts = 0;
  EventType type = EventType::System;
  int8_t channel = -1;
  std::string code;    // short machine-readable tag, e.g. "auto", "reservoir_low"
  float ml = 0;
  std::string detail;  // human-readable
};

// State persisted across reboots (written at watering boundaries, not continuously).
struct PersistedState {
  std::array<PlantRuntime, kMaxPlants> plants{};
  int32_t day = 0;
  float mlTodayTotal = 0;
  float usagePerDayMl = 0;
  bool emergencyStop = false;
  bool dispenseInProgress = false;
  uint8_t inProgressChannel = 0;
  float inProgressMl = 0;
};

}  // namespace plantcore
