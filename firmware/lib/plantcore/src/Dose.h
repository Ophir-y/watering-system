// Runs a single dispense on one outlet.
//
// Closed loop (scale available): the outlet closes once the reservoir has lost the
// requested weight (1 g = 1 ml). After closing, the scale is watched for continued
// loss, which means a valve stuck open or a siphon.
// Open loop (no scale): the outlet stays open for ml / calibrated flow.
#pragma once

#include "Types.h"

namespace plantcore {

class IOutlets {
 public:
  virtual ~IOutlets() = default;
  virtual void open(uint8_t channel) = 0;  // energise actuator rail + this channel
  virtual void closeAll() = 0;             // de-energise everything
};

struct DoseRequest {
  uint8_t channel = 0;
  float ml = 0;
  float flowMlPerS = 1;  // expected flow (already head-compensated for valves)
  uint32_t maxOnMs = 30000;
  bool closedLoop = true;
};

enum class DoseState : uint8_t { Idle, Running, Settling, Watching, Done, Failed };
enum class DoseFault : uint8_t { None, NoFlow, StuckOpen, ScaleLost, Aborted };

const char* doseFaultName(DoseFault f);

constexpr float kInflightMl = 2.0f;          // water still in the tube when the outlet closes
constexpr MonoMs kSettleMs = 4000;
constexpr MonoMs kWatchMs = 20000;
constexpr MonoMs kMinNoFlowCheckMs = 4000;

class DoseController {
 public:
  explicit DoseController(IOutlets& outlets) : outlets_(outlets) {}

  bool start(const DoseRequest& req, MonoMs now, bool scaleOk, float grams);
  void tick(MonoMs now, bool scaleOk, float grams);
  void abort(MonoMs now);
  void acknowledge();  // Done/Failed -> Idle

  DoseState state() const { return state_; }
  DoseFault fault() const { return fault_; }
  bool idle() const { return state_ == DoseState::Idle; }
  bool finished() const { return state_ == DoseState::Done || state_ == DoseState::Failed; }
  bool outletOpen() const { return state_ == DoseState::Running; }
  float dispensedMl() const { return dispensedMl_; }
  uint32_t onTimeMs() const { return onTimeMs_; }
  const DoseRequest& request() const { return req_; }

 private:
  void close(MonoMs now);
  void fail(DoseFault f, MonoMs now);

  IOutlets& outlets_;
  DoseRequest req_;
  DoseState state_ = DoseState::Idle;
  DoseFault fault_ = DoseFault::None;
  MonoMs startMs_ = 0;
  MonoMs phaseMs_ = 0;
  float baselineG_ = 0;
  float closedG_ = 0;
  float dispensedMl_ = 0;
  uint32_t onTimeMs_ = 0;
};

}  // namespace plantcore
