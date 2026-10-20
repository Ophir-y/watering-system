#include "EnvModel.h"

#include <algorithm>
#include <cmath>

namespace plantcore {

namespace {
constexpr float kNeutralVpdKPa = 1.0f;   // typical indoor conditions: no adjustment
constexpr float kOffsetPerKPa = 2.5f;
constexpr float kMaxOffsetUp = 5.0f;
constexpr float kMaxOffsetDown = -2.0f;
constexpr float kBrightLightOffset = 1.0f;
constexpr float kColdDoseFactor = 0.5f;
constexpr float kDormantTriggerDropPerFactor = 10.0f;
}  // namespace

float saturationVaporPressureKPa(float tempC) {
  return 0.6108f * std::exp(17.27f * tempC / (tempC + 237.3f));
}

float vpdKPa(float tempC, float humidityPct) {
  float rh = std::clamp(humidityPct, 0.0f, 100.0f);
  return saturationVaporPressureKPa(tempC) * (1.0f - rh / 100.0f);
}

bool isDormantMonth(const PlantProfile& p, int month, bool southern) {
  if (month < 1 || month > 12) return false;
  int m = month - 1;
  if (southern) m = (m + 6) % 12;
  return (p.dormantMonths >> m) & 1u;
}

WateringAdjust computeAdjust(const PlantProfile& p, const EnvSnapshot& env, int month, bool southern) {
  WateringAdjust adj;
  if (env.valid) {
    float vpd = vpdKPa(env.tempC, env.humidityPct);
    adj.triggerOffsetPct = std::clamp(kOffsetPerKPa * (vpd - kNeutralVpdKPa), kMaxOffsetDown, kMaxOffsetUp);
    if (env.tempC < p.tempC.min) {
      adj.tooCold = true;
      adj.doseFactor *= kColdDoseFactor;
      adj.triggerOffsetPct = std::min(adj.triggerOffsetPct, 0.0f);
    }
  }
  if (env.luxValid && env.lux > p.lightLux.max) {
    adj.triggerOffsetPct = std::min(adj.triggerOffsetPct + kBrightLightOffset, kMaxOffsetUp);
  }
  if (isDormantMonth(p, month, southern)) {
    adj.dormant = true;
    float factor = std::clamp(p.dormancyFactor, 0.1f, 1.0f);
    adj.doseFactor *= factor;
    adj.triggerOffsetPct -= (1.0f - factor) * kDormantTriggerDropPerFactor;
  }
  return adj;
}

}  // namespace plantcore
