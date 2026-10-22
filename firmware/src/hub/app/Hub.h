// The main station app: the plantlink Station core plus flash, the SD log, the
// clock, the room climate and live pushes to the dashboard.
//
// Every access to the station core goes through one mutex, so the radio task, web
// handlers and network task never act on it at the same time.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "../hal/Panel.h"
#include "../hal/Sensors.h"
#include "../hal/TimeKeeper.h"
#include "../storage/ConfigStore.h"
#include "../storage/LogStore.h"
#include "Station.h"

class Hub : public plantlink::IStationSink {
 public:
  using Pusher = std::function<void(const char* event, const std::string& json)>;

  Hub(ConfigStore& config, LogStore& logs, TimeKeeper& clock, EnvSensors& env)
      : config_(config), logs_(logs), clock_(clock), env_(env), station_(*this) {}

  void begin();
  // Runs fn with exclusive access to the station core. Returns false on lock timeout.
  bool with(const std::function<void(plantlink::Station&)>& fn, uint32_t timeoutMs = 2000);

  // Radio task: a frame from a node in, the reply out (0 = none).
  size_t onFrame(const plantlink::Mac& from, const uint8_t* d, size_t n, uint8_t* reply);
  // Network task, about once a second.
  void tick(uint32_t nowMs, uint8_t radioChannel, bool apActive);
  void flushPushes();
  bool status(JsonObject out, const plantlink::SystemInfo& sys);

  plantcore::Settings settings();
  std::vector<plantcore::PlantProfile> plants();
  LedPattern ledPattern() const { return static_cast<LedPattern>(led_.load()); }
  void setPusher(Pusher p) { pusher_ = std::move(p); }

  // IStationSink
  void onEvent(const plantcore::Event& e, const std::string& plantId) override;
  void onReading(const plantcore::ReadingRow& r) override;
  bool persistPlants(const std::vector<plantcore::PlantProfile>& plants) override;
  bool persistSettings(const plantcore::Settings& s) override;
  bool persistNodes(const std::vector<plantlink::NodeRecord>& nodes) override;
  bool persistStopped(bool stopped) override;
  plantlink::Key newKey() override;

 private:
  plantlink::ClockInfo clockInfo() const;
  void logEnvironment(const plantcore::EnvSnapshot& env, int64_t logEveryS);

  ConfigStore& config_;
  LogStore& logs_;
  TimeKeeper& clock_;
  EnvSensors& env_;
  plantlink::Station station_;
  SemaphoreHandle_t mutex_ = nullptr;

  uint32_t lastEnvMs_ = 0;
  int64_t lastEnvLogTs_ = 0;
  std::vector<std::pair<const char*, std::string>> pendingPushes_;
  Pusher pusher_;
  std::atomic<uint8_t> led_{0};
};
