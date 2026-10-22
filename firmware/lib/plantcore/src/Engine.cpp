#include "Engine.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace plantcore {

const char* sessionKindName(SessionKind k) {
  switch (k) {
    case SessionKind::Auto: return "auto";
    case SessionKind::Manual: return "manual";
    case SessionKind::Fallback: return "fallback";
  }
  return "auto";
}

const char* sessionEndName(SessionEnd e) {
  switch (e) {
    case SessionEnd::None: return "none";
    case SessionEnd::TargetReached: return "target reached";
    case SessionEnd::LimitReached: return "limit reached";
    case SessionEnd::NoResponse: return "no moisture response";
    case SessionEnd::Blocked: return "blocked";
    case SessionEnd::DoseFailed: return "dose failed";
    case SessionEnd::Aborted: return "aborted";
    case SessionEnd::Single: return "done";
  }
  return "none";
}

const char* plannerStateName(PlannerState s) {
  switch (s) {
    case PlannerState::Idle: return "idle";
    case PlannerState::WantPulse: return "waiting";
    case PlannerState::Pulsing: return "watering";
    case PlannerState::Soaking: return "soaking";
  }
  return "idle";
}

namespace {

constexpr int kPlant = 0;  // alert/event channel of the plant
constexpr int kNode = -1;  // ...and of the node itself

std::string fmt(const char* f, ...) {
  char buf[320];
  va_list ap;
  va_start(ap, f);
  vsnprintf(buf, sizeof buf, f, ap);
  va_end(ap);
  return buf;
}

bool hourInWindow(int hour, int start, int end) {
  if (start == end) return true;
  if (start < end) return hour >= start && hour < end;
  return hour >= start || hour < end;
}

const char* issueDescription(SensorIssue issue) {
  switch (issue) {
    case SensorIssue::BusError: return "is not responding (ADC read error)";
    case SensorIssue::Disconnected: return "looks disconnected or shorted";
    case SensorIssue::OutOfRange: return "reads far outside its calibrated range";
    case SensorIssue::Stuck: return "reading has not changed for 12 hours";
    case SensorIssue::Erratic: return "readings are jumping erratically";
    default: return "has a problem";
  }
}

}  // namespace

Engine::Engine(IPump& pump, IEngineSink& sink) : pump_(pump), sink_(sink), dose_(pump) {}

std::string Engine::label() const { return havePlant_ && !plant_.name.empty() ? plant_.name : std::string("Plant"); }

void Engine::resetPlant() {
  monitor_.reset();
  ema_.reset();
  planner_ = Planner{};
  runtime_ = PlantRuntime{};
  haveMoisture_ = false;
  moisturePct_ = 0;
  raw_ = 0;
  trendN_ = 0;
  trendPctPerMin_ = 0;
  lastSampleMs_ = 0;
  lastDoseEndMs_ = -kRecentlyWateredMs;
  sessionMono_ = false;
  wetSinceMs_ = tempOutSinceMs_ = humOutSinceMs_ = -1;
  lastLoggedBlock_ = BlockReason::None;
  alerts_.clearChannel(kPlant, last_.now);
}

void Engine::configure(const Settings& settings, const PlantProfile* plant) {
  const bool same = havePlant_ && plant && plant->id == plant_.id;
  if (!same && (havePlant_ || plant)) {
    pending_.active = false;
    if (!dose_.idle()) dose_.abort(last_.monoMs);
    resetPlant();
  } else if (same && (plant->cal.rawDry != plant_.cal.rawDry || plant->cal.rawWet != plant_.cal.rawWet)) {
    ema_.reset();
    monitor_.reset();
    haveMoisture_ = false;
  }

  settings_ = settings;
  havePlant_ = plant != nullptr;
  if (plant) plant_ = *plant;
  bottle_.configure(plant_.bottle);
  pumpMon_.configure(plant_.pump.currentSense);
  if (!settings_.automationEnabled) planner_.abort(last_.monoMs);
}

