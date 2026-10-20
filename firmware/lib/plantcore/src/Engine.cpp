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

Engine::Engine(IOutlets& outlets, IEngineSink& sink) : outlets_(outlets), sink_(sink), dose_(outlets) {}

const PlantProfile* Engine::plantAt(int ch) const {
  int idx = ch_[ch].plant;
  return idx >= 0 && idx < static_cast<int>(plants_.size()) ? &plants_[idx] : nullptr;
}

std::string Engine::plantLabel(int ch) const {
  const PlantProfile* p = plantAt(ch);
  if (p && !p->name.empty()) return p->name;
  return fmt("Channel %d", ch + 1);
}

int Engine::plantIndex(const std::string& id) const {
  for (size_t i = 0; i < plants_.size(); ++i) {
    if (plants_[i].id == id) return static_cast<int>(i);
  }
  return -1;
}

void Engine::configure(const Settings& settings, const std::vector<PlantProfile>& plants) {
  std::array<int, kMaxPlants> map;
  map.fill(-1);
  for (size_t i = 0; i < plants.size(); ++i) {
    int c = plants[i].channel;
    if (c < kMaxPlants && map[c] < 0) map[c] = static_cast<int>(i);
  }

  for (int c = 0; c < kMaxPlants; ++c) {
    const PlantProfile* before = plantAt(c);
    const PlantProfile* after = map[c] >= 0 ? &plants[map[c]] : nullptr;
    const bool same = before && after && before->id == after->id;
    if (!same && (before || after)) {
      if (!dose_.idle() && dose_.request().channel == c) dose_.abort(last_.monoMs);
      ch_[c] = Channel{};
      alerts_.clearChannel(static_cast<int8_t>(c), last_.now);
    } else if (same && (before->cal.rawDry != after->cal.rawDry || before->cal.rawWet != after->cal.rawWet)) {
      ch_[c].ema.reset();
      ch_[c].monitor.reset();
      ch_[c].haveMoisture = false;
    }
  }

  settings_ = settings;
  plants_ = plants;
  for (int c = 0; c < kMaxPlants; ++c) ch_[c].plant = map[c];
  reservoir_.configure(settings_.reservoir);
  if (!settings_.automationEnabled) {
    for (auto& c : ch_) c.planner.abort(last_.monoMs);
  }
}

void Engine::restore(const PersistedState& st, const Inputs& in) {
  last_ = in;
  for (int c = 0; c < kMaxPlants; ++c) ch_[c].runtime = st.plants[c];
  day_ = st.day;
  mlTodayTotal_ = st.mlTodayTotal;
  reservoir_.setUsagePerDay(st.usagePerDayMl);
  lastRolloverMs_ = in.monoMs;

  if (st.emergencyStop) {
    emergencyStop_ = true;
    raise(AlertCode::EmergencyStop, Severity::Info, -1,
          "Emergency stop is still active after a restart. Resume from the dashboard.");
  }
  if (st.dispenseInProgress && st.inProgressChannel < kMaxPlants) {
    int c = st.inProgressChannel;
    Channel& chn = ch_[c];
    chn.runtime.mlToday += st.inProgressMl;
    mlTodayTotal_ += st.inProgressMl;
    if (in.timeValid) chn.runtime.lastSessionEndS = in.now;
    chn.sessionMono = true;
    chn.lastSessionMonoMs = in.monoMs;
    std::string msg = fmt("The device restarted while watering %s. The full %.0f ml was counted to be safe.",
                          plantLabel(c).c_str(), st.inProgressMl);
    raise(AlertCode::InterruptedDispense, Severity::Warning, c, msg);
    event(EventType::Watering, c, "interrupted", st.inProgressMl, msg);
    persist();
  }
}

void Engine::tick(const Inputs& in) {
  last_ = in;
  rolloverDay(in);
  handleInterlocks(in);
  processMoisture(in);
  processEnvironment(in);
  processReservoir(in);
  stepDose(in);
  stepPlanners(in);
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

  reservoir_.onDayRollover(mlTodayTotal_);
  for (int c = 0; c < kMaxPlants; ++c) {
    ch_[c].runtime.mlToday = 0;
    clear(AlertCode::DailyLimitReached, c);
  }
  mlTodayTotal_ = 0;
  if (in.timeValid) day_ = in.localDay;
  lastRolloverMs_ = in.monoMs;
  persist();
}

