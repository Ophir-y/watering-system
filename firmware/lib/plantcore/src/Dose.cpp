#include "Dose.h"

#include <algorithm>

namespace plantcore {

const char* doseFaultName(DoseFault f) {
  switch (f) {
    case DoseFault::None: return "none";
    case DoseFault::NoFlow: return "no_flow";
    case DoseFault::StuckOpen: return "stuck_open";
    case DoseFault::ScaleLost: return "scale_lost";
    case DoseFault::Aborted: return "aborted";
  }
  return "unknown";
}

bool DoseController::start(const DoseRequest& req, MonoMs now, bool scaleOk, float grams) {
  if (state_ != DoseState::Idle || !(req.ml > 0)) return false;
  req_ = req;
  req_.closedLoop = req.closedLoop && scaleOk;
  fault_ = DoseFault::None;
  dispensedMl_ = 0;
  onTimeMs_ = 0;
  baselineG_ = grams;
  startMs_ = phaseMs_ = now;
  state_ = DoseState::Running;
  outlets_.open(req_.channel);
  return true;
}

void DoseController::close(MonoMs now) {
  outlets_.closeAll();
  onTimeMs_ = static_cast<uint32_t>(now - startMs_);
  phaseMs_ = now;
}

void DoseController::fail(DoseFault f, MonoMs now) {
  if (state_ == DoseState::Running) close(now);
  outlets_.closeAll();
  fault_ = f;
  state_ = DoseState::Failed;
}

void DoseController::tick(MonoMs now, bool scaleOk, float grams) {
  const MonoMs elapsed = now - startMs_;
  const float flow = std::max(req_.flowMlPerS, 0.05f);
  const float expectedMs = req_.ml / flow * 1000.0f;

  switch (state_) {
    case DoseState::Running: {
      if (!req_.closedLoop) {
        MonoMs runFor = std::min<MonoMs>(static_cast<MonoMs>(expectedMs), req_.maxOnMs);
        if (elapsed >= runFor) {
          close(now);
          dispensedMl_ = onTimeMs_ / 1000.0f * flow;
          state_ = DoseState::Done;
        }
        return;
      }
      if (!scaleOk) {
        // Can't see the water any more: stop and report an estimate.
        dispensedMl_ = std::min(req_.ml, elapsed / 1000.0f * flow);
        fail(DoseFault::ScaleLost, now);
        return;
      }
      dispensedMl_ = baselineG_ - grams;
      if (dispensedMl_ >= req_.ml - kInflightMl) {
        close(now);
        state_ = DoseState::Settling;
        return;
      }
      MonoMs noFlowCheck = std::clamp<MonoMs>(static_cast<MonoMs>(expectedMs * 0.6f), kMinNoFlowCheckMs,
                                              static_cast<MonoMs>(req_.maxOnMs));
      if (elapsed >= noFlowCheck && dispensedMl_ < std::max(3.0f, 0.15f * req_.ml)) {
        fail(DoseFault::NoFlow, now);
        return;
      }
      if (elapsed >= static_cast<MonoMs>(req_.maxOnMs)) {
        close(now);
        if (dispensedMl_ >= 0.25f * req_.ml) {
          state_ = DoseState::Settling;  // slow but flowing: keep the partial dose
        } else {
          fault_ = DoseFault::NoFlow;
          state_ = DoseState::Failed;
        }
      }
      return;
    }

    case DoseState::Settling:
      if (now - phaseMs_ < kSettleMs) return;
      if (!scaleOk) {
        state_ = DoseState::Done;  // keep the last in-flow measurement
        return;
      }
      closedG_ = grams;
      dispensedMl_ = std::max(0.0f, baselineG_ - grams);
      phaseMs_ = now;
      state_ = DoseState::Watching;
      return;

    case DoseState::Watching:
      if (scaleOk && closedG_ - grams > std::max(10.0f, 0.25f * req_.ml)) {
        dispensedMl_ = std::max(0.0f, baselineG_ - grams);
        fail(DoseFault::StuckOpen, now);
        return;
      }
      if (now - phaseMs_ >= kWatchMs) state_ = DoseState::Done;
      return;

    case DoseState::Idle:
    case DoseState::Done:
    case DoseState::Failed:
      return;
  }
}

void DoseController::abort(MonoMs now) {
  if (state_ == DoseState::Idle || finished()) {
    outlets_.closeAll();
    return;
  }
  if (state_ == DoseState::Running && !req_.closedLoop) {
    dispensedMl_ = (now - startMs_) / 1000.0f * std::max(req_.flowMlPerS, 0.05f);
  }
  fail(DoseFault::Aborted, now);
}

void DoseController::acknowledge() {
  if (finished()) state_ = DoseState::Idle;
}

}  // namespace plantcore
