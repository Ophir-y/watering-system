// Hard safety rules every dose must pass, automatic or manual.
#pragma once

#include "Types.h"

namespace plantcore {

enum class BlockReason : uint8_t {
  None,
  InvalidAmount,
  EmergencyStop,
  BottleEmpty,
  BatteryLow,
  Lockout,
  PlantDisabled,
  AutomationPaused,
  ManualOnly,
  SensorFault,
  MinInterval,
  PlantDailyLimit,
};

const char* blockReasonName(BlockReason r);

struct NodeState {
  bool automationEnabled = true;
  bool emergencyStop = false;
  bool bottleEmpty = false;
  bool batteryCritical = false;
};

struct DispenseRequest {
  SessionKind kind = SessionKind::Auto;
  bool continuation = false;          // next pulse of a session already in progress
  float ml = 0;
  float secondsSinceLastSession = 1e9f;
};

struct Verdict {
  bool allowed = false;
  BlockReason reason = BlockReason::None;
  float ml = 0;  // possibly reduced to fit the limits
};

// Smallest dose worth starting the pump for.
constexpr float kMinDoseMl = 5.0f;

// Battery thresholds for 3xAA (no load): warn, then refuse to pump.
constexpr float kBatteryLowMv = 3600;
constexpr float kBatteryCriticalMv = 3300;

Verdict evaluateDispense(const SafetyLimits& limits, const NodeState& node, const PlantProfile& plant,
                         const PlantRuntime& runtime, SensorHealth sensor, const DispenseRequest& req);

// Longest the pump may run for this dose: 1.5x the expected time plus margin,
// never more than the hard cap.
uint32_t maxOnTimeMs(const SafetyLimits& limits, float ml, float flowMlPerS);

}  // namespace plantcore