// ------------------------------------------------------------------ interlocks

void Engine::handleInterlocks(const Inputs& in) {
  const bool leakNow = settings_.leakSensorEnabled && in.leak;
  if (leakNow) {
    if (!leakLatched_) {
      leakLatched_ = true;
      raise(AlertCode::LeakDetected, Severity::Critical, -1,
            "Water detected by the leak sensor. All watering stopped until you resume.");
      emergencyStop("leak detected");
    }
  } else {
    clear(AlertCode::LeakDetected, -1);
  }

  if (settings_.reservoir.scaleEnabled) {
    if (!in.scaleOk) {
      if (scaleBadSinceMs_ < 0) {
        scaleBadSinceMs_ = in.monoMs;
      } else if (in.monoMs - scaleBadSinceMs_ >= kScaleFaultAfterMs) {
        raise(AlertCode::ScaleFault, Severity::Warning, -1,
              "The reservoir scale is not responding or not calibrated. Doses fall back to timed mode; the "
              "float switch still protects against running dry.");
      }
    } else {
      scaleBadSinceMs_ = -1;
      clear(AlertCode::ScaleFault, -1);
    }
  }
}

// ------------------------------------------------------------------ sensors

void Engine::pushTrend(Channel& chn, float pct, MonoMs now) {
  chn.trendPct = {chn.trendPct[1], chn.trendPct[2], pct};
  chn.trendMs = {chn.trendMs[1], chn.trendMs[2], now};
  chn.trendN = std::min(chn.trendN + 1, 3);
  const MonoMs span = chn.trendMs[2] - chn.trendMs[0];
  chn.trendPctPerMin = chn.trendN == 3 && span > 0 ? (chn.trendPct[2] - chn.trendPct[0]) / (span / 60000.0f) : 0;
}

bool Engine::sensorUsable(int c, const Inputs& in) const {
  const Channel& chn = ch_[c];
  if (!chn.haveMoisture || chn.monitor.health() == SensorHealth::Failed) return false;
  MonoMs staleMs = std::max<MonoMs>(5LL * settings_.sampleIntervalS * 1000, 5LL * 60 * 1000);
  return in.monoMs - chn.lastSampleMs <= staleMs;
}