void Engine::restore(const PersistedState& st, const Inputs& in) {
  last_ = in;
  runtime_ = st.plant;
  bottle_.restore(st.bottle);
  pumpMon_.restore(st.pump);
  day_ = st.day;
  stopped_ = st.stopped;
  lastRolloverMs_ = in.monoMs;

  if (st.dispenseInProgress && havePlant_) {
    runtime_.mlToday += st.inProgressMl;
    bottle_.dispensed(st.inProgressMl, st.inProgressMl / bottle_.flowFactor());
    if (in.timeValid) runtime_.lastSessionEndS = in.now;
    sessionMono_ = true;
    lastSessionMonoMs_ = in.monoMs;
    const std::string msg =
        fmt("The node restarted while watering %s. The full %.0f ml was counted to be safe.", label().c_str(),
            st.inProgressMl);
    raise(AlertCode::InterruptedDispense, Severity::Warning, kPlant, msg);
    event(EventType::Watering, kPlant, "interrupted", st.inProgressMl, msg);
    persist();
  }
}

void Engine::tick(const Inputs& in) {
  last_ = in;
  rolloverDay(in);
  processBattery(in);
  if (!havePlant_) return;
  processMoisture(in);
  processEnvironment(in);
  stepDose(in);
  processBottle();
  stepPlanner(in);
}

MonoMs Engine::wantTickBy(MonoMs now) const {
  if (pending_.active) return now;
  if (!dose_.idle()) return now + 100;
  switch (planner_.state()) {
    case PlannerState::WantPulse:
    case PlannerState::Pulsing: return now;
    case PlannerState::Soaking: return std::min(planner_.soakUntil(), now + kSessionSampleMs);
    case PlannerState::Idle: break;
  }
  return -1;
}

// ------------------------------------------------------------------ day rollover

void Engine::rolloverDay(const Inputs& in) {
  bool roll = false;
  if (in.timeValid) {
    if (day_ == 0) {
      day_ = in.localDay;
      lastRolloverMs_ = in.monoMs;
      persist();
      return;
    }
    roll = in.localDay != day_;
  } else {
    // No wall clock: fall back to 24 h of uptime so daily limits never lock up forever.
    roll = in.monoMs - lastRolloverMs_ >= 24LL * 3600 * 1000;
  }
  if (!roll) return;

  bottle_.onDayRollover(runtime_.mlToday);
  runtime_.mlToday = 0;
  clear(AlertCode::DailyLimitReached, kPlant);
  if (in.timeValid) day_ = in.localDay;
  lastRolloverMs_ = in.monoMs;
  persist();
}

// ------------------------------------------------------------------ battery

void Engine::processBattery(const Inputs& in) {
  // Cells sag while the pump runs; judge them only at rest.
  if (dose_.running()) {
    batteryQuietUntilMs_ = in.monoMs + 2000;
    return;
  }
  if (!(in.batteryMv > 0) || in.monoMs < batteryQuietUntilMs_) return;
  batteryMv_ = in.batteryMv;

  if (batteryMv_ < kBatteryCriticalMv) {
    batteryCritical_ = true;
  } else if (batteryMv_ > kBatteryCriticalMv + kBatteryRecoverMv) {
    batteryCritical_ = false;
  }
  if (batteryMv_ < kBatteryLowMv) {
    batteryLow_ = true;
  } else if (batteryMv_ > kBatteryLowMv + kBatteryRecoverMv) {
    batteryLow_ = false;
  }

  if (batteryCritical_) {
    raise(AlertCode::BatteryLow, Severity::Critical, kNode,
          fmt("%s: batteries are flat (%.2f V). The pump is off until you replace them.", label().c_str(),
              batteryMv_ / 1000.0f));
  } else if (batteryLow_) {
    raise(AlertCode::BatteryLow, Severity::Warning, kNode,
          fmt("%s: batteries are low (%.2f V). Replace them soon.", label().c_str(), batteryMv_ / 1000.0f));
  } else {
    clear(AlertCode::BatteryLow, kNode);
  }
}

// ------------------------------------------------------------------ sensors

