// The plant's bottle, without a level sensor: a fuel gauge.
//
// The node counts the water it pumps. Pressing the button (or "Refilled" in the
// dashboard) resets the gauge to full. When the bottle really runs dry (the pump
// feels it, or watering stops making the soil wetter), the difference between what
// the gauge expected and the bottle's capacity corrects the learned pump flow, so
// the gauge gets more accurate with every refill.
#pragma once

#include "Types.h"

namespace plantcore {

struct BottleStatus {
  float ml = 0;
  float pct = 0;
  float capacityMl = 0;
  float daysLeft = -1;  // -1 = unknown
  bool low = false;
  bool empty = false;
  EmptyReason reason = EmptyReason::None;
  float flowFactor = 1;
};

constexpr float kLowDaysLeft = 2;             // "refill soon" when the forecast drops below this...
constexpr float kForecastLowBelowPct = 50;    // ...and the bottle is under half full (a full one can't be helped)
constexpr float kFlowLearnMinFrac = 0.3f;     // learn the flow only after pumping >= 30 % of the bottle
constexpr float kFlowFactorMin = 0.5f;
constexpr float kFlowFactorMax = 2.0f;
constexpr float kFlowLearnGain = 0.5f;        // weight of a new refill-cycle sample

const char* emptyReasonName(EmptyReason r);

class BottleModel {
 public:
  void configure(const BottleConfig& cfg);
  void restore(const BottleState& s);
  const BottleState& state() const { return s_; }

  // ml: estimate at the corrected flow. nominalMl: the same at the profile's flow.
  void dispensed(float ml, float nominalMl);
  // ml <= 0 means "filled to capacity".
  void refilled(float ml);
  // Returns true when the bottle was not already marked empty.
  bool markEmpty(EmptyReason r);
  void onDayRollover(float mlThatDay);

  float flowFactor() const { return s_.flowFactor; }
  bool belowReserve() const { return s_.ml < cfg_.reserveMl; }
  bool empty() const { return s_.empty; }
  BottleStatus status() const;

 private:
  BottleConfig cfg_;
  BottleState s_;
};

}  // namespace plantcore
