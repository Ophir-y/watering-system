#include "Controller.h"

#include <esp_random.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>

#include <algorithm>

#include "config.h"

using namespace plantcore;

namespace {

class RecursiveLock {
 public:
  RecursiveLock(SemaphoreHandle_t m, uint32_t timeoutMs) : m_(m) {
    ok_ = xSemaphoreTakeRecursive(m_, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
  }
  ~RecursiveLock() {
    if (ok_) xSemaphoreGiveRecursive(m_);
  }
  bool ok() const { return ok_; }

 private:
  SemaphoreHandle_t m_;
  bool ok_;
};

bool hourInWindow(int hour, int start, int end) {
  if (start == end) return false;
  if (start < end) return hour >= start && hour < end;
  return hour >= start || hour < end;
}

std::string newPlantId(const std::string& name, const std::vector<PlantProfile>& plants) {
  std::string base;
  for (char c : name) {
    if (base.size() >= 16) break;
    if (isalnum(static_cast<unsigned char>(c))) {
      base += static_cast<char>(tolower(static_cast<unsigned char>(c)));
    } else if (!base.empty() && base.back() != '-') {
      base += '-';
    }
  }
  while (!base.empty() && base.back() == '-') base.pop_back();
  if (base.empty()) base = "plant";
  std::string id = base;
  for (int n = 2; std::any_of(plants.begin(), plants.end(), [&](const PlantProfile& p) { return p.id == id; }); ++n) {
    id = base + "-" + std::to_string(n);
  }
  return id;
}

std::string checkChannels(const std::vector<PlantProfile>& plants) {
  for (size_t i = 0; i < plants.size(); ++i) {
    for (size_t j = i + 1; j < plants.size(); ++j) {
      if (plants[i].id == plants[j].id) return "duplicate plant id " + plants[i].id;
      if (plants[i].channel == plants[j].channel) {
        return "channel " + std::to_string(plants[i].channel + 1) + " is already used by " + plants[i].name;
      }
    }
  }
  return "";
}

}  // namespace

void Controller::begin() {
  mutex_ = xSemaphoreCreateRecursiveMutex();

  Settings s;
  if (!config_.loadSettings(s)) s = Settings{};
  std::vector<PlantProfile> plants;
  config_.loadPlants(plants);
  const ConfigStore::ScaleCal cal = config_.loadScale();
  scale_.setCalibration(cal.offset, cal.countsPerGram);
  applySettingsCache(s, plants);

  engine_.configure(s, plants);
  PersistedState st;
  config_.loadState(st);
  engine_.restore(st, buildInputs(millis()));
  heartbeatMs_ = millis();
}

void Controller::applySettingsCache(const Settings& s, const std::vector<PlantProfile>& plants) {
  clock_.setTimezone(s.timezone);
  outlets_.setHardLimitMs(s.limits.hardMaxOnTimeMs);
  sampleIntervalMs_ = static_cast<uint32_t>(s.sampleIntervalS) * 1000;
  logIntervalS_ = static_cast<uint32_t>(s.logIntervalMin) * 60;
  leakEnabled_ = s.leakSensorEnabled;
  floatEnabled_ = s.reservoir.floatSwitchEnabled;
  uint8_t mask = 0;
  for (const auto& p : plants) {
    if (p.channel < kMaxPlants) mask |= static_cast<uint8_t>(1u << p.channel);
  }
  channelMask_ = mask;
}

bool Controller::withEngine(const std::function<void(Engine&)>& fn, uint32_t timeoutMs) {
  RecursiveLock lock(mutex_, timeoutMs);
  if (!lock.ok()) return false;
  fn(engine_);
  return true;
}

// ------------------------------------------------------------------ inputs

Inputs Controller::buildInputs(uint32_t nowMs) {
  Inputs in;
  in.monoMs = esp_timer_get_time() / 1000;
  in.timeValid = clock_.valid();
  if (in.timeValid) {
    in.now = clock_.now();
    const LocalTime lt = clock_.local(in.now);
    in.localHour = lt.hour;
    in.localMonth = lt.month;
    in.localDay = lt.day;
  }
  in.env = lastEnv_;
  in.scaleOk = scale_.ok(nowMs);
  in.scaleGrams = scale_.grams();
  in.floatLow = inputs_.floatLow();
  in.leak = inputs_.leak();
  return in;
}

void Controller::sampleMoisture(uint32_t nowMs, Inputs& in) {
  if (sweepChannel_ >= kMaxPlants && nowMs - lastSweepMs_ >= sampleIntervalMs_) {
    sweepChannel_ = 0;
    lastSweepMs_ = nowMs;
    lastEnv_ = env_.read();
    in.env = lastEnv_;
  }
  if (sweepChannel_ >= kMaxPlants) return;
  // Two channels per tick keeps each control tick short (~30 ms of I2C).
  const uint8_t mask = channelMask_;
  for (int k = 0; k < 2 && sweepChannel_ < kMaxPlants; ++k, ++sweepChannel_) {
    const int c = sweepChannel_;
    sweep_[c] = MoistureSample{};
    if (!(mask & (1u << c))) continue;
    int32_t raw = 0;
    const bool ok = moisture_.read(static_cast<uint8_t>(c), raw);
    sweep_[c] = MoistureSample{false, ok, ok ? raw : 0};
  }
  if (sweepChannel_ >= kMaxPlants) {
    for (int c = 0; c < kMaxPlants; ++c) {
      if (!(mask & (1u << c))) continue;
      in.moisture[c] = sweep_[c];
      in.moisture[c].fresh = true;
    }
  }
}

int32_t Controller::scaleAverageRaw() const {
  if (scaleRingN_ == 0) return 0;
  int64_t sum = 0;
  for (size_t i = 0; i < scaleRingN_; ++i) sum += scaleRing_[i];
  return static_cast<int32_t>(sum / static_cast<int64_t>(scaleRingN_));
}

// ------------------------------------------------------------------ tasks

void Controller::controlTask() {
  esp_task_wdt_add(nullptr);
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    const uint32_t nowMs = millis();
    scale_.poll(nowMs);
    if (scale_.samples() != lastScaleCount_) {
      lastScaleCount_ = scale_.samples();
      scaleRing_[scaleRingPos_] = scale_.raw();
      scaleRingPos_ = (scaleRingPos_ + 1) % scaleRing_.size();
      scaleRingN_ = std::min(scaleRingN_ + 1, scaleRing_.size());
    }

    Inputs in = buildInputs(nowMs);
    sampleMoisture(nowMs, in);  // I2C work happens outside the engine lock

    {
      RecursiveLock lock(mutex_, 500);
      if (lock.ok()) {
        if (!maintenance_) engine_.tick(in);
        if (in.timeValid && in.now - lastLogTs_ >= static_cast<int64_t>(logIntervalS_)) {
          lastLogTs_ = in.now;
          logPeriodic(in);
        }
        updateIndicators(in);
      }
    }
    flushPushes();
    heartbeatMs_ = nowMs;
    esp_task_wdt_reset();
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(config::kControlPeriodMs));
  }
}

