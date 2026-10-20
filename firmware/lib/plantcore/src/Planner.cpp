#include "Planner.h"

#include <algorithm>

namespace plantcore {

namespace {
constexpr float kLearnMinRisePct = 1.5f;
constexpr float kInitialPulseFrac = 0.05f;  // of pot volume, before anything is learned
constexpr float kTargetTolerancePct = 0.5f;
constexpr float kTriggerBelowTargetPct = 3.0f;
}  // namespace

float sessionTargetPct(const PlantProfile& p) {
  // Soak-and-dry plants (succulents, cacti) are watered thoroughly, then left to dry out.
  if (p.style == WateringStyle::SoakAndDry) return std::max(p.moisture.target, p.moisture.max - 3.0f);
  return p.moisture.target;
}

float triggerPct(const PlantProfile& p, const WateringAdjust& adj) {
  float t = p.moisture.min + adj.triggerOffsetPct;
  t = std::min(t, sessionTargetPct(p) - kTriggerBelowTargetPct);
  return std::max(t, p.moisture.critical);
}

float Planner::nextPulseMl(const PlantProfile& p, const PlantRuntime& rt, float pct,
                           const WateringAdjust& adj) const {
  float deficit = std::max(sessionTargetPct(p) - pct, 1.0f);
  float ml = rt.mlPerPct > 0 ? deficit * rt.mlPerPct * kLearnGain : p.potVolumeMl * kInitialPulseFrac;
  ml *= adj.doseFactor;
  float lo = std::min(kMinPulseMl, p.maxPulseMl);
  return std::clamp(ml, lo, p.maxPulseMl);
}

void Planner::tick(const PlantProfile& p, PlantRuntime& rt, const PlannerInput& in) {
  if (in.sensorUsable) lastPct_ = in.moisturePct;

  switch (state_) {
    case PlannerState::Idle: {
      if (!p.enabled || !p.autoWater) return;
      if (in.monoMs < retryAfter_) return;
      if (in.secondsSinceLastSession < p.minIntervalH * 3600.0f) return;
      if (rt.mlToday > p.maxDailyMl - kMinDoseMl) return;

      if (in.sensorUsable) {
        float pct = in.moisturePct;
        bool critical = pct < p.moisture.critical;
        if (pct < triggerPct(p, in.adjust) && (in.inWindow || critical)) {
          startSession(SessionKind::Auto, pct, nextPulseMl(p, rt, pct, in.adjust));
        }
      } else if (in.fallbackAllowed && p.onSensorFault == SensorFaultPolicy::FallbackSchedule && in.inWindow &&
                 in.secondsSinceLastFallback >= p.fallbackEveryDays * 86400.0f) {
        float ml = std::max(p.fallbackMl * in.adjust.doseFactor, kMinDoseMl);
        startSession(SessionKind::Fallback, lastPct_, ml);
      }
      return;
    }

    case PlannerState::WantPulse:
    case PlannerState::Pulsing:
      return;

    case PlannerState::Soaking: {
      if (in.monoMs < soakUntil_) return;
      if (!in.sensorUsable) {
        finish(SessionEnd::Aborted, lastPct_, in.monoMs, true);
        return;
      }
      // Judging the pulse while water is still reaching the sensor would under-read the
      // rise, over-estimate ml per %, and overshoot on the next pulse.
      if (in.trendPctPerMin > kStillRisingPctPerMin && in.monoMs < soakLimit_) return;
      float pct = in.moisturePct;
      float rise = pct - pctBeforePulse_;
      if (rise >= kLearnMinRisePct && lastPulseMl_ > 0) {
        float sample = std::clamp(lastPulseMl_ / rise, 0.5f, 200.0f);
        rt.mlPerPct = rt.mlPerPct > 0 ? 0.7f * rt.mlPerPct + 0.3f * sample : sample;
      }
      if (pct >= sessionTargetPct(p) - kTargetTolerancePct) {
        finish(SessionEnd::TargetReached, pct, in.monoMs, false);
        return;
      }
      if (pulses_ >= 2 && pct - startPct_ < kNoResponseRisePct) {
        finish(SessionEnd::NoResponse, pct, in.monoMs, true);
        return;
      }
      if (pulses_ >= kMaxPulsesPerSession || rt.mlToday > p.maxDailyMl - kMinDoseMl) {
        finish(SessionEnd::LimitReached, pct, in.monoMs, false);
        return;
      }
      pendingMl_ = nextPulseMl(p, rt, pct, in.adjust);
      pctBeforePulse_ = pct;
      state_ = PlannerState::WantPulse;
      return;
    }
  }
}

void Planner::startSession(SessionKind kind, float pct, float ml) {
  kind_ = kind;
  pulses_ = 0;
  sessionMl_ = 0;
  lastPulseMl_ = 0;
  startPct_ = pct;
  pctBeforePulse_ = pct;
  pendingMl_ = ml;
  state_ = PlannerState::WantPulse;
}

void Planner::onDoseStarted() {
  if (state_ == PlannerState::WantPulse) state_ = PlannerState::Pulsing;
}

void Planner::onDoseFinished(float dispensedMl, MonoMs now, float soakMin) {
  if (state_ != PlannerState::Pulsing) return;
  pulses_++;
  sessionMl_ += dispensedMl;
  lastPulseMl_ = dispensedMl;
  if (kind_ != SessionKind::Auto) {
    finish(SessionEnd::Single, lastPct_, now, false);
    return;
  }
  const MonoMs soakMs = static_cast<MonoMs>(std::clamp(soakMin, 2.0f, 60.0f) * 60000.0f);
  soakUntil_ = now + soakMs;
  soakLimit_ = soakUntil_ + kMaxSoakExtensions * soakMs;
  state_ = PlannerState::Soaking;
}

void Planner::onDoseFailed(MonoMs now) {
  if (state_ != PlannerState::Idle) finish(SessionEnd::DoseFailed, lastPct_, now, true);
}

void Planner::onBlocked(MonoMs now) {
  if (state_ != PlannerState::Idle) finish(SessionEnd::Blocked, lastPct_, now, true);
}

void Planner::abort(MonoMs now) {
  if (state_ != PlannerState::Idle) finish(SessionEnd::Aborted, lastPct_, now, true);
}

void Planner::finish(SessionEnd end, float pct, MonoMs now, bool backoff) {
  report_ = SessionReport{kind_, end, sessionMl_, pulses_, startPct_, pct};
  reportReady_ = true;
  state_ = PlannerState::Idle;
  pendingMl_ = 0;
  if (backoff) retryAfter_ = now + kRetryBackoffMs;
}

bool Planner::takeReport(SessionReport& out) {
  if (!reportReady_) return false;
  out = report_;
  reportReady_ = false;
  return true;
}

}  // namespace plantcore