void Engine::pushTrend(float pct, MonoMs now, std::array<float, 3>& pcts, std::array<MonoMs, 3>& ms, int& n,
                       float& rate) {
  pcts = {pcts[1], pcts[2], pct};
  ms = {ms[1], ms[2], now};
  n = std::min(n + 1, 3);
  const MonoMs span = ms[2] - ms[0];
  rate = n == 3 && span > 0 ? (pcts[2] - pcts[0]) / (span / 60000.0f) : 0;
}

bool Engine::sensorUsable(const Inputs& in) const {
  if (!haveMoisture_ || monitor_.health() == SensorHealth::Failed) return false;
  const MonoMs staleMs = std::max<MonoMs>(5LL * settings_.checkInMin * 60000, 5LL * 60 * 1000);
  return in.monoMs - lastSampleMs_ <= staleMs;
}

void Engine::processMoisture(const Inputs& in) {
  if (!in.moisture.fresh) return;
  const PlantProfile& p = plant_;
  const MoistureSample& s = in.moisture;

  const bool recently = !dose_.idle() || in.monoMs - lastDoseEndMs_ < kRecentlyWateredMs;
  raw_ = s.raw;
  const bool usable = monitor_.onSample(s, p.cal, in.monoMs, recently);
  if (usable) {
    const float pct = rawToPercent(s.raw, p.cal);
    const bool hadPrev = haveMoisture_;
    const float prev = moisturePct_;
    moisturePct_ = ema_.update(pct);
    haveMoisture_ = true;
    lastSampleMs_ = in.monoMs;
    if (hadPrev && !recently && planner_.state() == PlannerState::Idle && pct - prev > kDetectedWateringRisePct) {
      event(EventType::Watering, kPlant, "detected", 0,
            fmt("%s: moisture jumped from %.0f%% to %.0f%% without the node watering (watered by hand?)",
                label().c_str(), prev, pct));
      ema_.reset();
      moisturePct_ = ema_.update(pct);
      trendN_ = 0;
    }
    pushTrend(moisturePct_, in.monoMs, trendPct_, trendMs_, trendN_, trendPctPerMin_);
  }

  const SensorIssue issue = monitor_.issue();
  if (issue != SensorIssue::NoResponse) clear(AlertCode::SensorNoResponse, kPlant);
  if (issue == SensorIssue::Uncalibrated) {
    raise(AlertCode::CalibrationNeeded, Severity::Info, kPlant,
          fmt("%s: calibrate the moisture sensor (dry and wet points) before automatic watering can start.",
              label().c_str()));
    clear(AlertCode::SensorFault, kPlant);
  } else {
    clear(AlertCode::CalibrationNeeded, kPlant);
    if (issue == SensorIssue::None || issue == SensorIssue::NoResponse) {
      clear(AlertCode::SensorFault, kPlant);
    } else {
      const Severity sev = monitor_.health() == SensorHealth::Failed ? Severity::Warning : Severity::Info;
      raise(AlertCode::SensorFault, sev, kPlant,
            fmt("%s: moisture sensor %s.", label().c_str(), issueDescription(issue)));
    }
  }

  if (!usable || !haveMoisture_) return;
  if (moisturePct_ < p.moisture.critical) {
    raise(AlertCode::PlantTooDry, Severity::Warning, kPlant,
          fmt("%s is very dry (%.0f%%, critical below %.0f%%).", label().c_str(), moisturePct_, p.moisture.critical));
  } else if (moisturePct_ > p.moisture.min) {
    clear(AlertCode::PlantTooDry, kPlant);
  }
  if (moisturePct_ > p.moisture.max) {
    if (wetSinceMs_ < 0) {
      wetSinceMs_ = in.monoMs;
    } else if (in.monoMs - wetSinceMs_ >= kTooWetAfterMs) {
      raise(AlertCode::PlantTooWet, Severity::Warning, kPlant,
            fmt("%s has stayed above %.0f%% moisture for 2 days. Check drainage and pot size, or adjust its band.",
                label().c_str(), p.moisture.max));
    }
  } else if (moisturePct_ < p.moisture.max - 2) {
    wetSinceMs_ = -1;
    clear(AlertCode::PlantTooWet, kPlant);
  }
}

