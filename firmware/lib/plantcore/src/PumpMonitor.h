// Feels the pump through its current (a 1 ohm shunt on the node).
//
// A submersible pump pushing water draws noticeably more current than one sucking
// air. The monitor learns the normal "wet" current from healthy runs, normalised by
// the battery voltage so draining cells are not mistaken for a dry pump, and then
// judges every run:
//   Dry           well below the wet current: the bottle ran out
//   Disconnected  almost no current at all: pump unplugged or a broken wire
//   Jammed        far above the wet current: the impeller is blocked
// Disconnected needs nothing learned; Dry and Jammed wait for kLearnRuns healthy runs.
#pragma once

#include "Types.h"

namespace plantcore {

enum class PumpVerdict : uint8_t { Ok, Dry, Disconnected, Jammed };

const char* pumpVerdictName(PumpVerdict v);

constexpr MonoMs kPumpInrushMs = 400;      // ignore the start-up surge
constexpr MonoMs kPumpDryForMs = 1500;
constexpr MonoMs kPumpOpenForMs = 1000;
constexpr MonoMs kPumpJamForMs = 1000;
constexpr float kPumpDryFrac = 0.65f;      // below this fraction of the wet current = dry
constexpr float kPumpJamFrac = 2.0f;       // above this multiple = jammed
constexpr float kPumpOpenMa = 20;          // below this = nothing connected
constexpr uint8_t kLearnRuns = 3;
constexpr MonoMs kLearnMinSteadyMs = 1000; // a run must give this much steady data to learn from
constexpr float kAssumedBatteryV = 4.5f;   // when the battery voltage is unknown

class PumpMonitor {
 public:
  void configure(bool enabled) { enabled_ = enabled; }
  void restore(const PumpLearned& l) { learned_ = l; }
  const PumpLearned& learned() const { return learned_; }
  bool enabled() const { return enabled_; }
  // Dry and Jammed can be detected.
  bool ready() const { return enabled_ && learned_.runs >= kLearnRuns && learned_.wetMaPerV > 0; }

  void start(MonoMs now);
  // Feed every reading taken while the pump runs; returns the verdict so far.
  PumpVerdict sample(MonoMs now, float mA, float batteryMv);
  // The run ended. learn: the caller believes the pump was moving water.
  void finish(bool learn);

  PumpVerdict verdict() const { return verdict_; }
  // When the current first dropped to "dry" (valid after a Dry verdict).
  MonoMs drySinceMs() const { return drySince_; }
  float lastRunMaPerV() const { return steadyN_ > 0 ? static_cast<float>(steadySum_ / steadyN_) : 0; }
  bool running() const { return running_; }

 private:
  bool enabled_ = true;
  PumpLearned learned_;
  bool running_ = false;
  MonoMs startMs_ = 0;
  MonoMs firstSteadyMs_ = -1;
  MonoMs lastSteadyMs_ = -1;
  double steadySum_ = 0;
  uint32_t steadyN_ = 0;
  MonoMs drySince_ = -1;
  MonoMs openSince_ = -1;
  MonoMs jamSince_ = -1;
  PumpVerdict verdict_ = PumpVerdict::Ok;
};

}  // namespace plantcore
