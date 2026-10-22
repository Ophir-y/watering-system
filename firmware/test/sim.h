// Test doubles and a small physical simulation of one plant node: a plant in its pot,
// a bottle with a submersible pump, and 3xAA batteries.
#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "Engine.h"
#include "Types.h"

namespace sim {

using namespace plantcore;

struct RecordingSink : IEngineSink {
  std::vector<Event> events;
  PersistedState last;
  int persists = 0;
  void onEvent(const Event& e) override { events.push_back(e); }
  void onPersist(const PersistedState& s) override {
    last = s;
    persists++;
  }
  int count(EventType t, const std::string& code = "") const {
    int n = 0;
    for (const auto& e : events) {
      if (e.type == t && (code.empty() || e.code == code)) n++;
    }
    return n;
  }
  float wateredMl() const {
    float ml = 0;
    for (const auto& e : events) {
      if (e.type == EventType::Watering) ml += e.ml;
    }
    return ml;
  }
};

struct SimPlant {
  float moisture = 50;        // true moisture on the sensor's calibrated scale
  float dryPctPerHour = 0.4f; // evapotranspiration
  float mlPerPct = 10;        // ml of water for +1 %
  float pendingMl = 0;        // water poured but not yet reached the sensor
  float absorbPerMin = 0.15f; // fraction of pending water reaching the sensor per minute
  bool sensorInSoil = true;
  bool waterReachesPot = true;  // false: the tube slipped out of the pot
  bool sensorUnplugged = false;
  SensorCalibration cal{21000, 10000};

  int32_t raw() const {
    if (sensorUnplugged) return 0;
    float pct = sensorInSoil ? moisture : 0;  // in air the probe reads its dry point
    return cal.rawDry + static_cast<int32_t>((cal.rawWet - cal.rawDry) * pct / 100.0f);
  }
};

// The world implements IPump so the engine's actuation moves simulated water.
struct World : IPump {
  static constexpr EpochS kEpoch0 = 1767225600;  // 2026-01-01 00:00 UTC

  SimPlant plant;
  float flowMlPerS = 2.0f;     // the pump's real flow
  float bottleMl = 1000;       // water really in the bottle
  float intakeMl = 30;         // below this the pump sucks air
  bool pumpOn = false;
  bool pumpUnplugged = false;  // no current, no water
  bool pumpJammed = false;     // high current, no water
  float wetMa = 180;           // pump current at 4.5 V, pushing water...
  float dryMa = 95;            // ...and sucking air
  bool currentSensor = true;   // the node reports the pump current
  float batteryMv = 4500;
  float batterySagMv = 300;    // drop while the pump runs
  MonoMs now = 0;
  MonoMs lastSampleMs = -1000000;
  int sampleIntervalS = 60;
  int startHour = 9;           // simulated wall clock starts at this hour
  bool clockValid = true;
  EnvSnapshot env{true, 22, 55, true, 6000};
  float dispensedTotal = 0;

  void on() override { pumpOn = true; }
  void off() override { pumpOn = false; }

  EpochS epoch() const { return kEpoch0 + startHour * 3600 + now / 1000; }
  Settings settings() const { return Settings{}; }

  bool moving() const { return pumpOn && !pumpUnplugged && !pumpJammed && bottleMl > intakeMl; }

  float pumpCurrentMa() const {
    if (!pumpOn || pumpUnplugged) return pumpOn ? 2 : 0;
    const float scale = (batteryMv - batterySagMv) / 4500.0f;
    if (pumpJammed) return 2.6f * wetMa * scale;
    return (bottleMl > intakeMl ? wetMa : dryMa) * scale;
  }

  void advance(MonoMs dtMs) {
    const float dtS = dtMs / 1000.0f;
    if (moving()) {
      const float ml = std::min(flowMlPerS * dtS, bottleMl - intakeMl);
      bottleMl -= ml;
      dispensedTotal += ml;
      if (plant.waterReachesPot) plant.pendingMl += ml;
    }
    SimPlant& p = plant;
    p.moisture -= p.dryPctPerHour * dtS / 3600.0f;
    const float absorbed = p.pendingMl * std::min(1.0f, p.absorbPerMin * dtS / 60.0f);
    p.pendingMl -= absorbed;
    p.moisture = std::clamp(p.moisture + absorbed / p.mlPerPct, 0.0f, 100.0f);
    now += dtMs;
  }

  Inputs inputs() {
    Inputs in;
    in.monoMs = now;
    in.timeValid = clockValid;
    in.now = clockValid ? epoch() : 0;
    in.localHour = static_cast<int>((epoch() / 3600) % 24);
    in.localMonth = 6;
    in.localDay = static_cast<int32_t>(epoch() / 86400);
    const bool fresh = now - lastSampleMs >= sampleIntervalS * 1000LL;
    if (fresh) lastSampleMs = now;
    in.moisture.fresh = fresh;
    in.moisture.ok = true;
    in.moisture.raw = plant.raw();
    in.env = env;
    in.batteryMv = pumpOn ? batteryMv - batterySagMv : batteryMv;
    in.pumpSense = currentSensor && pumpOn;
    in.pumpMa = pumpCurrentMa();
    return in;
  }
};

inline PlantProfile makePlant(const std::string& id) {
  PlantProfile p;
  p.id = id;
  p.name = id;
  p.potVolumeMl = 2000;
  p.moisture = {20, 35, 55, 75};
  p.minIntervalH = 6;
  p.maxDailyMl = 600;
  p.maxPulseMl = 120;
  p.soakMin = 10;
  p.pump.flowMlPerS = 2.0f;
  p.bottle.capacityMl = 1000;
  p.bottle.lowPct = 25;
  p.bottle.reserveMl = 50;
  p.cal = {21000, 10000};
  p.tempC = {10, 35};
  p.humidityPct = {20, 90};
  return p;
}

// Steps world + engine together: fine steps while the pump runs.
inline void run(World& w, Engine& e, MonoMs durationMs, MonoMs coarseMs = 10000) {
  const MonoMs end = w.now + durationMs;
  while (w.now < end) {
    e.tick(w.inputs());
    const bool busy = e.doseActive() || w.pumpOn;
    w.advance(busy ? 100 : coarseMs);
  }
}

}  // namespace sim
