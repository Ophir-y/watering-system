// Per-channel moisture sensor fault detection.
#pragma once

#include "Types.h"

namespace plantcore {

struct SensorMonitorConfig {
  int32_t minValidRaw = 2000;    // ~0.25 V: input pulled to ground, sensor unplugged
  int32_t maxValidRaw = 26000;   // ~3.25 V: output shorted to supply
  int failuresToTrip = 3;        // consecutive bad samples before a hard fault
  float calMarginFrac = 0.25f;   // allowed overshoot beyond the calibration points
  MonoMs stuckWindowMs = 12LL * 3600 * 1000;
  int32_t stuckSpanRaw = 3;      // max-min below this over the window = frozen
  float erraticJumpPct = 25;
  int erraticJumps = 3;
  MonoMs erraticWindowMs = 3600LL * 1000;
  float noResponseRecoverPct = 5;  // natural rise that clears a no-response fault
};

class SensorMonitor {
 public:
  SensorMonitor() = default;
  explicit SensorMonitor(SensorMonitorConfig cfg) : cfg_(cfg) {}

  // Feed every fresh sample. recentlyWatered suppresses jump detection right after a dose.
  // Returns true if the sample can be used for control.
  bool onSample(const MoistureSample& s, const SensorCalibration& cal, MonoMs now, bool recentlyWatered);

  // Raised by the engine when watering produced no moisture rise.
  void reportNoResponse(float currentPct);
  void clearNoResponse();
  void reset();

  SensorHealth health() const;
  SensorIssue issue() const;
  float lastPct() const { return lastPct_; }

 private:
  SensorMonitorConfig cfg_;
  int busErrors_ = 0;
  int rangeErrors_ = 0;
  int calErrors_ = 0;
  bool uncalibrated_ = false;
  bool noResponse_ = false;
  float noResponseBaselinePct_ = 0;
  // stuck detection
  bool windowOpen_ = false;
  MonoMs windowStart_ = 0;
  int32_t windowMin_ = 0;
  int32_t windowMax_ = 0;
  bool stuck_ = false;
  int32_t stuckValue_ = 0;
  // erratic detection
  bool havePct_ = false;
  float lastPct_ = 0;
  int jumps_ = 0;
  MonoMs firstJumpAt_ = 0;
  MonoMs lastJumpAt_ = 0;
  bool erratic_ = false;
};

const char* sensorIssueName(SensorIssue issue);

}  // namespace plantcore