void Engine::processEnvironment(const Inputs& in) {
  if (!in.env.valid) {
    tempOutSinceMs_ = humOutSinceMs_ = -1;
    return;
  }
  const PlantProfile& p = plant_;

  const bool tempOut = in.env.tempC < p.tempC.min || in.env.tempC > p.tempC.max;
  if (!tempOut) {
    tempOutSinceMs_ = -1;
    clear(AlertCode::TempOutOfRange, kPlant);
  } else if (tempOutSinceMs_ < 0) {
    tempOutSinceMs_ = in.monoMs;
  } else if (in.monoMs - tempOutSinceMs_ >= kEnvAlertAfterMs) {
    raise(AlertCode::TempOutOfRange, Severity::Info, kPlant,
          fmt("%s prefers %.0f-%.0f °C; it has been %.1f °C for over 2 hours.", label().c_str(), p.tempC.min,
              p.tempC.max, in.env.tempC));
  }

  const bool humOut = in.env.humidityPct < p.humidityPct.min || in.env.humidityPct > p.humidityPct.max;
  if (!humOut) {
    humOutSinceMs_ = -1;
    clear(AlertCode::HumidityOutOfRange, kPlant);
  } else if (humOutSinceMs_ < 0) {
    humOutSinceMs_ = in.monoMs;
  } else if (in.monoMs - humOutSinceMs_ >= kEnvAlertAfterMs) {
    raise(AlertCode::HumidityOutOfRange, Severity::Info, kPlant,
          fmt("%s prefers %.0f-%.0f %% humidity; it has been %.0f %% for over 2 hours.", label().c_str(),
              p.humidityPct.min, p.humidityPct.max, in.env.humidityPct));
  }
}

// ------------------------------------------------------------------ bottle

void Engine::processBottle() {
  const BottleStatus b = bottle_.status();
  if (b.empty) {
    std::string why;
    switch (b.reason) {
      case EmptyReason::PumpDry: why = "the pump ran dry"; break;
      case EmptyReason::NoResponse: why = "watering stopped making the soil wetter (or the tube slipped out)"; break;
      default: why = "the gauge counted it empty"; break;
    }
    raise(AlertCode::BottleEmpty, Severity::Critical, kPlant,
          fmt("%s's bottle is empty: %s. Refill it, then press the node's button.", label().c_str(), why.c_str()));
    clear(AlertCode::BottleLow, kPlant);
    return;
  }
  clear(AlertCode::BottleEmpty, kPlant);
  if (b.low) {
    const std::string days = b.daysLeft >= 0 ? fmt(", about %.1f days left", b.daysLeft) : std::string();
    raise(AlertCode::BottleLow, Severity::Warning, kPlant,
          fmt("%s's bottle is low: about %.0f ml (%.0f%%)%s.", label().c_str(), b.ml, b.pct, days.c_str()));
  } else {
    clear(AlertCode::BottleLow, kPlant);
  }
}

void Engine::refilled(float ml) {
  bottle_.refilled(ml);
  const BottleStatus b = bottle_.status();
  clear(AlertCode::BottleEmpty, kPlant);
  if (!b.low) clear(AlertCode::BottleLow, kPlant);
  event(EventType::Refill, kPlant, "refill", ml > 0 ? ml : b.capacityMl,
        ml > 0 && ml < b.capacityMl ? fmt("Bottle topped up (+%.0f ml)", ml) : std::string("Bottle refilled"));
  persist();
}

// ------------------------------------------------------------------ dosing

float Engine::effectiveFlow() const { return std::max(plant_.pump.flowMlPerS, 0.05f) * bottle_.flowFactor(); }

bool Engine::fitToGauge(Verdict& v) {
  // Once the pump's wet current is known, the pump itself tells when the bottle is
  // empty. Until then the gauge is the only guard: never pump into the reserve.
  if (!v.allowed || pumpMon_.ready()) return v.allowed;
  const float available = bottle_.status().ml - plant_.bottle.reserveMl;
  if (available < kMinDoseMl) {
    bottle_.markEmpty(EmptyReason::Gauge);
    processBottle();
    v = Verdict{false, BlockReason::BottleEmpty, 0};
    return false;
  }
  v.ml = std::min(v.ml, available);
  return true;
}