void Controller::safetyTask() {
  esp_task_wdt_add(nullptr);
  for (;;) {
    const uint32_t now = millis();
    inputs_.poll(now);

    // Act on hard interlocks immediately; the engine latches them on its next tick.
    if (leakEnabled_ && inputs_.leak()) outlets_.closeAll();
    if (floatEnabled_ && inputs_.floatLow() && outlets_.openChannel() >= 0) outlets_.closeAll();

    // Control task watchdog: close everything if it stalls, restart if it stays stuck.
    const uint32_t silent = now - heartbeatMs_;
    if (silent > config::kControlStallCloseMs) outlets_.closeAll();
    if (silent > config::kControlStallRebootMs) {
      log_e("Control task stalled for %u ms, restarting", silent);
      esp_restart();
    }

    switch (inputs_.takePress()) {
      case PanelInputs::Press::Short:
        withEngine([](Engine& e) { e.emergencyStop("stop button pressed"); }, 200);
        indicators_.chirp(2);
        break;
      case PanelInputs::Press::Long:
        if (onSetupAp_) onSetupAp_();
        indicators_.chirp(3);
        break;
      case PanelInputs::Press::None:
        break;
    }

    indicators_.setPattern(static_cast<LedPattern>(ledState_.load()));
    indicators_.setAlarm(alarm_);
    indicators_.update(now);
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(config::kSafetyPeriodMs));
  }
}

