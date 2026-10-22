#include "Safety.h"

#include <algorithm>

namespace plantcore {

const char* blockReasonName(BlockReason r) {
  switch (r) {
    case BlockReason::None: return "none";
    case BlockReason::InvalidAmount: return "invalid_amount";
    case BlockReason::EmergencyStop: return "emergency_stop";
    case BlockReason::BottleEmpty: return "bottle_empty";
    case BlockReason::BatteryLow: return "battery_low";
    case BlockReason::Lockout: return "lockout";
    case BlockReason::PlantDisabled: return "plant_disabled";
    case BlockReason::AutomationPaused: return "automation_paused";
    case BlockReason::ManualOnly: return "manual_only";
    case BlockReason::SensorFault: return "sensor_fault";
    case BlockReason::MinInterval: return "min_interval";
    case BlockReason::PlantDailyLimit: return "plant_daily_limit";
  }
  return "unknown";
}

namespace {
Verdict block(BlockReason r) { return Verdict{false, r, 0}; }
}  // namespace

Verdict evaluateDispense(const SafetyLimits& limits, const NodeState& node, const PlantProfile& plant,
                         const PlantRuntime& runtime, SensorHealth sensor, const DispenseRequest& req) {
  if (!(req.ml > 0)) return block(BlockReason::InvalidAmount);
  // Interlocks that apply to every kind of dose.
  if (node.emergencyStop) return block(BlockReason::EmergencyStop);
  if (node.bottleEmpty) return block(BlockReason::BottleEmpty);
  if (node.batteryCritical) return block(BlockReason::BatteryLow);
  if (runtime.lockedOut) return block(BlockReason::Lockout);
  if (!plant.enabled) return block(BlockReason::PlantDisabled);

  const bool manual = req.kind == SessionKind::Manual;
  if (!manual) {
    if (!node.automationEnabled) return block(BlockReason::AutomationPaused);
    if (!plant.autoWater) return block(BlockReason::ManualOnly);
    if (req.kind == SessionKind::Auto && sensor == SensorHealth::Failed) return block(BlockReason::SensorFault);
    if (!req.continuation && req.secondsSinceLastSession < plant.minIntervalH * 3600.0f) {
      return block(BlockReason::MinInterval);
    }
  }

  float pulseCap = limits.hardMaxPulseMl;
  float plantDailyCap = limits.hardMaxDailyMlPerPlant;
  if (!manual) {
    pulseCap = std::min(pulseCap, plant.maxPulseMl);
    plantDailyCap = std::min(plantDailyCap, plant.maxDailyMl);
  }

  float ml = std::min(req.ml, pulseCap);
  const float plantRemaining = plantDailyCap - runtime.mlToday;
  if (plantRemaining < kMinDoseMl) return block(BlockReason::PlantDailyLimit);
  ml = std::min(ml, plantRemaining);
  if (ml < kMinDoseMl) return block(BlockReason::InvalidAmount);
  return Verdict{true, BlockReason::None, ml};
}

uint32_t maxOnTimeMs(const SafetyLimits& limits, float ml, float flowMlPerS) {
  float flow = std::max(flowMlPerS, 0.1f);
  float ms = ml / flow * 1500.0f + 3000.0f;
  return static_cast<uint32_t>(std::min(ms, static_cast<float>(limits.hardMaxOnTimeMs)));
}

}  // namespace plantcore
