#include "Signal.h"

#include <algorithm>
#include <cstdlib>

namespace plantcore {

bool calibrationValid(const SensorCalibration& cal) {
  return std::abs(cal.rawDry - cal.rawWet) >= kMinCalibrationSpan;
}

float rawToPercent(int32_t raw, const SensorCalibration& cal) {
  if (!calibrationValid(cal)) return 0;
  float pct = 100.0f * static_cast<float>(raw - cal.rawDry) / static_cast<float>(cal.rawWet - cal.rawDry);
  return std::clamp(pct, 0.0f, 100.0f);
}

int32_t medianInPlace(int32_t* values, size_t n) {
  std::nth_element(values, values + n / 2, values + n);
  return values[n / 2];
}

float Ema::update(float x) {
  if (!primed_) {
    value_ = x;
    primed_ = true;
  } else {
    value_ += alpha_ * (x - value_);
  }
  return value_;
}

}  // namespace plantcore