void Engine::processMoisture(const Inputs& in) {
  for (int c = 0; c < kMaxPlants; ++c) {
    Channel& chn = ch_[c];
    if (chn.plant < 0 || !in.moisture[c].fresh) continue;
    const PlantProfile& p = plants_[chn.plant];
    const MoistureSample& s = in.moisture[c];

    const bool dosingHere = !dose_.idle() && dose_.request().channel == c;
    const bool recently = dosingHere || in.monoMs - chn.lastDoseEndMs < kRecentlyWateredMs;
    chn.raw = s.raw;
    const bool usable = chn.monitor.onSample(s, p.cal, in.monoMs, recently);
    if (usable) {
      const float pct = rawToPercent(s.raw, p.cal);
      const bool hadPrev = chn.haveMoisture;
      const float prev = chn.moisturePct;
      chn.moisturePct = chn.ema.update(pct);
      chn.haveMoisture = true;
      chn.lastSampleMs = in.monoMs;
      if (hadPrev && !recently && chn.planner.state() == PlannerState::Idle &&
          pct - prev > kDetectedWateringRisePct) {
        event(EventType::Watering, c, "detected", 0,
              fmt("%s: moisture jumped from %.0f%% to %.0f%% without the system watering (watered by hand?)",
                  plantLabel(c).c_str(), prev, pct));
        chn.ema.reset();
        chn.moisturePct = chn.ema.update(pct);
        chn.trendN = 0;
      }
      pushTrend(chn, chn.moisturePct, in.monoMs);
    }

    const SensorIssue issue = chn.monitor.issue();
    if (issue != SensorIssue::NoResponse) clear(AlertCode::SensorNoResponse, c);
    if (issue == SensorIssue::Uncalibrated) {
      raise(AlertCode::CalibrationNeeded, Severity::Info, c,
            fmt("%s: calibrate the moisture sensor (dry and wet points) before automatic watering can start.",
                plantLabel(c).c_str()));
      clear(AlertCode::SensorFault, c);
    } else {
      clear(AlertCode::CalibrationNeeded, c);
      if (issue == SensorIssue::None || issue == SensorIssue::NoResponse) {
        clear(AlertCode::SensorFault, c);
      } else {
        Severity sev = chn.monitor.health() == SensorHealth::Failed ? Severity::Warning : Severity::Info;
        raise(AlertCode::SensorFault, sev, c,
              fmt("%s: moisture sensor %s.", plantLabel(c).c_str(), issueDescription(issue)));
      }
    }

    if (!usable || !chn.haveMoisture) continue;
    if (chn.moisturePct < p.moisture.critical) {
      raise(AlertCode::PlantTooDry, Severity::Warning, c,
            fmt("%s is very dry (%.0f%%, critical below %.0f%%).", plantLabel(c).c_str(), chn.moisturePct,
                p.moisture.critical));
    } else if (chn.moisturePct > p.moisture.min) {
      clear(AlertCode::PlantTooDry, c);
    }
    if (chn.moisturePct > p.moisture.max) {
      if (chn.wetSinceMs < 0) {
        chn.wetSinceMs = in.monoMs;
      } else if (in.monoMs - chn.wetSinceMs >= kTooWetAfterMs) {
        raise(AlertCode::PlantTooWet, Severity::Warning, c,
              fmt("%s has stayed above %.0f%% moisture for 2 days. Check drainage and pot size, or adjust "
                  "its moisture band.",
                  plantLabel(c).c_str(), p.moisture.max));
      }
    } else if (chn.moisturePct < p.moisture.max - 2) {
      chn.wetSinceMs = -1;
      clear(AlertCode::PlantTooWet, c);
    }
  }
}

void Engine::processEnvironment(const Inputs& in) {
  for (int c = 0; c < kMaxPlants; ++c) {
    Channel& chn = ch_[c];
    if (chn.plant < 0) continue;
    if (!in.env.valid) {
      chn.tempOutSinceMs = chn.humOutSinceMs = -1;
      continue;
    }
    const PlantProfile& p = plants_[chn.plant];

    const bool tempOut = in.env.tempC < p.tempC.min || in.env.tempC > p.tempC.max;
    if (!tempOut) {
      chn.tempOutSinceMs = -1;
      clear(AlertCode::TempOutOfRange, c);
    } else if (chn.tempOutSinceMs < 0) {
      chn.tempOutSinceMs = in.monoMs;
    } else if (in.monoMs - chn.tempOutSinceMs >= kEnvAlertAfterMs) {
      raise(AlertCode::TempOutOfRange, Severity::Info, c,
            fmt("%s prefers %.0f-%.0f °C; it has been %.1f °C for over 2 hours.", plantLabel(c).c_str(),
                p.tempC.min, p.tempC.max, in.env.tempC));
    }

    const bool humOut = in.env.humidityPct < p.humidityPct.min || in.env.humidityPct > p.humidityPct.max;
    if (!humOut) {
      chn.humOutSinceMs = -1;
      clear(AlertCode::HumidityOutOfRange, c);
    } else if (chn.humOutSinceMs < 0) {
      chn.humOutSinceMs = in.monoMs;
    } else if (in.monoMs - chn.humOutSinceMs >= kEnvAlertAfterMs) {
      raise(AlertCode::HumidityOutOfRange, Severity::Info, c,
            fmt("%s prefers %.0f-%.0f %% humidity; it has been %.0f %% for over 2 hours.", plantLabel(c).c_str(),
                p.humidityPct.min, p.humidityPct.max, in.env.humidityPct));
    }
  }
}

// ------------------------------------------------------------------ reservoir

