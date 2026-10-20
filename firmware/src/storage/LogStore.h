// History on the microSD card, with RAM rings that keep the last day available
// when the card is missing or failing.
//
//   /readings/YYYY-MM-DD.csv   moisture rows (see plantcore/History.h)
//   /env/YYYY-MM-DD.csv        environment + reservoir rows
//   /events/YYYY-MM.csv        watering, alerts, refills, system events
//   /daily/YYYY.csv            one moisture summary row per plant per day (long ranges)
//   /daily/env-YYYY.csv        one environment summary row per day
//   /backup/config-YYYY-MM-DD.json
//
// Producers only enqueue; the storage task does the writing. Queries run in the
// web server task and share the card through a mutex.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include <array>
#include <deque>
#include <string>
#include <vector>

#include "History.h"
#include "Types.h"

class TimeKeeper;

class LogStore {
 public:
  void begin(TimeKeeper& clock);
  void task();  // storage task body, never returns

  bool sdOk() const { return sdOk_; }
  float usedMb() const { return usedMb_; }
  float totalMb() const { return totalMb_; }
  uint32_t dropped() const { return dropped_; }

  // Producers (any task, never block).
  void reading(int64_t ts, const std::string& plantId, int32_t raw, float pct);
  void env(const plantcore::EnvRow& row);
  void event(const plantcore::Event& e, const std::string& plantId);
  bool writeBackup(const std::string& json);

  // Queries (web task).
  void history(const std::string& plantId, int64_t from, int64_t to, size_t points, JsonObject out);
  void envHistory(int64_t from, int64_t to, size_t points, JsonObject out);
  void events(int64_t from, int64_t to, const std::string& type, const std::string& plantId, size_t limit,
              JsonArray out);

  // CSV export: the files covering a range, read in chunks.
  std::vector<std::string> exportFiles(const std::string& kind, int64_t from, int64_t to);
  size_t readChunk(const std::string& path, size_t offset, uint8_t* buf, size_t len);

 private:
  enum class Kind : uint8_t { Reading, Env, Event };
  struct Item {
    Kind kind;
    int64_t ts;
    char plantId[25];
    int32_t raw;
    float pct;
    plantcore::EnvRow env;
    char line[240];
  };
  struct RamReading {
    uint32_t ts;
    uint8_t slot;
    int16_t pct10;
  };
  struct RamEnv {
    uint32_t ts;
    float values[plantcore::kEnvColumns];
    uint8_t valid;
  };
  struct RamEvent {
    plantcore::Event e;
    std::string plantId;
  };
  struct DayAccum {
    std::string date;
    int64_t ts = 0;
    double sum = 0;
    float min = 0, max = 0;
    uint32_t n = 0;
  };

  void enqueue(const Item& item);
  void handle(const Item& item);
  bool mount();
  bool append(const std::string& path, const char* header, const std::string& line);
  void flushDaily(const std::string& newDate, int64_t ts);
  void maintenance();
  std::vector<std::string> datesInRange(int64_t from, int64_t to) const;
  uint8_t slotFor(const std::string& plantId);

  TimeKeeper* clock_ = nullptr;
  QueueHandle_t queue_ = nullptr;
  SemaphoreHandle_t sd_ = nullptr;
  SemaphoreHandle_t ram_ = nullptr;
  volatile bool sdOk_ = false;
  float usedMb_ = 0;
  float totalMb_ = 0;
  uint32_t dropped_ = 0;
  uint32_t lastMaintenanceMs_ = 0;
  uint32_t lastMountAttemptMs_ = 0;

  std::array<std::string, 16> slots_;
  std::deque<RamReading> ramReadings_;
  std::deque<RamEnv> ramEnv_;
  std::deque<RamEvent> ramEvents_;

  // Daily summaries, accumulated by the storage task.
  std::vector<std::pair<std::string, DayAccum>> plantDays_;
  std::string envDate_;
  int64_t envDayTs_ = 0;
  double envSum_[plantcore::kEnvColumns] = {0, 0, 0, 0};
  uint32_t envN_[plantcore::kEnvColumns] = {0, 0, 0, 0};
};
