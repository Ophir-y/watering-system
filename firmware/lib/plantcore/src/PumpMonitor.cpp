#include "PumpMonitor.h"

namespace plantcore {

const char* pumpVerdictName(PumpVerdict v) {
  switch (v) {
    case PumpVerdict::Ok: return "ok";
    case PumpVerdict::Dry: return "dry";
    case PumpVerdict::Disconnected: return "disconnected";
    case PumpVerdict::Jammed: return "jammed";
  }
  return "ok";
}

void PumpMonitor::start(MonoMs now) {
  running_ = true;
  startMs_ = now;
  firstSteadyMs_ = lastSteadyMs_ = -1;
  steadySum_ = 0;
  steadyN_ = 0;
  drySince_ = openSince_ = jamSince_ = -1;
  verdict_ = PumpVerdict::Ok;
}

PumpVerdict PumpMonitor::sample(MonoMs now, float mA, float batteryMv) {
  if (!running_ || !enabled_ || verdict_ != PumpVerdict::Ok) return verdict_;
  if (now - startMs_ < kPumpInrushMs) return verdict_;

  const float volts = batteryMv > 1000 ? batteryMv / 1000.0f : kAssumedBatteryV;
  const float g = mA / volts;

  // Nothing connected: no learning needed.
  if (mA < kPumpOpenMa) {
    if (openSince_ < 0) openSince_ = now;
    if (now - openSince_ >= kPumpOpenForMs) verdict_ = PumpVerdict::Disconnected;
    return verdict_;
  }
  openSince_ = -1;

  if (ready()) {
    const float wet = learned_.wetMaPerV;
    if (g < kPumpDryFrac * wet) {
      if (drySince_ < 0) drySince_ = now;
      if (now - drySince_ >= kPumpDryForMs) verdict_ = PumpVerdict::Dry;
    } else {
      drySince_ = -1;
    }
    if (g > kPumpJamFrac * wet) {
      if (jamSince_ < 0) jamSince_ = now;
      if (now - jamSince_ >= kPumpJamForMs) verdict_ = PumpVerdict::Jammed;
    } else {
      jamSince_ = -1;
    }
    // Only readings that look like water flowing describe the wet current.
    if (drySince_ >= 0 || jamSince_ >= 0) return verdict_;
  }

  if (firstSteadyMs_ < 0) firstSteadyMs_ = now;
  lastSteadyMs_ = now;
  steadySum_ += g;
  steadyN_++;
  return verdict_;
}

void PumpMonitor::finish(bool learn) {
  if (!running_) return;
  running_ = false;
  if (!learn || !enabled_ || verdict_ != PumpVerdict::Ok || steadyN_ == 0) return;
  if (lastSteadyMs_ - firstSteadyMs_ < kLearnMinSteadyMs) return;
  const float run = static_cast<float>(steadySum_ / steadyN_);
  learned_.wetMaPerV = learned_.runs == 0 ? run : 0.8f * learned_.wetMaPerV + 0.2f * run;
  if (learned_.runs < 255) learned_.runs++;
}

}  // namespace plantcore
