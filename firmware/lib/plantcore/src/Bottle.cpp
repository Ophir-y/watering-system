#include "Bottle.h"

#include <algorithm>

namespace plantcore {

const char* emptyReasonName(EmptyReason r) {
  switch (r) {
    case EmptyReason::None: return "none";
    case EmptyReason::Gauge: return "gauge";
    case EmptyReason::PumpDry: return "pump_dry";
    case EmptyReason::NoResponse: return "no_response";
  }
  return "none";
}

void BottleModel::configure(const BottleConfig& cfg) {
  cfg_ = cfg;
  if (s_.ml < 0) s_.ml = cfg_.capacityMl;  // never set: assume it was filled when installed
  s_.ml = std::min(s_.ml, cfg_.capacityMl);
}

void BottleModel::restore(const BottleState& s) {
  s_ = s;
  s_.flowFactor = std::clamp(s_.flowFactor > 0 ? s_.flowFactor : 1.0f, kFlowFactorMin, kFlowFactorMax);
  configure(cfg_);
}

void BottleModel::dispensed(float ml, float nominalMl) {
  if (!(ml > 0)) return;
  s_.ml = std::max(0.0f, s_.ml - ml);
  s_.pumpedSinceRefillMl += std::max(0.0f, nominalMl);
}

void BottleModel::refilled(float ml) {
  if (!(ml > 0) || ml >= cfg_.capacityMl) {
    s_.ml = cfg_.capacityMl;
    s_.knownFull = true;
    s_.pumpedSinceRefillMl = 0;
  } else {
    // A top-up of unknown starting level: the cycle can no longer teach the flow.
    s_.ml = std::min(cfg_.capacityMl, std::max(0.0f, s_.ml) + ml);
    s_.knownFull = false;
  }
  s_.empty = false;
  s_.emptyReason = static_cast<uint8_t>(EmptyReason::None);
}

bool BottleModel::markEmpty(EmptyReason r) {
  // Only the pump running dry is a reliable "empty right now". From a confirmed full
  // bottle it means capacityMl actually came out while the gauge counted
  // pumpedSinceRefillMl at the nominal flow: the real flow is their ratio.
  if (r == EmptyReason::PumpDry && s_.knownFull && s_.pumpedSinceRefillMl >= kFlowLearnMinFrac * cfg_.capacityMl) {
    const float sample = std::clamp(cfg_.capacityMl / s_.pumpedSinceRefillMl, kFlowFactorMin, kFlowFactorMax);
    s_.flowFactor = std::clamp((1 - kFlowLearnGain) * s_.flowFactor + kFlowLearnGain * sample, kFlowFactorMin,
                               kFlowFactorMax);
  }
  s_.knownFull = false;
  s_.ml = 0;
  const bool fresh = !s_.empty;
  s_.empty = true;
  s_.emptyReason = static_cast<uint8_t>(r);
  return fresh;
}

void BottleModel::onDayRollover(float mlThatDay) {
  if (s_.usagePerDayMl > 0) {
    s_.usagePerDayMl = 0.7f * s_.usagePerDayMl + 0.3f * mlThatDay;
  } else if (mlThatDay > 0) {
    s_.usagePerDayMl = mlThatDay;
  }
}

BottleStatus BottleModel::status() const {
  BottleStatus st;
  st.capacityMl = cfg_.capacityMl;
  st.ml = std::max(0.0f, s_.ml);
  st.pct = cfg_.capacityMl > 0 ? std::clamp(st.ml / cfg_.capacityMl * 100.0f, 0.0f, 100.0f) : 0;
  if (s_.usagePerDayMl > 1) st.daysLeft = st.ml / s_.usagePerDayMl;
  st.empty = s_.empty;
  st.reason = static_cast<EmptyReason>(s_.emptyReason);
  st.low = !st.empty && (st.pct < cfg_.lowPct ||
                         (st.daysLeft >= 0 && st.daysLeft < kLowDaysLeft && st.pct < kForecastLowBelowPct));
  st.flowFactor = s_.flowFactor;
  return st;
}

}  // namespace plantcore
