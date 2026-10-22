// Runs one dose on the node's pump: on for ml / flow seconds, then off.
//
// There is no scale: the volume is time x the pump's flow (corrected by what the
// bottle gauge has learned). The engine stops a dose early when the pump monitor
// feels the pump run dry, unplugged or jammed.
#pragma once

#include "Types.h"

namespace plantcore {

class IPump {
 public:
  virtual ~IPump() = default;
  virtual void on() = 0;
  virtual void off() = 0;
};

struct DoseRequest {
  float ml = 0;
  float flowMlPerS = 1;  // expected flow, already corrected by the bottle gauge
  uint32_t maxOnMs = 30000;
};

enum class DoseState : uint8_t { Idle, Running, Done, Failed };
enum class DoseFault : uint8_t { None, Aborted, PumpDry, PumpDisconnected, PumpJammed };

const char* doseFaultName(DoseFault f);

class DoseController {
 public:
  explicit DoseController(IPump& pump) : pump_(pump) {}

  bool start(const DoseRequest& req, MonoMs now);
  void tick(MonoMs now);
  // Stop with a fault. Water is counted until flowedUntilMs (default: now).
  void fail(DoseFault f, MonoMs now, MonoMs flowedUntilMs = -1);
  void abort(MonoMs now) { fail(DoseFault::Aborted, now); }
  void acknowledge();  // Done/Failed -> Idle

  DoseState state() const { return state_; }
  DoseFault fault() const { return fault_; }
  bool idle() const { return state_ == DoseState::Idle; }
  bool running() const { return state_ == DoseState::Running; }
  bool finished() const { return state_ == DoseState::Done || state_ == DoseState::Failed; }
  float dispensedMl() const { return dispensedMl_; }
  // So far, while running.
  float dispensedSoFar(MonoMs now) const;
  uint32_t onTimeMs() const { return onTimeMs_; }
  const DoseRequest& request() const { return req_; }

 private:
  IPump& pump_;
  DoseRequest req_;
  DoseState state_ = DoseState::Idle;
  DoseFault fault_ = DoseFault::None;
  MonoMs startMs_ = 0;
  float dispensedMl_ = 0;
  uint32_t onTimeMs_ = 0;
};

}  // namespace plantcore