NodeState Engine::nodeState() const {
  NodeState n;
  n.automationEnabled = settings_.automationEnabled;
  n.emergencyStop = stopped_;
  n.bottleEmpty = bottle_.empty();
  n.batteryCritical = batteryCritical_;
  return n;
}

void Engine::startDose(float ml, SessionKind kind, bool timed) {
  DoseRequest req;
  req.ml = ml;
  req.flowMlPerS = effectiveFlow();
  req.maxOnMs = maxOnTimeMs(settings_.limits, ml, req.flowMlPerS);
  doseTimed_ = timed;
  doseKind_ = kind;

  // Record the dose before any water moves, so a power cut mid-dose is accounted for.
  dispenseInProgress_ = true;
  inProgressMl_ = ml;
  persist();

  dose_.start(req, last_.monoMs);
  pumpMon_.start(last_.monoMs);
}

void Engine::stepDose(const Inputs& in) {
  if (pending_.active && dose_.idle()) {
    pending_.active = false;
    startDose(pending_.ml, SessionKind::Manual, pending_.timed);
  }
  if (dose_.idle()) return;
  if (dose_.running() && in.pumpSense && pumpMon_.enabled()) {
    switch (pumpMon_.sample(in.monoMs, in.pumpMa, in.batteryMv)) {
      case PumpVerdict::Dry: dose_.fail(DoseFault::PumpDry, in.monoMs, pumpMon_.drySinceMs()); break;
      case PumpVerdict::Disconnected: dose_.fail(DoseFault::PumpDisconnected, in.monoMs, 0); break;
      case PumpVerdict::Jammed: dose_.fail(DoseFault::PumpJammed, in.monoMs, 0); break;
      case PumpVerdict::Ok: break;
    }
  }
  dose_.tick(in.monoMs);
  if (dose_.finished()) finishDose(in);
}

void Engine::finishDose(const Inputs& in) {
  const float ml = dose_.dispensedMl();
  const DoseFault fault = dose_.fault();

  if (!doseTimed_) runtime_.mlToday += ml;
  bottle_.dispensed(ml, ml / bottle_.flowFactor());
  // Learn the wet current only from runs that very likely moved water.
  pumpMon_.finish(fault == DoseFault::None && bottle_.status().pct >= kPumpLearnMinBottlePct);
  lastPumpVerdict_ = pumpMon_.verdict();
  lastDoseEndMs_ = in.monoMs;
  dispenseInProgress_ = false;

  lastDose_.valid = true;
  lastDose_.ml = ml;
  lastDose_.onTimeMs = dose_.onTimeMs();
  lastDose_.timed = doseTimed_;
  lastDose_.fault = fault;
  lastDose_.at = in.now;

  if (doseKind_ == SessionKind::Manual) {
    if (ml > 0 && !doseTimed_) {
      if (in.timeValid) runtime_.lastSessionEndS = in.now;
      sessionMono_ = true;
      lastSessionMonoMs_ = in.monoMs;
    }
    std::string detail = doseTimed_ ? fmt("Timed pump run (%.1f s)", dose_.onTimeMs() / 1000.0f)
                                    : std::string("Manual watering");
    if (fault != DoseFault::None) detail += std::string(" stopped: ") + doseFaultName(fault);
    event(EventType::Watering, kPlant, doseTimed_ ? "timed" : "manual", ml, detail);
  } else if (fault == DoseFault::None) {
    planner_.onDoseFinished(ml, in.monoMs, plant_.soakMin);
  } else {
    planner_.onDoseFailed(in.monoMs);
  }
  dose_.acknowledge();

  switch (fault) {
    case DoseFault::PumpDry:
      bottle_.markEmpty(EmptyReason::PumpDry);
      break;
    case DoseFault::PumpDisconnected:
      runtime_.lockedOut = true;
      raise(AlertCode::PumpDisconnected, Severity::Warning, kPlant,
            fmt("%s's pump drew no current: check its wires and the MOSFET. Watering is locked until you clear it.",
                label().c_str()));
      break;
    case DoseFault::PumpJammed:
      runtime_.lockedOut = true;
      raise(AlertCode::PumpJammed, Severity::Warning, kPlant,
            fmt("%s's pump drew far too much current: the impeller may be blocked. Watering is locked until you "
                "clear it.",
                label().c_str()));
      break;
    case DoseFault::None:
      // Without a learned pump current the gauge decides; with one, the pump does.
      if (bottle_.belowReserve() && !pumpMon_.ready()) bottle_.markEmpty(EmptyReason::Gauge);
      break;
    case DoseFault::Aborted:
      break;
  }
  persist();
}