void Controller::updateIndicators(const Inputs& in) {
  Severity sev = Severity::Info;
  const bool any = engine_.alerts().highestActive(sev, true);
  LedPattern p = LedPattern::Ok;
  if (engine_.emergencyStopped()) {
    p = LedPattern::Stopped;
  } else if (any && sev == Severity::Critical) {
    p = LedPattern::Critical;
  } else if (apActive_) {
    p = LedPattern::SetupAp;
  } else if (any && sev == Severity::Warning) {
    p = LedPattern::Warning;
  }
  ledState_ = static_cast<uint8_t>(p);
  const Settings& s = engine_.settings();
  const bool quiet = in.timeValid && hourInWindow(in.localHour, s.quietStartH, s.quietEndH);
  alarm_ = any && sev == Severity::Critical && s.buzzerEnabled && !quiet;
}

void Controller::logPeriodic(const Inputs& in) {
  const EngineStatus st = engine_.status();
  for (const auto& p : st.plants) {
    if (p.haveMoisture) logs_.reading(in.now, p.id, p.raw, p.moisturePct);
  }
  EnvRow row;
  row.ts = in.now;
  row.valid[0] = row.valid[1] = in.env.valid;
  row.values[0] = in.env.tempC;
  row.values[1] = in.env.humidityPct;
  row.valid[2] = in.env.luxValid;
  row.values[2] = in.env.lux;
  row.valid[3] = st.reservoir.scaleValid;
  row.values[3] = st.reservoir.ml;
  logs_.env(row);
}

void Controller::flushPushes() {
  std::vector<std::pair<const char*, std::string>> out;
  {
    RecursiveLock lock(mutex_, 100);
    if (!lock.ok()) return;
    out.swap(pendingPushes_);
  }
  if (!pusher_) return;
  for (const auto& p : out) pusher_(p.first, p.second);
}

bool Controller::status(JsonObject out, const SystemInfo& sys) {
  SystemInfo info = sys;
  info.scaleCalibrated = scale_.calibrated();
  info.scaleRaw = scale_.raw();
  RecursiveLock lock(mutex_, 1000);
  if (!lock.ok()) return false;
  statusToJson(engine_.status(), info, engine_.settings(), out);
  return true;
}

// ------------------------------------------------------------------ sink

std::string Controller::plantIdForChannel(int ch) const {
  for (const auto& p : engine_.plants()) {
    if (p.channel == ch) return p.id;
  }
  return "";
}

void Controller::onEvent(const Event& e) {
  const std::string plantId = e.channel >= 0 ? plantIdForChannel(e.channel) : std::string();
  logs_.event(e, plantId);
  JsonDocument doc;
  toJson(e, plantId, doc.to<JsonObject>());
  std::string json;
  serializeJson(doc, json);
  RecursiveLock lock(mutex_, 100);
  if (lock.ok() && pendingPushes_.size() < 32) pendingPushes_.emplace_back("event", json);
}

void Controller::onPersist(const PersistedState& s) { config_.saveState(s); }

// ------------------------------------------------------------------ configuration

Settings Controller::settings() {
  Settings s;
  withEngine([&](Engine& e) { s = e.settings(); });
  return s;
}

std::vector<PlantProfile> Controller::plants() {
  std::vector<PlantProfile> p;
  withEngine([&](Engine& e) { p = e.plants(); });
  return p;
}

