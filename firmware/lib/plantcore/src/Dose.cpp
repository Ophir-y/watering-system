#include "Dose.h"

#include <algorithm>

namespace plantcore {

const char* doseFaultName(DoseFault f) {
  switch (f) {
    case DoseFault::None: return "none";
    case DoseFault::Aborted: return "aborted";
    case DoseFault::PumpDry: return "pump_dry";
    case DoseFault::PumpDisconnected: return "pump_disconnected";
    case DoseFault::PumpJammed: return "pump_jammed";
  }
  return "unknown";
}

bool DoseController::start(const DoseRequest& req, MonoMs now) {
  if (state_ != DoseState::Idle || !(req.ml > 0)) return false;
  req_ = req;
  req_.flowMlPerS = std::max(req.flowMlPerS, 0.05f);
  fault_ = DoseFault::None;
  dispensedMl_ = 0;
  onTimeMs_ = 0;
  startMs_ = now;
  state_ = DoseState::Running;
  pump_.on();
  return true;
}

float DoseController::dispensedSoFar(MonoMs now) const {
  if (state_ != DoseState::Running) return dispensedMl_;
  return std::max<MonoMs>(0, now - startMs_) / 1000.0f * req_.flowMlPerS;
}

void DoseController::tick(MonoMs now) {
  if (state_ != DoseState::Running) return;
  const MonoMs runFor =
      std::min<MonoMs>(static_cast<MonoMs>(req_.ml / req_.flowMlPerS * 1000.0f), static_cast<MonoMs>(req_.maxOnMs));
  if (now - startMs_ < runFor) return;
  pump_.off();
  onTimeMs_ = static_cast<uint32_t>(now - startMs_);
  dispensedMl_ = onTimeMs_ / 1000.0f * req_.flowMlPerS;
  state_ = DoseState::Done;
}

void DoseController::fail(DoseFault f, MonoMs now, MonoMs flowedUntilMs) {
  pump_.off();
  if (state_ != DoseState::Running) return;
  onTimeMs_ = static_cast<uint32_t>(std::max<MonoMs>(0, now - startMs_));
  const MonoMs flowedMs = std::clamp<MonoMs>((flowedUntilMs >= 0 ? flowedUntilMs : now) - startMs_, 0, onTimeMs_);
  dispensedMl_ = flowedMs / 1000.0f * req_.flowMlPerS;
  fault_ = f;
  state_ = DoseState::Failed;
}

void DoseController::acknowledge() {
  if (finished()) state_ = DoseState::Idle;
}

}  // namespace plantcore
