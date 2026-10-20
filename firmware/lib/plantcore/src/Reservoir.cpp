#include "Reservoir.h"

#include <algorithm>
#include <cmath>

namespace plantcore {

void ReservoirModel::emit(ReservoirEventType t, float ml) {
  event_ = ReservoirEvent{t, ml};
  pending_ = true;
}

bool ReservoirModel::takeEvent(ReservoirEvent& out) {
  if (!pending_) return false;
  out = event_;
  pending_ = false;
  return true;
}

void ReservoirModel::update(MonoMs now, bool scaleOk, float grams, bool floatLow, bool dosing) {
  floatLow_ = floatLow;
  scaleOk_ = scaleOk && cfg_.scaleEnabled;

  auto restartCandidate = [&](float g) {
    candidateG_ = g;
    candidateSince_ = now;
    candidateCommitted_ = false;
  };

  if (!scaleOk_) {
    restartCandidate(lastG_);
    lossPending_ = false;
    return;
  }
  lastG_ = grams;

  if (grams < kRemovedBelowG) {
    removed_ = true;
    lossPending_ = false;
    restartCandidate(grams);
    return;
  }
  if (dosing) {
    lossPending_ = false;
    restartCandidate(grams);
    return;
  }

  // Fast loss: clearly below the baseline for a while with no dose running.
  if (haveStable_ && !removed_ && stableG_ - grams > kLossMinMl) {
    if (!lossPending_) {
      lossPending_ = true;
      lossSince_ = now;
    } else if (now - lossSince_ >= kLossConfirmMs) {
      emit(ReservoirEventType::UnexplainedLoss, stableG_ - grams);
      stableG_ = grams;
      lossPending_ = false;
      restartCandidate(grams);
      return;
    }
  } else {
    lossPending_ = false;
  }

  // Track a stable reading to re-baseline, spot refills and accumulate slow losses.
  if (std::fabs(grams - candidateG_) > kStableToleranceG) {
    restartCandidate(grams);
    return;
  }
  if (candidateCommitted_ || now - candidateSince_ < kStableForMs) return;
  candidateCommitted_ = true;

  const float g = candidateG_;
  if (!haveStable_) {
    haveStable_ = true;
    removed_ = false;
    stableG_ = g;
    return;
  }
  const float delta = g - stableG_;
  stableG_ = g;
  if (removed_) {
    // Container was lifted off and put back: compare with the weight before.
    removed_ = false;
    lossAccum_ = 0;
    if (delta >= kRefillMinMl) emit(ReservoirEventType::Refill, delta);
    return;
  }
  if (delta >= kRefillMinMl) {
    emit(ReservoirEventType::Refill, delta);
    lossAccum_ = 0;
  } else if (delta < 0) {
    if (lossAccum_ <= 0 || now - lossWindowStart_ > kLossWindowMs) {
      lossWindowStart_ = now;
      lossAccum_ = 0;
    }
    lossAccum_ += -delta;
    if (lossAccum_ >= kLossMinMl) {
      emit(ReservoirEventType::UnexplainedLoss, lossAccum_);
      lossAccum_ = 0;
    }
  }
}

void ReservoirModel::accountDispensed(float ml) {
  if (haveStable_) stableG_ -= ml;
}

void ReservoirModel::onDayRollover(float mlDispensedThatDay) {
  if (usagePerDay_ > 0) {
    usagePerDay_ = 0.7f * usagePerDay_ + 0.3f * mlDispensedThatDay;
  } else if (mlDispensedThatDay > 0) {
    usagePerDay_ = mlDispensedThatDay;
  }
}

ReservoirStatus ReservoirModel::status() const {
  ReservoirStatus s;
  s.floatLow = floatLow_;
  s.scaleValid = scaleOk_ && !removed_;
  if (s.scaleValid) {
    s.ml = std::max(0.0f, lastG_);
    s.pct = cfg_.capacityMl > 0 ? std::clamp(s.ml / cfg_.capacityMl * 100.0f, 0.0f, 100.0f) : 0;
    if (usagePerDay_ > 1) s.daysLeft = s.ml / usagePerDay_;
    s.low = s.pct < cfg_.lowPct || (s.daysLeft >= 0 && s.daysLeft < cfg_.lowDaysLeft);
  }
  s.empty = floatLow_ || (scaleOk_ && removed_) || (s.scaleValid && s.ml < cfg_.emptyMl);
  s.disagree = floatLow_ && s.scaleValid && s.pct > 30;
  return s;
}

}  // namespace plantcore