void Engine::processReservoir(const Inputs& in) {
  const bool floatLow = settings_.reservoir.floatSwitchEnabled && in.floatLow;
  reservoir_.update(in.monoMs, in.scaleOk, in.scaleGrams, floatLow, !dose_.idle());

  ReservoirEvent ev;
  if (reservoir_.takeEvent(ev)) {
    if (ev.type == ReservoirEventType::Refill) {
      event(EventType::Refill, -1, "refill", ev.ml, fmt("Reservoir refilled (+%.0f ml)", ev.ml));
    } else if (ev.type == ReservoirEventType::UnexplainedLoss) {
      raise(AlertCode::ReservoirUnexplainedLoss, Severity::Critical, -1,
            fmt("The reservoir lost %.0f ml while nothing was being watered. Possible leak or a valve stuck "
                "open. All watering stopped.",
                ev.ml));
      emergencyStop("unexplained reservoir loss");
    }
  }

  const ReservoirStatus rs = reservoir_.status();
  if (rs.empty) {
    raise(AlertCode::ReservoirEmpty, Severity::Critical, -1,
          rs.floatLow ? "The reservoir is empty (float switch). Watering is paused until it is refilled."
                      : "The reservoir is empty or lifted off the scale. Watering is paused until it is refilled.");
    if (dose_.outletOpen()) dose_.abort(in.monoMs);
    clear(AlertCode::ReservoirLow, -1);
  } else {
    clear(AlertCode::ReservoirEmpty, -1);
    const bool stillLow = alerts_.isActive(AlertCode::ReservoirLow, -1) && rs.scaleValid &&
                          rs.pct < settings_.reservoir.lowPct + 3;
    if (rs.low || stillLow) {
      std::string days = rs.daysLeft >= 0 ? fmt(", about %.1f days left", rs.daysLeft) : std::string();
      raise(AlertCode::ReservoirLow, Severity::Warning, -1,
            fmt("The reservoir is low: %.0f ml (%.0f%%)%s.", rs.ml, rs.pct, days.c_str()));
    } else {
      clear(AlertCode::ReservoirLow, -1);
    }
  }

  if (rs.disagree) {
    raise(AlertCode::FloatScaleDisagree, Severity::Warning, -1,
          fmt("The float switch reports an empty reservoir but the scale reads %.0f ml. One of them is faulty; "
              "the reservoir is treated as empty.",
              rs.ml));
  } else {
    clear(AlertCode::FloatScaleDisagree, -1);
  }
}

// ------------------------------------------------------------------ dosing

float Engine::effectiveFlow(const PlantProfile& p) const {
  float flow = std::max(p.outlet.flowMlPerS, 0.05f);
  if (p.outlet.type != OutletType::Valve) return flow;
  // Gravity feed: flow ~ sqrt(head). Scale the calibrated flow by the current head.
  const ReservoirStatus rs = reservoir_.status();
  if (!rs.scaleValid) return flow;
  const float h = settings_.reservoir.fullHeightMm;
  const float headNow = p.outlet.dropMm + rs.pct / 100.0f * h;
  const float headCal = p.outlet.dropMm + p.outlet.calFillPct / 100.0f * h;
  if (headNow > 1 && headCal > 1) flow *= std::sqrt(headNow / headCal);
  return flow;
}

GlobalState Engine::globalState() const {
  GlobalState g;
  g.automationEnabled = settings_.automationEnabled;
  g.emergencyStop = emergencyStop_;
  g.leak = leakLatched_ || (settings_.leakSensorEnabled && last_.leak);
  g.reservoirEmpty = reservoir_.status().empty;
  g.outletActive = !dose_.idle();
  g.mlTodayTotal = mlTodayTotal_;
  return g;
}

