#include "SensorMonitor.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "Signal.h"

namespace plantcore {

const char* sensorIssueName(SensorIssue issue) {
  switch (issue) {
    case SensorIssue::None: return "ok";
    case SensorIssue::Uncalibrated: return "uncalibrated";
    case SensorIssue::BusError: return "bus_error";
    case SensorIssue::Disconnected: return "disconnected";
    case SensorIssue::OutOfRange: return "out_of_range";
    case SensorIssue::NoResponse: return "no_response";
    case SensorIssue::Stuck: return "stuck";
    case SensorIssue::Erratic: return "erratic";
  }
  return "unknown";
}

void SensorMonitor::reset() { *this = SensorMonitor(cfg_); }

bool SensorMonitor::onSample(const MoistureSample& s, const SensorCalibration& cal, MonoMs now,
                             bool recentlyWatered) {
  uncalibrated_ = !calibrationValid(cal);
  if (!s.ok) {
    busErrors_++;
    return false;
  }
  busErrors_ = 0;

  if (s.raw < cfg_.minValidRaw || s.raw > cfg_.maxValidRaw) {
    rangeErrors_++;
    return false;
  }
  rangeErrors_ = 0;
  if (uncalibrated_) return false;

  int32_t lo = std::min(cal.rawDry, cal.rawWet);
  int32_t hi = std::max(cal.rawDry, cal.rawWet);
  int32_t margin = static_cast<int32_t>((hi - lo) * cfg_.calMarginFrac);
  if (s.raw < lo - margin || s.raw > hi + margin) {
    calErrors_++;
    return false;
  }
  calErrors_ = 0;

  // Frozen output: the reading never moves over a long window.
  if (stuck_ && std::abs(s.raw - stuckValue_) > cfg_.stuckSpanRaw) stuck_ = false;
  if (!windowOpen_) {
    windowOpen_ = true;
    windowStart_ = now;
    windowMin_ = windowMax_ = s.raw;
  } else {
    windowMin_ = std::min(windowMin_, s.raw);
    windowMax_ = std::max(windowMax_, s.raw);
    if (now - windowStart_ >= cfg_.stuckWindowMs) {
      if (windowMax_ - windowMin_ < cfg_.stuckSpanRaw) {
        stuck_ = true;
        stuckValue_ = s.raw;
      }
      windowStart_ = now;
      windowMin_ = windowMax_ = s.raw;
    }
  }

  float pct = rawToPercent(s.raw, cal);
  if (havePct_ && !recentlyWatered && std::fabs(pct - lastPct_) > cfg_.erraticJumpPct) {
    if (jumps_ == 0 || now - firstJumpAt_ > cfg_.erraticWindowMs) {
      jumps_ = 0;
      firstJumpAt_ = now;
    }
    jumps_++;
    lastJumpAt_ = now;
    if (jumps_ >= cfg_.erraticJumps) erratic_ = true;
  }
  if (erratic_ && now - lastJumpAt_ > 6 * cfg_.erraticWindowMs) {
    erratic_ = false;
    jumps_ = 0;
  }

  if (noResponse_ && pct >= noResponseBaselinePct_ + cfg_.noResponseRecoverPct) noResponse_ = false;

  havePct_ = true;
  lastPct_ = pct;
  return true;
}

void SensorMonitor::reportNoResponse(float currentPct) {
  noResponse_ = true;
  noResponseBaselinePct_ = currentPct;
}

void SensorMonitor::clearNoResponse() { noResponse_ = false; }

SensorIssue SensorMonitor::issue() const {
  if (uncalibrated_) return SensorIssue::Uncalibrated;
  if (busErrors_ >= cfg_.failuresToTrip) return SensorIssue::BusError;
  if (rangeErrors_ >= cfg_.failuresToTrip) return SensorIssue::Disconnected;
  if (calErrors_ >= cfg_.failuresToTrip) return SensorIssue::OutOfRange;
  if (noResponse_) return SensorIssue::NoResponse;
  if (stuck_) return SensorIssue::Stuck;
  if (erratic_) return SensorIssue::Erratic;
  return SensorIssue::None;
}

SensorHealth SensorMonitor::health() const {
  switch (issue()) {
    case SensorIssue::None: return SensorHealth::Ok;
    case SensorIssue::Stuck:
    case SensorIssue::Erratic: return SensorHealth::Suspect;
    default: return SensorHealth::Failed;
  }
}

}  // namespace plantcore