std::string Controller::savePlant(PlantProfile& input, const std::string& replaceId) {
  std::string err;
  bool locked = withEngine([&](Engine& e) {
    std::vector<PlantProfile> plants = e.plants();
    PlantProfile p = input;
    if (replaceId.empty()) {
      if (plants.size() >= static_cast<size_t>(kMaxPlants)) {
        err = "all 8 channels are in use";
        return;
      }
      if (p.id.empty()) p.id = newPlantId(p.name, plants);
      plants.push_back(p);
    } else {
      auto it = std::find_if(plants.begin(), plants.end(), [&](const PlantProfile& x) { return x.id == replaceId; });
      if (it == plants.end()) {
        err = "not_found";
        return;
      }
      p.id = replaceId;  // ids are immutable: history is keyed by them
      *it = p;
    }
    if (!validateProfile(p, err)) return;
    err = checkChannels(plants);
    if (!err.empty()) return;
    if (!config_.savePlants(plants)) {
      err = "could not save to flash";
      return;
    }
    applySettingsCache(e.settings(), plants);
    e.configure(e.settings(), plants);
    input = p;
  });
  return locked ? err : "busy";
}

std::string Controller::deletePlant(const std::string& id) {
  std::string err;
  bool locked = withEngine([&](Engine& e) {
    std::vector<PlantProfile> plants = e.plants();
    auto it = std::find_if(plants.begin(), plants.end(), [&](const PlantProfile& p) { return p.id == id; });
    if (it == plants.end()) {
      err = "not_found";
      return;
    }
    plants.erase(it);
    if (!config_.savePlants(plants)) {
      err = "could not save to flash";
      return;
    }
    applySettingsCache(e.settings(), plants);
    e.configure(e.settings(), plants);
  });
  return locked ? err : "busy";
}

std::string Controller::saveSettings(const Settings& s) {
  std::string err;
  if (!validateSettings(s, err)) return err;
  bool locked = withEngine([&](Engine& e) {
    if (!config_.saveSettings(s)) {
      err = "could not save to flash";
      return;
    }
    applySettingsCache(s, e.plants());
    e.configure(s, e.plants());
  });
  return locked ? err : "busy";
}

std::string Controller::restoreConfig(const Settings& s, const std::vector<PlantProfile>& plants) {
  std::string err;
  if (!validateSettings(s, err)) return "settings: " + err;
  if (plants.size() > static_cast<size_t>(kMaxPlants)) return "too many plants";
  for (const auto& p : plants) {
    if (!validateProfile(p, err)) return p.id + ": " + err;
  }
  err = checkChannels(plants);
  if (!err.empty()) return err;
  bool locked = withEngine([&](Engine& e) {
    if (!config_.saveSettings(s) || !config_.savePlants(plants)) {
      err = "could not save to flash";
      return;
    }
    applySettingsCache(s, plants);
    e.configure(s, plants);
  });
  return locked ? err : "busy";
}

// ------------------------------------------------------------------ calibration

bool Controller::readMoistureRaw(uint8_t channel, int32_t& raw) { return moisture_.read(channel, raw); }

std::string Controller::tareScale() {
  if (scaleRingN_ < 10) return "the scale is not responding";
  ConfigStore::ScaleCal cal = config_.loadScale();
  cal.offset = scaleAverageRaw();
  if (!config_.saveScale(cal)) return "could not save to flash";
  scale_.setCalibration(cal.offset, cal.countsPerGram);
  return "";
}

std::string Controller::calibrateScale(float grams) {
  if (!(grams >= 100 && grams <= 50000)) return "use a known amount between 100 and 50000 ml";
  if (scaleRingN_ < 10) return "the scale is not responding";
  ConfigStore::ScaleCal cal = config_.loadScale();
  const int32_t delta = scaleAverageRaw() - cal.offset;
  if (std::abs(delta) < 1000) return "no weight change since tare: tare the empty container first, then add water";
  cal.countsPerGram = static_cast<float>(delta) / grams;
  if (!config_.saveScale(cal)) return "could not save to flash";
  scale_.setCalibration(cal.offset, cal.countsPerGram);
  return "";
}

void Controller::setMaintenance(bool on) {
  maintenance_ = on;
  if (on) outlets_.closeAll();
}

bool Controller::healthy() const { return millis() - heartbeatMs_ < 1000; }