void Engine::startDose(int c, float ml, SessionKind kind, bool openLoop) {
  const PlantProfile& p = *plantAt(c);
  DoseRequest req;
  req.channel = static_cast<uint8_t>(c);
  req.ml = ml;
  req.flowMlPerS = effectiveFlow(p);
  req.maxOnMs = maxOnTimeMs(settings_.limits, ml, req.flowMlPerS);
  req.closedLoop = !openLoop && settings_.reservoir.scaleEnabled && last_.scaleOk;
  doseTimed_ = openLoop;

  // Record the dispense before any water moves, so a power cut mid-dose is accounted for.
  dispenseInProgress_ = true;
  inProgressCh_ = req.channel;
  inProgressMl_ = ml;
  persist();

  doseKind_ = kind;
  dose_.start(req, last_.monoMs, req.closedLoop, last_.scaleGrams);
}

void Engine::stepDose(const Inputs& in) {
  if (dose_.idle()) return;
  dose_.tick(in.monoMs, in.scaleOk && settings_.reservoir.scaleEnabled, in.scaleGrams);
  if (!dose_.finished()) return;

  const int c = dose_.request().channel;
  const float ml = dose_.dispensedMl();
  const DoseFault fault = dose_.fault();
  Channel& chn = ch_[c];

  chn.runtime.mlToday += ml;
  mlTodayTotal_ += ml;
  if (dose_.request().closedLoop) reservoir_.accountDispensed(ml);
  chn.lastDoseEndMs = in.monoMs;
  dispenseInProgress_ = false;

  lastDose_.valid = true;
  lastDose_.channel = static_cast<uint8_t>(c);
  lastDose_.plantId = plantAt(c) ? plantAt(c)->id : std::string();
  lastDose_.ml = ml;
  lastDose_.onTimeMs = dose_.onTimeMs();
  lastDose_.closedLoop = dose_.request().closedLoop;
  lastDose_.at = in.now;

  if (doseKind_ == SessionKind::Manual) {
    if (ml > 0) {
      if (in.timeValid) chn.runtime.lastSessionEndS = in.now;
      chn.sessionMono = true;
      chn.lastSessionMonoMs = in.monoMs;
    }
    std::string detail = doseTimed_ ? fmt("Timed outlet run (%.1f s)", dose_.onTimeMs() / 1000.0f)
                                    : std::string("Manual watering");
    if (fault != DoseFault::None) detail += std::string(" stopped: ") + doseFaultName(fault);
    event(EventType::Watering, c, doseTimed_ ? "timed" : "manual", ml, detail);
  } else if (fault == DoseFault::None) {
    chn.planner.onDoseFinished(ml, in.monoMs, plantAt(c) ? plantAt(c)->soakMin : 10);
  } else {
    chn.planner.onDoseFailed(in.monoMs);
  }

  const float requested = dose_.request().ml;
  dose_.acknowledge();

  if (fault == DoseFault::NoFlow) {
    chn.runtime.lockedOut = true;
    raise(AlertCode::OutletNoFlow, Severity::Warning, c,
          fmt("No water left the reservoir for %s (%.0f of %.0f ml). Check the tubing, pump or valve. The outlet "
              "is locked until you clear it.",
              plantLabel(c).c_str(), ml, requested));
  } else if (fault == DoseFault::StuckOpen) {
    chn.runtime.lockedOut = true;
    raise(AlertCode::OutletStuckOpen, Severity::Critical, c,
          fmt("Water kept flowing after %s's outlet closed: a stuck valve or a siphon. All watering stopped.",
              plantLabel(c).c_str()));
    emergencyStop("outlet stuck open");
  }
  persist();
}

// ------------------------------------------------------------------ planning

float Engine::secondsSinceLastSession(int c, const Inputs& in) const {
  const Channel& chn = ch_[c];
  if (chn.sessionMono) return (in.monoMs - chn.lastSessionMonoMs) / 1000.0f;
  if (chn.runtime.lastSessionEndS > 0) {
    if (in.timeValid) return static_cast<float>(std::max<EpochS>(0, in.now - chn.runtime.lastSessionEndS));
    return in.monoMs / 1000.0f;  // wall time unknown: assume it happened just before boot
  }
  return 1e9f;
}