// ------------------------------------------------------------------ planning

float Engine::secondsSinceLastSession(const Inputs& in) const {
  if (sessionMono_) return (in.monoMs - lastSessionMonoMs_) / 1000.0f;
  if (runtime_.lastSessionEndS > 0) {
    if (in.timeValid) return static_cast<float>(std::max<EpochS>(0, in.now - runtime_.lastSessionEndS));
    return in.monoMs / 1000.0f;  // wall time unknown: assume it happened just before boot
  }
  return 1e9f;
}

void Engine::stepPlanner(const Inputs& in) {
  const PlantProfile& p = plant_;

  // A session may have ended in stepDose or a command; account for it before the
  // planner decides anything, or it would not see the watering it just did.
  SessionReport r;
  if (planner_.takeReport(r)) handleReport(r, in);

  PlannerInput pin;
  pin.monoMs = in.monoMs;
  pin.sensorUsable = sensorUsable(in);
  // Timed fallback only for a confirmed hardware fault: not while waiting for the first
  // reading, and not after "no response" (the water may not be reaching the pot).
  const SensorIssue issue = monitor_.issue();
  pin.fallbackAllowed =
      issue == SensorIssue::BusError || issue == SensorIssue::Disconnected || issue == SensorIssue::OutOfRange;
  pin.moisturePct = moisturePct_;
  pin.trendPctPerMin = trendPctPerMin_;
  pin.adjust = computeAdjust(p, in.env, in.localMonth, settings_.southernHemisphere);
  pin.inWindow = !in.timeValid || hourInWindow(in.localHour, settings_.waterWindowStartH, settings_.waterWindowEndH);
  pin.secondsSinceLastSession = secondsSinceLastSession(in);
  float sinceFallback = 1e9f;
  if (runtime_.lastFallbackS > 0 && in.timeValid) sinceFallback = static_cast<float>(in.now - runtime_.lastFallbackS);
  pin.secondsSinceLastFallback = std::min(sinceFallback, pin.secondsSinceLastSession);

  if (settings_.automationEnabled && !stopped_) planner_.tick(p, runtime_, pin);

  if (planner_.state() == PlannerState::WantPulse && dose_.idle()) {
    DispenseRequest req;
    req.kind = planner_.kind();
    req.continuation = planner_.continuation();
    req.ml = planner_.pendingMl();
    req.secondsSinceLastSession = pin.secondsSinceLastSession;
    Verdict v = evaluateDispense(settings_.limits, nodeState(), p, runtime_, monitor_.health(), req);
    if (fitToGauge(v)) {
      startDose(v.ml, req.kind, false);
      planner_.onDoseStarted();
      lastLoggedBlock_ = BlockReason::None;
    } else {
      planner_.onBlocked(in.monoMs);
      if (v.reason != lastLoggedBlock_) {
        event(EventType::System, kPlant, "blocked", 0,
              fmt("%s: watering skipped (%s)", label().c_str(), blockReasonName(v.reason)));
        lastLoggedBlock_ = v.reason;
      }
      if (v.reason == BlockReason::PlantDailyLimit) {
        raise(AlertCode::DailyLimitReached, Severity::Info, kPlant,
              fmt("%s reached its daily watering limit.", label().c_str()));
      }
    }
  }

  if (planner_.takeReport(r)) handleReport(r, in);
}

