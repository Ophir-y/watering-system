// Per-plant pulse-and-soak watering state machine.
//
//   Idle --(moisture < trigger)--> WantPulse --(dose starts)--> Pulsing
//     ^                               ^                            |
//     |                               +--(below target)-- Soaking <+ (dose done)
//     +----------(target reached / limits / no response)-----+
//
// The planner only decides; the engine checks safety and drives the pump.
#pragma once

#include "EnvModel.h"
#include "Safety.h"
#include "Types.h"

namespace plantcore {

struct PlannerInput {
  MonoMs monoMs = 0;
  bool sensorUsable = false;  // fresh-enough reading from a non-failed sensor
  bool fallbackAllowed = true;  // false when the fault suggests water isn't reaching the pot
  float moisturePct = 0;      // smoothed
  float trendPctPerMin = 0;   // recent rate of change of the smoothed moisture
  WateringAdjust adjust;
  bool inWindow = true;
  float secondsSinceLastSession = 1e9f;
  float secondsSinceLastFallback = 1e9f;
};

struct SessionReport {
  SessionKind kind = SessionKind::Auto;
  SessionEnd end = SessionEnd::None;
  float ml = 0;
  int pulses = 0;
  float startPct = 0;
  float endPct = 0;
};

constexpr int kMaxPulsesPerSession = 6;
constexpr float kMinPulseMl = 20.0f;
constexpr float kLearnGain = 0.7f;           // undershoot factor on learned dose
constexpr float kNoResponseRisePct = 1.0f;   // less than this after 2+ pulses = no response
constexpr MonoMs kRetryBackoffMs = 30LL * 60 * 1000;
constexpr float kStillRisingPctPerMin = 0.15f;  // keep soaking while water is still arriving
constexpr int kMaxSoakExtensions = 2;           // ...for at most this many extra soak periods

float sessionTargetPct(const PlantProfile& p);
float triggerPct(const PlantProfile& p, const WateringAdjust& adj);

class Planner {
 public:
  void tick(const PlantProfile& p, PlantRuntime& rt, const PlannerInput& in);

  PlannerState state() const { return state_; }
  SessionKind kind() const { return kind_; }
  float pendingMl() const { return pendingMl_; }
  bool continuation() const { return pulses_ > 0; }
  int pulses() const { return pulses_; }
  MonoMs soakUntil() const { return soakUntil_; }

  void onDoseStarted();
  void onDoseFinished(float dispensedMl, MonoMs now, float soakMin);
  void onDoseFailed(MonoMs now);
  void onBlocked(MonoMs now);
  void abort(MonoMs now);

  // Returns true once per finished session.
  bool takeReport(SessionReport& out);

 private:
  void startSession(SessionKind kind, float pct, float ml);
  void finish(SessionEnd end, float pct, MonoMs now, bool backoff);
  float nextPulseMl(const PlantProfile& p, const PlantRuntime& rt, float pct, const WateringAdjust& adj) const;

  PlannerState state_ = PlannerState::Idle;
  SessionKind kind_ = SessionKind::Auto;
  float pendingMl_ = 0;
  int pulses_ = 0;
  float sessionMl_ = 0;
  float lastPulseMl_ = 0;
  float startPct_ = 0;
  float pctBeforePulse_ = 0;
  float lastPct_ = 0;
  MonoMs soakUntil_ = 0;
  MonoMs soakLimit_ = 0;
  MonoMs retryAfter_ = 0;
  bool reportReady_ = false;
  SessionReport report_;
};

}  // namespace plantcore