void Engine::stepPlanners(const Inputs& in) {
  for (int k = 0; k < kMaxPlants; ++k) {
    const int c = (rr_ + k) % kMaxPlants;
    Channel& chn = ch_[c];
    if (chn.plant < 0) continue;
    const PlantProfile& p = plants_[chn.plant];

    // A session may have ended in stepDose or a command; account for it before the
    // planner decides anything, or it would not see the watering it just did.
    SessionReport r;
    if (chn.planner.takeReport(r)) handleReport(c, r, in);

    PlannerInput pin;
    pin.monoMs = in.monoMs;
    pin.sensorUsable = sensorUsable(c, in);
    // Timed fallback only for a confirmed hardware fault: not while waiting for the first
    // reading, and not after "no response" (the water may not be reaching the pot).
    const SensorIssue issue = chn.monitor.issue();
    pin.fallbackAllowed = issue == SensorIssue::BusError || issue == SensorIssue::Disconnected ||
                          issue == SensorIssue::OutOfRange;
    pin.moisturePct = chn.moisturePct;
    pin.trendPctPerMin = chn.trendPctPerMin;
    pin.adjust = computeAdjust(p, in.env, in.localMonth, settings_.southernHemisphere);
    pin.inWindow = !in.timeValid || hourInWindow(in.localHour, settings_.waterWindowStartH, settings_.waterWindowEndH);
    pin.secondsSinceLastSession = secondsSinceLastSession(c, in);
    float sinceFallback = 1e9f;
    if (chn.runtime.lastFallbackS > 0 && in.timeValid) {
      sinceFallback = static_cast<float>(in.now - chn.runtime.lastFallbackS);
    }
    pin.secondsSinceLastFallback = std::min(sinceFallback, pin.secondsSinceLastSession);

    if (settings_.automationEnabled && !emergencyStop_) chn.planner.tick(p, chn.runtime, pin);

    if (chn.planner.state() == PlannerState::WantPulse && dose_.idle()) {
      DispenseRequest req;
      req.kind = chn.planner.kind();
      req.continuation = chn.planner.continuation();
      req.ml = chn.planner.pendingMl();
      req.secondsSinceLastSession = pin.secondsSinceLastSession;
      const Verdict v = evaluateDispense(settings_.limits, globalState(), p, chn.runtime, chn.monitor.health(), req);
      if (v.allowed) {
        startDose(c, v.ml, req.kind);
        chn.planner.onDoseStarted();
        chn.lastLoggedBlock = BlockReason::None;
        rr_ = (c + 1) % kMaxPlants;
      } else {
        chn.planner.onBlocked(in.monoMs);
        if (v.reason != chn.lastLoggedBlock) {
          event(EventType::System, c, "blocked", 0,
                fmt("%s: watering skipped (%s)", plantLabel(c).c_str(), blockReasonName(v.reason)));
          chn.lastLoggedBlock = v.reason;
        }
        if (v.reason == BlockReason::PlantDailyLimit || v.reason == BlockReason::TotalDailyLimit) {
          raise(AlertCode::DailyLimitReached, Severity::Info, c,
                fmt("%s reached its daily watering limit.", plantLabel(c).c_str()));
        }
      }
    }

    if (chn.planner.takeReport(r)) handleReport(c, r, in);
  }
}

