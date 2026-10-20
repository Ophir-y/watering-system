// Connects the plantcore engine to the hardware. Runs the control task (sensors ->
// engine, 100 ms) and the safety task (inputs, watchdog, indicators, 50 ms).
//
// Every access to the engine goes through one mutex, so the control task, web
// handlers and network task never act on it concurrently.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "../hal/Outlets.h"
#include "../hal/Panel.h"
#include "../hal/Sensors.h"
#include "../hal/TimeKeeper.h"
#include "../storage/ConfigStore.h"
#include "../storage/LogStore.h"
#include "Engine.h"
#include "Json.h"

class Controller : public plantcore::IEngineSink {
 public:
  using Pusher = std::function<void(const char* event, const std::string& json)>;

  Controller(Outlets& outlets, MoistureBank& moisture, EnvSensors& env, Scale& scale, TimeKeeper& clock,
             ConfigStore& config, LogStore& logs, PanelInputs& inputs, Indicators& indicators)
      : outlets_(outlets), moisture_(moisture), env_(env), scale_(scale), clock_(clock), config_(config),
        logs_(logs), inputs_(inputs), indicators_(indicators), engine_(outlets, *this) {}

  void begin();
  [[noreturn]] void controlTask();
  [[noreturn]] void safetyTask();

  // Runs fn with exclusive access to the engine. Returns false on lock timeout.
  bool withEngine(const std::function<void(plantcore::Engine&)>& fn, uint32_t timeoutMs = 2000);

  // Configuration edits (persist + apply). Return "" or an error message.
  std::string savePlant(plantcore::PlantProfile& p, const std::string& replaceId);  // p receives the saved profile
  std::string deletePlant(const std::string& id);
  std::string saveSettings(const plantcore::Settings& s);
  std::string restoreConfig(const plantcore::Settings& s, const std::vector<plantcore::PlantProfile>& plants);
  plantcore::Settings settings();
  std::vector<plantcore::PlantProfile> plants();

  // Calibration helpers.
  bool readMoistureRaw(uint8_t channel, int32_t& raw);
  std::string tareScale();
  std::string calibrateScale(float grams);
  bool scaleCalibrated() const { return scale_.calibrated(); }

  // Full /api/status body.
  bool status(JsonObject out, const plantcore::SystemInfo& sys);

  void setPusher(Pusher p) { pusher_ = std::move(p); }
  void setSetupApRequest(std::function<void()> fn) { onSetupAp_ = std::move(fn); }
  void setApActive(bool on) { apActive_ = on; }
  void setMaintenance(bool on);
  bool healthy() const;
  uint32_t lastHeartbeatMs() const { return heartbeatMs_; }

  // IEngineSink
  void onEvent(const plantcore::Event& e) override;
  void onPersist(const plantcore::PersistedState& s) override;

 private:
  void applySettingsCache(const plantcore::Settings& s, const std::vector<plantcore::PlantProfile>& plants);
  plantcore::Inputs buildInputs(uint32_t nowMs);
  void sampleMoisture(uint32_t nowMs, plantcore::Inputs& in);
  void logPeriodic(const plantcore::Inputs& in);
  void flushPushes();
  std::string plantIdForChannel(int ch) const;
  int32_t scaleAverageRaw() const;
  void updateIndicators(const plantcore::Inputs& in);

  Outlets& outlets_;
  MoistureBank& moisture_;
  EnvSensors& env_;
  Scale& scale_;
  TimeKeeper& clock_;
  ConfigStore& config_;
  LogStore& logs_;
  PanelInputs& inputs_;
  Indicators& indicators_;
  plantcore::Engine engine_;
  SemaphoreHandle_t mutex_ = nullptr;

  // Moisture sampling: one channel per control tick, a full sweep every sampleIntervalS.
  uint32_t lastSweepMs_ = 0;
  int sweepChannel_ = plantcore::kMaxPlants;
  std::array<plantcore::MoistureSample, plantcore::kMaxPlants> sweep_{};
  plantcore::EnvSnapshot lastEnv_;

  // Rolling raw average of the scale (for tare/calibration).
  std::array<int32_t, 20> scaleRing_{};
  size_t scaleRingN_ = 0;
  size_t scaleRingPos_ = 0;
  uint32_t lastScaleCount_ = 0;

  int64_t lastLogTs_ = 0;
  uint32_t lastPushMs_ = 0;
  std::vector<std::pair<const char*, std::string>> pendingPushes_;
  Pusher pusher_;
  std::function<void()> onSetupAp_;

  // Copies of settings the tasks need without taking the engine lock.
  std::atomic<uint32_t> sampleIntervalMs_{60000};
  std::atomic<uint32_t> logIntervalS_{600};
  std::atomic<bool> leakEnabled_{true};
  std::atomic<bool> floatEnabled_{true};
  std::atomic<uint8_t> channelMask_{0};

  std::atomic<uint32_t> heartbeatMs_{0};
  std::atomic<bool> maintenance_{false};
  std::atomic<bool> apActive_{false};
  std::atomic<uint8_t> ledState_{0};
  std::atomic<bool> alarm_{false};
};