void Engine::handleReport(const SessionReport& r, const Inputs& in) {
  const PlantProfile& p = plant_;

  if (r.ml > 0) {
    if (in.timeValid) {
      runtime_.lastSessionEndS = in.now;
      if (r.kind == SessionKind::Fallback) runtime_.lastFallbackS = in.now;
    }
    sessionMono_ = true;
    lastSessionMonoMs_ = in.monoMs;
    const std::string detail =
        r.kind == SessionKind::Fallback
            ? std::string("Fallback schedule (moisture sensor unavailable)")
            : fmt("%d pulse%s, %.0f%% -> %.0f%%, %s", r.pulses, r.pulses == 1 ? "" : "s", r.startPct, r.endPct,
                  sessionEndName(r.end));
    event(EventType::Watering, kPlant, sessionKindName(r.kind), r.ml, detail);
  }

  switch (r.end) {
    case SessionEnd::TargetReached:
      clear(AlertCode::TargetNotReached, kPlant);
      break;
    case SessionEnd::LimitReached:
      if (r.endPct < p.moisture.min) {
        raise(AlertCode::TargetNotReached, Severity::Warning, kPlant,
              fmt("%s only reached %.0f%% after %.0f ml (target %.0f%%). Check the sensor and drainage, or raise "
                  "its limits.",
                  label().c_str(), r.endPct, r.ml, sessionTargetPct(p)));
      }
      break;
    case SessionEnd::NoResponse:
      // The pump felt water moving: the water went somewhere, but the sensor didn't
      // see it. Otherwise the likeliest cause is an empty bottle.
      if (pumpMon_.ready() && lastPumpVerdict_ == PumpVerdict::Ok) {
        monitor_.reportNoResponse(r.endPct);
        raise(AlertCode::SensorNoResponse, Severity::Warning, kPlant,
              fmt("%s's moisture did not rise after %.0f ml although the pump moved water. Check the sensor is in "
                  "the soil and the tube ends in the pot.",
                  label().c_str(), r.ml));
      } else {
        bottle_.markEmpty(EmptyReason::NoResponse);
        processBottle();
      }
      break;
    default:
      break;
  }
  persist();
}

// ------------------------------------------------------------------ commands

std::string Engine::manualWater(float ml) { return manualDose(ml, false); }

std::string Engine::timedRun(float seconds) {
  if (!havePlant_) return "no_plant";
  if (!(seconds >= 1 && seconds <= 60)) return "invalid_amount";
  return manualDose(seconds * effectiveFlow(), true);
}

std::string Engine::manualDose(float ml, bool timed) {
  if (!havePlant_) return "no_plant";
  if (doseActive()) return "busy";
  if (planner_.state() != PlannerState::Idle) planner_.abort(last_.monoMs);

  DispenseRequest req;
  req.kind = SessionKind::Manual;
  req.ml = ml;
  Verdict v = evaluateDispense(settings_.limits, nodeState(), plant_, runtime_, monitor_.health(), req);
  if (!fitToGauge(v)) {
    event(EventType::System, kPlant, "blocked", 0,
          fmt("%s: %s refused (%s)", label().c_str(), timed ? "timed run" : "watering", blockReasonName(v.reason)));
    return blockReasonName(v.reason);
  }
  pending_ = PendingDose{true, v.ml, timed};
  return "";
}

void Engine::setStopped(bool stop, const std::string& why) {
  if (stop) {
    pump_.off();
    pending_.active = false;
    if (!dose_.idle()) dose_.abort(last_.monoMs);
    planner_.abort(last_.monoMs);
    if (stopped_) return;
    stopped_ = true;
    event(EventType::System, kNode, "emergency_stop", 0, why);
  } else {
    if (!stopped_) return;
    stopped_ = false;
    event(EventType::System, kNode, "resume", 0, "Watering resumed");
  }
  persist();
}

void Engine::setAutomation(bool enabled) {
  if (settings_.automationEnabled == enabled) return;
  settings_.automationEnabled = enabled;
  if (!enabled) planner_.abort(last_.monoMs);
}

std::string Engine::clearFaults() {
  if (!havePlant_) return "no_plant";
  runtime_.lockedOut = false;
  monitor_.clearNoResponse();
  clear(AlertCode::PumpDisconnected, kPlant);
  clear(AlertCode::PumpJammed, kPlant);
  clear(AlertCode::SensorNoResponse, kPlant);
  event(EventType::System, kPlant, "lockout_cleared", 0, label() + ": faults cleared by user");
  persist();
  return "";
}