void Engine::handleReport(int c, const SessionReport& r, const Inputs& in) {
  Channel& chn = ch_[c];
  const PlantProfile* p = plantAt(c);
  if (!p) return;

  if (r.ml > 0) {
    if (in.timeValid) {
      chn.runtime.lastSessionEndS = in.now;
      if (r.kind == SessionKind::Fallback) chn.runtime.lastFallbackS = in.now;
    }
    chn.sessionMono = true;
    chn.lastSessionMonoMs = in.monoMs;
    std::string detail =
        r.kind == SessionKind::Fallback
            ? std::string("Fallback schedule (moisture sensor unavailable)")
            : fmt("%d pulse%s, %.0f%% -> %.0f%%, %s", r.pulses, r.pulses == 1 ? "" : "s", r.startPct, r.endPct,
                  sessionEndName(r.end));
    event(EventType::Watering, c, sessionKindName(r.kind), r.ml, detail);
  }

  switch (r.end) {
    case SessionEnd::TargetReached:
      clear(AlertCode::TargetNotReached, c);
      break;
    case SessionEnd::LimitReached:
      if (r.endPct < p->moisture.min) {
        raise(AlertCode::TargetNotReached, Severity::Warning, c,
              fmt("%s only reached %.0f%% after %.0f ml (target %.0f%%). Check the sensor position and drainage, "
                  "or raise its limits.",
                  plantLabel(c).c_str(), r.endPct, r.ml, sessionTargetPct(*p)));
      }
      break;
    case SessionEnd::NoResponse:
      chn.monitor.reportNoResponse(r.endPct);
      raise(AlertCode::SensorNoResponse, Severity::Warning, c,
            fmt("%s's moisture did not rise after %.0f ml. Check that the sensor is in the soil and the water "
                "reaches the pot. Automatic watering is paused for this plant until the reading rises or you "
                "clear it.",
                plantLabel(c).c_str(), r.ml));
      break;
    default:
      break;
  }
  persist();
}

// ------------------------------------------------------------------ commands

std::string Engine::manualWater(const std::string& plantId, float ml) { return manualDose(plantId, ml, false); }

std::string Engine::timedRun(const std::string& plantId, float seconds) {
  const int idx = plantIndex(plantId);
  if (idx < 0) return "not_found";
  if (!(seconds >= 1 && seconds <= 60)) return "invalid_amount";
  return manualDose(plantId, seconds * std::max(plants_[idx].outlet.flowMlPerS, 0.05f), true);
}

std::string Engine::manualDose(const std::string& plantId, float ml, bool timed) {
  const int idx = plantIndex(plantId);
  if (idx < 0) return "not_found";
  const int c = plants_[idx].channel;
  if (c >= kMaxPlants) return "not_found";
  if (!dose_.idle()) return "busy";
  Channel& chn = ch_[c];
  if (chn.planner.state() != PlannerState::Idle) chn.planner.abort(last_.monoMs);

  DispenseRequest req;
  req.kind = SessionKind::Manual;
  req.ml = ml;
  const Verdict v = evaluateDispense(settings_.limits, globalState(), plants_[idx], chn.runtime, chn.monitor.health(), req);
  if (!v.allowed) return blockReasonName(v.reason);
  startDose(c, v.ml, SessionKind::Manual, timed);
  return "";
}

void Engine::emergencyStop(const std::string& why) {
  outlets_.closeAll();
  if (!dose_.idle()) dose_.abort(last_.monoMs);
  for (auto& c : ch_) c.planner.abort(last_.monoMs);
  if (emergencyStop_) return;
  emergencyStop_ = true;
  raise(AlertCode::EmergencyStop, Severity::Info, -1, "Emergency stop (" + why + "). Watering is disabled until you resume.");
  event(EventType::System, -1, "emergency_stop", 0, why);
  persist();
}

std::string Engine::resume() {
  if (settings_.leakSensorEnabled && last_.leak) return "leak_still_detected";
  emergencyStop_ = false;
  leakLatched_ = false;
  clear(AlertCode::EmergencyStop, -1);
  clear(AlertCode::ReservoirUnexplainedLoss, -1);
  event(EventType::System, -1, "resume", 0, "Watering resumed");
  persist();
  return "";
}

void Engine::setAutomation(bool enabled) {
  if (settings_.automationEnabled == enabled) return;
  settings_.automationEnabled = enabled;
  if (!enabled) {
    for (auto& c : ch_) c.planner.abort(last_.monoMs);
  }
  event(EventType::System, -1, enabled ? "automation_on" : "automation_off", 0,
        enabled ? "Automatic watering enabled" : "Automatic watering paused");
}

std::string Engine::clearLockout(const std::string& plantId) {
  const int idx = plantIndex(plantId);
  if (idx < 0) return "not_found";
  const int c = plants_[idx].channel;
  if (c >= kMaxPlants) return "not_found";
  Channel& chn = ch_[c];
  chn.runtime.lockedOut = false;
  chn.monitor.clearNoResponse();
  clear(AlertCode::OutletNoFlow, c);
  clear(AlertCode::OutletStuckOpen, c);
  clear(AlertCode::SensorNoResponse, c);
  event(EventType::System, c, "lockout_cleared", 0, plantLabel(c) + ": faults cleared by user");
  persist();
  return "";
}

