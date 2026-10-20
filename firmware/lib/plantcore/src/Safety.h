// Hard safety rules every dispense must pass, automatic or manual.
#pragma once

#include "Types.h"

namespace plantcore {

enum class BlockReason : uint8_t {
  None,
  InvalidAmount,
  Leak,
  EmergencyStop,
  ReservoirEmpty,
  OutletBusy,
  Lockout,
  PlantDisabled,
  AutomationPaused,
  ManualOnly,
  SensorFault,
  MinInterval,
  PlantDailyLimit,
  TotalDailyLimit,
};

const char* blockReasonName(BlockReason r);

struct GlobalState {
  bool automationEnabled = true;
  bool emergencyStop = false;
  bool leak = false;
  bool reservoirEmpty = false;
  bool outletActive = false;
  float mlTodayTotal = 0;
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

// Smallest dose worth opening an outlet for.
constexpr float kMinDoseMl = 5.0f;

Verdict evaluateDispense(const SafetyLimits& limits, const GlobalState& global, const PlantProfile& plant,
                         const PlantRuntime& runtime, SensorHealth sensor, const DispenseRequest& req);

// Longest an outlet may stay open for this dose: 1.5x the expected time plus margin,
// never more than the hard cap.
uint32_t maxOnTimeMs(const SafetyLimits& limits, float ml, float flowMlPerS);

}  // namespace plantcore
