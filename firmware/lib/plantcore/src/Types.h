// Core data types shared by the control engine, the JSON layer, the radio protocol
// and the firmware. Pure C++17: no Arduino headers, so everything here also builds
// for host tests.
//
// One plant node runs one Engine for one plant with its own bottle and pump. The
// main station keeps the profiles, settings, alerts and history for all nodes.
#pragma once

#include <cstdint>
#include <string>

namespace plantcore {

constexpr int kMaxNodes = 16;  // plants (nodes) one main station manages

using EpochS = int64_t;  // wall-clock unix seconds, 0 = unknown
using MonoMs = int64_t;  // monotonic milliseconds since boot

struct Range {
  float min = 0;
  float max = 0;
};

// ---------------------------------------------------------------- plants

enum class WateringStyle : uint8_t { EvenlyMoist, DryBetween, SoakAndDry };
enum class SensorFaultPolicy : uint8_t { Pause, FallbackSchedule };

// Moisture thresholds in calibrated percent (0 = sensor dry point, 100 = wet point).
struct MoistureBand {
  float critical = 20;  // below: plant in danger, water even outside the window
  float min = 35;       // trigger: start a watering session below this
  float target = 55;    // a session stops once this is reached
  float max = 75;       // above: too wet
};

// Raw readings at the two calibration points. Raw values are millivolts x 8 (the
// scale of the ADS1115 the first board used: 1 count = 0.125 mV). Capacitive sensors
// read higher when dry, but either direction is accepted.
struct SensorCalibration {
  int32_t rawDry = 19200;  // ~2.4 V
  int32_t rawWet = 9600;   // ~1.2 V
};

struct PumpConfig {
  float flowMlPerS = 1.5f;   // measured with the dashboard's "Measure flow" helper
  bool currentSense = true;  // 1 ohm shunt fitted: feel the pump run dry, unplugged or jammed
};

struct BottleConfig {
  float capacityMl = 1500;  // water you pour in to refill it from empty
  float lowPct = 25;        // "refill soon" below this level
  float reserveMl = 50;     // the gauge calls the bottle empty below this
};

struct PlantProfile {
  std::string id;
  std::string name;
  std::string species;
  bool enabled = true;
  bool autoWater = true;  // false: manual watering only
  std::string node;       // radio node (12 hex digits of its MAC); main station only
  PumpConfig pump;
  BottleConfig bottle;
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

// Learned / bookkeeping state of the plant that must survive reboots.
struct PlantRuntime {
  EpochS lastSessionEndS = 0;  // 0 = never
  EpochS lastFallbackS = 0;
  float mlToday = 0;
  float mlPerPct = 0;          // learned ml needed for +1 % moisture, 0 = unknown
  bool lockedOut = false;      // pump fault: no automatic or manual watering until cleared
};

// The bottle's fuel gauge: there is no level sensor, the node counts what it pumps.
struct BottleState {
  float ml = -1;                 // estimated water left, -1 = never set (assumed full)
  float pumpedSinceRefillMl = 0; // at the nominal pump flow, for flow self-calibration
  bool knownFull = false;        // the last refill was a confirmed fill to capacity
  float flowFactor = 1;          // learned correction of the pump's nominal flow
  float usagePerDayMl = 0;       // forecast for "days left"
  bool empty = false;
  uint8_t emptyReason = 0;       // EmptyReason
};

// What the pump's current has taught the node.
struct PumpLearned {
  float wetMaPerV = 0;  // normal current per battery volt while pumping water, 0 = unknown
  uint8_t runs = 0;     // healthy runs averaged into wetMaPerV
};

// ---------------------------------------------------------------- settings

struct SafetyLimits {
  float hardMaxPulseMl = 250;
  float hardMaxDailyMlPerPlant = 1500;
  uint32_t hardMaxOnTimeMs = 120000;
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
  std::string timezone = "UTC0";  // POSIX TZ string used by the main station
  std::string tzName = "UTC";     // IANA name shown in the dashboard
  bool southernHemisphere = false;
  uint8_t waterWindowStartH = 7;
  uint8_t waterWindowEndH = 21;
  uint16_t checkInMin = 5;        // how often each node wakes, reads its soil and reports
  uint16_t logIntervalMin = 10;
  bool automationEnabled = true;
  SafetyLimits limits;
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
  MoistureSample moisture;
  EnvSnapshot env;      // room climate, relayed by the main station
  float batteryMv = 0;  // 0 = unknown
  bool pumpSense = false;  // pumpMa holds a reading taken while the pump runs
  float pumpMa = 0;
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

enum class EmptyReason : uint8_t { None, Gauge, PumpDry, NoResponse };

enum class Severity : uint8_t { Info = 0, Warning = 1, Critical = 2 };

// Codes are numbered for the radio protocol: append new ones, never reorder.
enum class AlertCode : uint8_t {
  BottleLow,
  BottleEmpty,
  PumpDisconnected,
  PumpJammed,
  BatteryLow,
  NodeOffline,
  EmergencyStop,
  SensorFault,
  SensorNoResponse,
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
  int8_t channel = -1;  // node: 0 = the plant, -1 = the node itself; station: plant slot, -1 = system
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
  std::string code;    // short machine-readable tag, e.g. "auto", "bottle_low"
  float ml = 0;
  std::string detail;  // human-readable
  Severity severity = Severity::Info;  // AlertRaised only
};

// State the node persists across reboots (written at watering boundaries).
struct PersistedState {
  PlantRuntime plant;
  BottleState bottle;
  PumpLearned pump;
  int32_t day = 0;
  bool stopped = false;  // the main station's emergency stop, mirrored
  bool dispenseInProgress = false;
  float inProgressMl = 0;
};

}  // namespace plantcore
