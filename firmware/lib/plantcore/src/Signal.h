// Sensor signal helpers: calibration, median filtering and exponential smoothing.
#pragma once

#include <cstddef>
#include <cstdint>

#include "Types.h"

namespace plantcore {

// Minimum distance between the dry and wet calibration points, in raw counts.
constexpr int32_t kMinCalibrationSpan = 1500;

bool calibrationValid(const SensorCalibration& cal);

// Maps a raw reading to 0..100 % using the dry/wet points (clamped).
float rawToPercent(int32_t raw, const SensorCalibration& cal);

// Median of n values; reorders the buffer in place. n must be > 0.
int32_t medianInPlace(int32_t* values, size_t n);

class Ema {
 public:
  explicit Ema(float alpha = 0.3f) : alpha_(alpha) {}
  float update(float x);
  void reset() { primed_ = false; }
  bool primed() const { return primed_; }
  float value() const { return value_; }

 private:
  float alpha_;
  bool primed_ = false;
  float value_ = 0;
};

}  // namespace plantcore
