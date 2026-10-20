// Reservoir level, refill detection, unexplained-loss detection and usage forecast.
#pragma once

#include "Types.h"

namespace plantcore {

struct ReservoirStatus {
  bool scaleValid = false;
  float ml = 0;
  float pct = 0;
  float daysLeft = -1;  // -1 = unknown
  bool low = false;
  bool empty = false;
  bool floatLow = false;
  bool disagree = false;  // float says empty, scale says plenty (one of them is wrong)
};

enum class ReservoirEventType : uint8_t { None, Refill, UnexplainedLoss };

struct ReservoirEvent {
  ReservoirEventType type = ReservoirEventType::None;
  float ml = 0;
};

constexpr float kRemovedBelowG = -30;        // container lifted off the scale
constexpr float kStableToleranceG = 4;
constexpr MonoMs kStableForMs = 10000;
constexpr float kRefillMinMl = 200;
constexpr float kLossMinMl = 60;             // sudden or accumulated loss that is not ours
constexpr MonoMs kLossConfirmMs = 30000;
constexpr MonoMs kLossWindowMs = 2LL * 3600 * 1000;

class ReservoirModel {
 public:
  void configure(const ReservoirConfig& cfg) { cfg_ = cfg; }

  // dosing: a dose is in progress, weight changes are expected.
  void update(MonoMs now, bool scaleOk, float grams, bool floatLow, bool dosing);
  // Called after a dose with the measured volume, so it is not mistaken for a leak.
  void accountDispensed(float ml);
  void onDayRollover(float mlDispensedThatDay);
  void setUsagePerDay(float ml) { usagePerDay_ = ml; }
  float usagePerDay() const { return usagePerDay_; }

  bool takeEvent(ReservoirEvent& out);
  ReservoirStatus status() const;

 private:
  void emit(ReservoirEventType t, float ml);

  ReservoirConfig cfg_;
  bool scaleOk_ = false;
  bool floatLow_ = false;
  float lastG_ = 0;
  bool haveStable_ = false;
  float stableG_ = 0;
  bool removed_ = false;
  float candidateG_ = 0;
  MonoMs candidateSince_ = 0;
  bool candidateCommitted_ = false;
  bool lossPending_ = false;
  MonoMs lossSince_ = 0;
  MonoMs lossWindowStart_ = 0;
  float lossAccum_ = 0;
  float usagePerDay_ = 0;
  bool pending_ = false;
  ReservoirEvent event_;
};

}  // namespace plantcore