void Engine::setSystemAlert(AlertCode code, Severity sev, bool active, const std::string& message) {
  if (active) {
    raise(code, sev, kNode, message);
  } else {
    clear(code, kNode);
  }
}

// ------------------------------------------------------------------ status

NodeStatus Engine::status() const {
  NodeStatus s;
  s.hasPlant = havePlant_;
  if (havePlant_) {
    s.plantId = plant_.id;
    s.haveMoisture = haveMoisture_;
    s.moisturePct = moisturePct_;
    s.raw = raw_;
    s.health = monitor_.health();
    s.issue = monitor_.issue();
    s.planner = planner_.state();
    const WateringAdjust adj = computeAdjust(plant_, last_.env, last_.localMonth, settings_.southernHemisphere);
    s.triggerPct = triggerPct(plant_, adj);
    s.targetPct = sessionTargetPct(plant_);
    s.mlToday = runtime_.mlToday;
    s.lastWateredAt = runtime_.lastSessionEndS;
    s.lockedOut = runtime_.lockedOut;
    s.mlPerPct = runtime_.mlPerPct;
    if (planner_.state() == PlannerState::Soaking) {
      s.soakRemainingMs = std::max<MonoMs>(0, planner_.soakUntil() - last_.monoMs);
    }
  }
  s.bottle = bottle_.status();
  if (!dose_.idle()) {
    s.doseActive = true;
    s.doseMl = dose_.request().ml;
    s.doseDispensedMl = dose_.dispensedSoFar(last_.monoMs);
  }
  s.lastDose = lastDose_;
  s.pumpVerdict = lastPumpVerdict_;
  s.pumpReady = pumpMon_.ready();
  s.pumpWetMaPerV = pumpMon_.learned().wetMaPerV;
  s.batteryMv = batteryMv_;
  s.batteryLow = batteryLow_;
  s.stopped = stopped_;
  s.automationEnabled = settings_.automationEnabled;
  for (const auto& a : alerts_.all()) {
    if (a.active && a.code < AlertCode::Count) s.activeAlerts |= 1u << static_cast<uint8_t>(a.code);
  }
  return s;
}

PersistedState Engine::persisted() const {
  PersistedState st;
  st.plant = runtime_;
  st.bottle = bottle_.state();
  st.pump = pumpMon_.learned();
  st.day = day_;
  st.stopped = stopped_;
  st.dispenseInProgress = dispenseInProgress_;
  st.inProgressMl = inProgressMl_;
  return st;
}

// ------------------------------------------------------------------ helpers

void Engine::raise(AlertCode code, Severity sev, int ch, const std::string& msg) {
  bool existed = false;
  Severity before = Severity::Info;
  for (const auto& a : alerts_.all()) {
    if (a.active && a.code == code && a.channel == ch) {
      existed = true;
      before = a.severity;
    }
  }
  const bool fresh = alerts_.raise(code, sev, static_cast<int8_t>(ch), msg, last_.now);
  // Escalations are announced again so the main station sees the new severity.
  if (fresh || (existed && sev > before)) {
    Event e;
    e.ts = last_.now;
    e.type = EventType::AlertRaised;
    e.channel = static_cast<int8_t>(ch);
    e.code = alertCodeName(code);
    e.detail = msg;
    e.severity = sev;
    sink_.onEvent(e);
  }
}

void Engine::clear(AlertCode code, int ch) {
  if (alerts_.clear(code, static_cast<int8_t>(ch), last_.now)) {
    event(EventType::AlertCleared, ch, alertCodeName(code), 0, "");
  }
}

void Engine::event(EventType type, int ch, const std::string& code, float ml, const std::string& detail) {
  Event e;
  e.ts = last_.now;
  e.type = type;
  e.channel = static_cast<int8_t>(ch);
  e.code = code;
  e.ml = ml;
  e.detail = detail;
  sink_.onEvent(e);
}

void Engine::persist() { sink_.onPersist(persisted()); }

}  // namespace plantcore
