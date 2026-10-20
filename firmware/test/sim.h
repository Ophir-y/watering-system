// Test doubles and a small physical simulation used by the engine and scenario tests.
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
  float wateredMl(int channel = -1) const {
    float ml = 0;
    for (const auto& e : events) {
      if (e.type == EventType::Watering && (channel < 0 || e.channel == channel)) ml += e.ml;
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
  bool waterReachesPot = true;
  bool sensorUnplugged = false;
  SensorCalibration cal{21000, 10000};

  int32_t raw() const {
    if (sensorUnplugged) return 0;
    float pct = sensorInSoil ? moisture : 0;  // in air the probe reads its dry point
    return cal.rawDry + static_cast<int32_t>((cal.rawWet - cal.rawDry) * pct / 100.0f);
  }
};

// The world implements IOutlets so the engine's actuation moves simulated water.
struct World : IOutlets {
  static constexpr EpochS kEpoch0 = 1767225600;  // 2026-01-01 00:00 UTC

  std::vector<SimPlant> plants;
  std::vector<float> flowMlPerS;   // actual flow per channel
  std::vector<bool> outletBroken;  // no flow even when open
  std::vector<bool> stuckOpen;     // flows even when closed
  int openChannel = -1;
  float reservoirMl = 4000;
  bool gravity = false;            // flow scales with sqrt(head)
  float dropMm = 300, fullHeightMm = 250, capacityMl = 5000;
  bool leak = false;
  bool scaleOk = true;
  MonoMs now = 0;
  MonoMs lastSampleMs = -1000000;
  int sampleIntervalS = 60;
  int startHour = 9;               // simulated wall clock starts at this hour
  EnvSnapshot env{true, 22, 55, true, 6000};
  float dispensedTotal = 0;
  int maxOnSeen = 0;

  explicit World(int n = 1) {
    plants.resize(n);
    flowMlPerS.assign(n, 2.0f);
    outletBroken.assign(n, false);
    stuckOpen.assign(n, false);
  }

  void open(uint8_t ch) override { openChannel = ch; }
  void closeAll() override { openChannel = -1; }

  EpochS epoch() const { return kEpoch0 + startHour * 3600 + now / 1000; }

  float channelFlow(int c) const {
    float f = flowMlPerS[c];
    if (gravity) {
      float head = dropMm + reservoirMl / capacityMl * fullHeightMm;
      f *= std::sqrt(head / (dropMm + fullHeightMm));
    }
    return f;
  }

  void advance(MonoMs dtMs) {
    const float dtS = dtMs / 1000.0f;
    for (size_t c = 0; c < plants.size(); ++c) {
      bool flowing = (static_cast<int>(c) == openChannel && !outletBroken[c]) || stuckOpen[c];
      if (flowing && reservoirMl > 0) {
        float ml = std::min(channelFlow(static_cast<int>(c)) * dtS, reservoirMl);
        reservoirMl -= ml;
        dispensedTotal += ml;
        if (plants[c].waterReachesPot) plants[c].pendingMl += ml;
      }
    }
    for (auto& p : plants) {
      p.moisture -= p.dryPctPerHour * dtS / 3600.0f;
      float absorbed = p.pendingMl * std::min(1.0f, p.absorbPerMin * dtS / 60.0f);
      p.pendingMl -= absorbed;
      p.moisture = std::clamp(p.moisture + absorbed / p.mlPerPct, 0.0f, 100.0f);
    }
    now += dtMs;
  }

  Inputs inputs() {
    Inputs in;
    in.monoMs = now;
    in.timeValid = true;
    in.now = epoch();
    in.localHour = static_cast<int>((in.now / 3600) % 24);
    in.localMonth = 6;
    in.localDay = static_cast<int32_t>(in.now / 86400);
    const bool fresh = now - lastSampleMs >= sampleIntervalS * 1000LL;
    if (fresh) lastSampleMs = now;
    for (size_t c = 0; c < plants.size() && c < static_cast<size_t>(kMaxPlants); ++c) {
      in.moisture[c].fresh = fresh;
      in.moisture[c].ok = true;
      in.moisture[c].raw = plants[c].raw();
    }
    in.env = env;
    in.scaleOk = scaleOk;
    in.scaleGrams = reservoirMl;
    in.floatLow = reservoirMl < 100;
    in.leak = leak;
    return in;
  }
};

inline PlantProfile makePlant(const std::string& id, uint8_t channel) {
  PlantProfile p;
  p.id = id;
  p.name = id;
  p.channel = channel;
  p.potVolumeMl = 2000;
  p.moisture = {20, 35, 55, 75};
  p.minIntervalH = 6;
  p.maxDailyMl = 600;
  p.maxPulseMl = 120;
  p.soakMin = 10;
  p.outlet.flowMlPerS = 2.0f;
  p.cal = {21000, 10000};
  p.tempC = {10, 35};
  p.humidityPct = {20, 90};
  return p;
}

// Steps world + engine together: fine steps while water may be moving.
inline void run(World& w, Engine& e, MonoMs durationMs, MonoMs coarseMs = 10000) {
  const MonoMs end = w.now + durationMs;
  while (w.now < end) {
    e.tick(w.inputs());
    bool busy = e.doseActive() || w.openChannel >= 0;
    w.advance(busy ? 250 : coarseMs);
  }
}

}  // namespace sim