void Engine::setSystemAlert(AlertCode code, Severity sev, bool active, const std::string& message) {
  if (active) {
    raise(code, sev, -1, message);
  } else {
    clear(code, -1);
  }
}

void Engine::noteManualRefill(float ml) {
  event(EventType::Refill, -1, "manual_refill", ml, "Refill recorded from the dashboard");
}

// ------------------------------------------------------------------ status

EngineStatus Engine::status() const {
  EngineStatus s;
  s.automationEnabled = settings_.automationEnabled;
  s.emergencyStop = emergencyStop_;
  s.leak = leakLatched_ || (settings_.leakSensorEnabled && last_.leak);
  s.reservoir = reservoir_.status();
  s.env = last_.env;
  s.vpdKPa = last_.env.valid ? vpdKPa(last_.env.tempC, last_.env.humidityPct) : 0;
  if (!dose_.idle()) {
    s.dose.active = true;
    s.dose.channel = dose_.request().channel;
    if (const PlantProfile* p = plantAt(s.dose.channel)) s.dose.plantId = p->id;
    s.dose.kind = doseKind_;
    s.dose.ml = dose_.request().ml;
    s.dose.dispensedMl = dose_.dispensedMl();
  }
  s.lastDose = lastDose_;
  s.mlTodayTotal = mlTodayTotal_;
  for (const auto& p : plants_) {
    if (p.channel >= kMaxPlants) continue;
    const Channel& chn = ch_[p.channel];
    if (chn.plant < 0 || plants_[chn.plant].id != p.id) continue;
    PlantStatus ps;
    ps.id = p.id;
    ps.name = p.name;
    ps.channel = p.channel;
    ps.haveMoisture = chn.haveMoisture;
    ps.moisturePct = chn.moisturePct;
    ps.raw = chn.raw;
    ps.health = chn.monitor.health();
    ps.issue = chn.monitor.issue();
    ps.planner = chn.planner.state();
    WateringAdjust adj = computeAdjust(p, last_.env, last_.localMonth, settings_.southernHemisphere);
    ps.triggerPct = triggerPct(p, adj);
    ps.targetPct = sessionTargetPct(p);
    ps.mlToday = chn.runtime.mlToday;
    ps.lastWateredAt = chn.runtime.lastSessionEndS;
    ps.lockedOut = chn.runtime.lockedOut;
    ps.mlPerPct = chn.runtime.mlPerPct;
    if (chn.planner.state() == PlannerState::Soaking) {
      ps.soakRemainingMs = std::max<MonoMs>(0, chn.planner.soakUntil() - last_.monoMs);
    }
    s.plants.push_back(ps);
  }
  s.alertsCritical = alerts_.countActive(Severity::Critical);
  s.alertsWarning = alerts_.countActive(Severity::Warning);
  s.alertsInfo = alerts_.countActive(Severity::Info);
  return s;
}

PersistedState Engine::persisted() const {
  PersistedState st;
  for (int c = 0; c < kMaxPlants; ++c) st.plants[c] = ch_[c].runtime;
  st.day = day_;
  st.mlTodayTotal = mlTodayTotal_;
  st.usagePerDayMl = reservoir_.usagePerDay();
  st.emergencyStop = emergencyStop_;
  st.dispenseInProgress = dispenseInProgress_;
  st.inProgressChannel = inProgressCh_;
  st.inProgressMl = inProgressMl_;
  return st;
}

// ------------------------------------------------------------------ helpers

void Engine::raise(AlertCode code, Severity sev, int ch, const std::string& msg) {
  if (alerts_.raise(code, sev, static_cast<int8_t>(ch), msg, last_.now)) {
    event(EventType::AlertRaised, ch, alertCodeName(code), 0, msg);
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
