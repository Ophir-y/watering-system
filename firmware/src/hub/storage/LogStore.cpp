#include "LogStore.h"

#include <FS.h>
#include <SD.h>
#include <SPI.h>

#include <algorithm>
#include <cstring>

#include "../hal/TimeKeeper.h"
#include "Json.h"
#include "config.h"
#include "pins.h"

using plantcore::EnvRow;
using plantcore::kEnvColumns;

namespace {

class Lock {
 public:
  Lock(SemaphoreHandle_t m, uint32_t timeoutMs) : m_(m) { ok_ = xSemaphoreTake(m_, pdMS_TO_TICKS(timeoutMs)) == pdTRUE; }
  ~Lock() {
    if (ok_) xSemaphoreGive(m_);
  }
  bool ok() const { return ok_; }

 private:
  SemaphoreHandle_t m_;
  bool ok_;
};

// Calls fn(line) for every line of a file, reading in blocks.
template <typename F>
void forEachLine(const std::string& path, F fn) {
  File f = SD.open(path.c_str(), FILE_READ);
  if (!f) return;
  uint8_t block[512];
  std::string line;
  line.reserve(160);
  int n;
  while ((n = f.read(block, sizeof block)) > 0) {
    for (int i = 0; i < n; ++i) {
      if (block[i] == '\n') {
        fn(line);
        line.clear();
      } else if (block[i] != '\r') {
        line += static_cast<char>(block[i]);
      }
    }
  }
  if (!line.empty()) fn(line);
  f.close();
}

void copyStr(char* dst, size_t size, const std::string& src) {
  strncpy(dst, src.c_str(), size - 1);
  dst[size - 1] = '\0';
}

}  // namespace

void LogStore::begin(TimeKeeper& clock) {
  clock_ = &clock;
  queue_ = xQueueCreate(32, sizeof(Item));
  sd_ = xSemaphoreCreateMutex();
  ram_ = xSemaphoreCreateMutex();
  SPI.begin(pins::kSdSck, pins::kSdMiso, pins::kSdMosi, pins::kSdCs);
  mount();
}

bool LogStore::mount() {
  Lock lock(sd_, 2000);
  if (!lock.ok()) return false;
  lastMountAttemptMs_ = millis();
  SD.end();
  if (!SD.begin(pins::kSdCs, SPI, 20000000, "/sd", 5)) {
    sdOk_ = false;
    return false;
  }
  for (const char* dir : {"/readings", "/env", "/events", "/daily", "/backup"}) {
    if (!SD.exists(dir)) SD.mkdir(dir);
  }
  totalMb_ = SD.totalBytes() / 1048576.0f;
  usedMb_ = SD.usedBytes() / 1048576.0f;
  sdOk_ = true;
  return true;
}

uint8_t LogStore::slotFor(const std::string& plantId) {
  for (size_t i = 0; i < slots_.size(); ++i) {
    if (slots_[i] == plantId) return static_cast<uint8_t>(i);
  }
  for (size_t i = 0; i < slots_.size(); ++i) {
    if (slots_[i].empty()) {
      slots_[i] = plantId;
      return static_cast<uint8_t>(i);
    }
  }
  slots_.back() = plantId;
  return static_cast<uint8_t>(slots_.size() - 1);
}

// ------------------------------------------------------------------ producers

void LogStore::enqueue(const Item& item) {
  if (!queue_ || xQueueSend(queue_, &item, 0) != pdTRUE) dropped_++;
}

void LogStore::reading(const plantcore::ReadingRow& row) {
  {
    Lock lock(ram_, 50);
    if (lock.ok()) {
      const uint16_t bottle = row.bottleMl >= 0 ? static_cast<uint16_t>(std::min(row.bottleMl, 65000.0f)) : 0xFFFF;
      ramReadings_.push_back(RamReading{static_cast<uint32_t>(row.ts), slotFor(row.plantId),
                                        static_cast<int16_t>(row.pct * 10), bottle});
      while (ramReadings_.size() > config::kRamReadingsPerPlant * plantcore::kMaxNodes) ramReadings_.pop_front();
    }
  }
  Item item{};
  item.kind = Kind::Reading;
  item.ts = row.ts;
  copyStr(item.plantId, sizeof item.plantId, row.plantId);
  item.raw = row.raw;
  item.pct = row.pct;
  item.bottleMl = row.bottleMl;
  item.batteryMv = row.batteryMv;
  enqueue(item);
}

void LogStore::env(const EnvRow& row) {
  {
    Lock lock(ram_, 50);
    if (lock.ok()) {
      RamEnv r{static_cast<uint32_t>(row.ts), {}, 0};
      for (int i = 0; i < kEnvColumns; ++i) {
        r.values[i] = row.values[i];
        if (row.valid[i]) r.valid |= 1 << i;
      }
      ramEnv_.push_back(r);
      while (ramEnv_.size() > config::kRamEnvRows) ramEnv_.pop_front();
    }
  }
  Item item{};
  item.kind = Kind::Env;
  item.ts = row.ts;
  item.env = row;
  enqueue(item);
}

void LogStore::event(const plantcore::Event& e, const std::string& plantId) {
  {
    Lock lock(ram_, 50);
    if (lock.ok()) {
      ramEvents_.push_back(RamEvent{e, plantId});
      while (ramEvents_.size() > config::kRamEvents) ramEvents_.pop_front();
    }
  }
  Item item{};
  item.kind = Kind::Event;
  item.ts = e.ts;
  copyStr(item.line, sizeof item.line, plantcore::formatEvent(e, plantId));
  enqueue(item);
}

bool LogStore::writeBackup(const std::string& json) {
  if (!sdOk_ || !clock_) return false;
  Lock lock(sd_, 2000);
  if (!lock.ok()) return false;
  std::string path = std::string("/backup/config-") + clock_->local(clock_->now()).date + ".json";
  File f = SD.open(path.c_str(), FILE_WRITE);
  if (!f) return false;
  bool ok = f.write(reinterpret_cast<const uint8_t*>(json.data()), json.size()) == json.size();
  f.close();
  return ok;
}

// ------------------------------------------------------------------ storage task

void LogStore::task() {
  for (;;) {
    Item item;
    if (xQueueReceive(queue_, &item, pdMS_TO_TICKS(1000)) == pdTRUE) handle(item);
    const uint32_t now = millis();
    if (!sdOk_ && now - lastMountAttemptMs_ > 60000) mount();
    if (now - lastMaintenanceMs_ > 3600000UL) {
      lastMaintenanceMs_ = now;
      maintenance();
    }
  }
}

bool LogStore::append(const std::string& path, const char* header, const std::string& line) {
  if (!sdOk_) return false;
  Lock lock(sd_, 2000);
  if (!lock.ok()) return false;
  File f = SD.open(path.c_str(), FILE_APPEND);
  if (!f) {
    sdOk_ = false;
    return false;
  }
  bool ok = true;
  if (f.size() == 0) ok = f.printf("%s\n", header) > 0;
  ok = ok && f.printf("%s\n", line.c_str()) > 0;
  f.close();
  if (!ok) sdOk_ = false;
  return ok;
}

void LogStore::handle(const Item& item) {
  const LocalTime lt = clock_->local(item.ts);
  const std::string date = lt.date;
  switch (item.kind) {
    case Kind::Reading: {
      // Daily summary: close out the previous day for this plant first.
      auto it = std::find_if(plantDays_.begin(), plantDays_.end(),
                             [&](const auto& p) { return p.first == item.plantId; });
      if (it == plantDays_.end()) {
        plantDays_.push_back({item.plantId, DayAccum{}});
        it = plantDays_.end() - 1;
      }
      DayAccum& acc = it->second;
      if (acc.n > 0 && acc.date != date) {
        plantcore::ReadingRow row{acc.ts, it->first, 0, static_cast<float>(acc.sum / acc.n),
                                  acc.bottleN ? static_cast<float>(acc.bottleSum / acc.bottleN) : -1.0f, -1.0f};
        append("/daily/" + acc.date.substr(0, 4) + ".csv", plantcore::kReadingsHeader, plantcore::formatReading(row));
        acc = DayAccum{};
      }
      if (acc.n == 0) {
        acc.date = date;
        acc.ts = item.ts;
      }
      acc.sum += item.pct;
      acc.n++;
      if (item.bottleMl >= 0) {
        acc.bottleSum += item.bottleMl;
        acc.bottleN++;
      }
      plantcore::ReadingRow row{item.ts, item.plantId, item.raw, item.pct, item.bottleMl, item.batteryMv};
      append("/readings/" + date + ".csv", plantcore::kReadingsHeader, plantcore::formatReading(row));
      break;
    }
    case Kind::Env: {
      flushDaily(date, item.ts);
      for (int i = 0; i < kEnvColumns; ++i) {
        if (!item.env.valid[i]) continue;
        envSum_[i] += item.env.values[i];
        envN_[i]++;
      }
      append("/env/" + date + ".csv", plantcore::kEnvHeader, plantcore::formatEnv(item.env));
      break;
    }
    case Kind::Event:
      append("/events/" + date.substr(0, 7) + ".csv", plantcore::kEventsHeader, item.line);
      break;
  }
}

void LogStore::flushDaily(const std::string& newDate, int64_t ts) {
  if (envDate_ == newDate) return;
  bool any = false;
  EnvRow row;
  row.ts = envDayTs_;
  for (int i = 0; i < kEnvColumns; ++i) {
    row.valid[i] = envN_[i] > 0;
    row.values[i] = envN_[i] ? static_cast<float>(envSum_[i] / envN_[i]) : 0;
    any = any || row.valid[i];
    envSum_[i] = 0;
    envN_[i] = 0;
  }
  if (any && !envDate_.empty()) {
    append("/daily/env-" + envDate_.substr(0, 4) + ".csv", plantcore::kEnvHeader, plantcore::formatEnv(row));
  }
  envDate_ = newDate;
  envDayTs_ = ts;
}

void LogStore::maintenance() {
  if (!sdOk_) return;
  Lock lock(sd_, 5000);
  if (!lock.ok()) return;
  usedMb_ = SD.usedBytes() / 1048576.0f;
  // Prune the oldest raw days when the card fills up (daily summaries are kept).
  for (int pass = 0; pass < 30 && totalMb_ - usedMb_ < config::kSdMinFreeMb; ++pass) {
    File dir = SD.open("/readings");
    std::string oldest;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
      std::string name = f.name();
      if (oldest.empty() || name < oldest) oldest = name;
    }
    dir.close();
    if (oldest.empty()) break;
    SD.remove(("/readings/" + oldest).c_str());
    SD.remove(("/env/" + oldest).c_str());
    usedMb_ = SD.usedBytes() / 1048576.0f;
  }
}

// ------------------------------------------------------------------ queries

std::vector<std::string> LogStore::datesInRange(int64_t from, int64_t to) const {
  std::vector<std::string> dates;
  for (int64_t t = from; t <= to + 86400; t += 86400) {
    std::string d = clock_->local(std::min(t, to)).date;
    if (dates.empty() || dates.back() != d) dates.push_back(d);
    if (t >= to) break;
  }
  return dates;
}

void LogStore::history(const std::string& plantId, int64_t from, int64_t to, size_t points, JsonObject out) {
  points = std::clamp<size_t>(points, 10, 300);
  plantcore::Downsampler ds(from, to, points, 2);  // moisture %, bottle ml
  const bool longRange = to - from > config::kLongRangeDays * 86400LL;
  bool fromCard = false;
  if (sdOk_) {
    Lock lock(sd_, 3000);
    if (lock.ok()) {
      fromCard = true;
      auto add = [&](const std::string& line) {
        plantcore::ReadingRow r;
        if (plantcore::parseReading(line, r) && r.plantId == plantId) {
          const float v[2] = {r.pct, r.bottleMl};
          const bool ok[2] = {true, r.bottleMl >= 0};
          ds.add(r.ts, v, ok);
        }
      };
      if (longRange) {
        const int y0 = atoi(clock_->local(from).date), y1 = atoi(clock_->local(to).date);
        for (int y = y0; y <= y1; ++y) forEachLine("/daily/" + std::to_string(y) + ".csv", add);
      } else {
        for (const auto& d : datesInRange(from, to)) forEachLine("/readings/" + d + ".csv", add);
      }
    }
  }
  if (!fromCard) {
    Lock lock(ram_, 500);
    if (lock.ok()) {
      for (const auto& r : ramReadings_) {
        if (slots_[r.slot] != plantId) continue;
        const float v[2] = {r.pct10 / 10.0f, static_cast<float>(r.bottleMl)};
        const bool ok[2] = {true, r.bottleMl != 0xFFFF};
        ds.add(r.ts, v, ok);
      }
    }
  }
  out["plantId"] = plantId;
  plantcore::seriesToJson(ds, from, to, out);
}

void LogStore::envHistory(int64_t from, int64_t to, size_t points, JsonObject out) {
  points = std::clamp<size_t>(points, 10, 300);
  plantcore::Downsampler ds(from, to, points, kEnvColumns);
  const bool longRange = to - from > config::kLongRangeDays * 86400LL;
  bool fromCard = false;
  if (sdOk_) {
    Lock lock(sd_, 3000);
    if (lock.ok()) {
      fromCard = true;
      auto add = [&](const std::string& line) {
        EnvRow r;
        if (plantcore::parseEnv(line, r)) ds.add(r.ts, r.values, r.valid);
      };
      if (longRange) {
        const int y0 = atoi(clock_->local(from).date), y1 = atoi(clock_->local(to).date);
        for (int y = y0; y <= y1; ++y) forEachLine("/daily/env-" + std::to_string(y) + ".csv", add);
      } else {
        for (const auto& d : datesInRange(from, to)) forEachLine("/env/" + d + ".csv", add);
      }
    }
  }
  if (!fromCard) {
    Lock lock(ram_, 500);
    if (lock.ok()) {
      for (const auto& r : ramEnv_) {
        bool valid[kEnvColumns];
        for (int i = 0; i < kEnvColumns; ++i) valid[i] = r.valid & (1 << i);
        ds.add(r.ts, r.values, valid);
      }
    }
  }
  plantcore::seriesToJson(ds, from, to, out);
}

void LogStore::events(int64_t from, int64_t to, const std::string& type, const std::string& plantId, size_t limit,
                      JsonArray out) {
  limit = std::clamp<size_t>(limit, 1, 500);
  std::deque<std::pair<plantcore::Event, std::string>> found;
  auto consider = [&](const plantcore::Event& e, const std::string& pid) {
    if (e.ts < from || e.ts > to) return;
    if (!type.empty() && type != plantcore::eventTypeName(e.type)) return;
    if (!plantId.empty() && pid != plantId) return;
    found.emplace_back(e, pid);
    if (found.size() > limit) found.pop_front();
  };

  bool fromCard = false;
  if (sdOk_) {
    Lock lock(sd_, 3000);
    if (lock.ok()) {
      fromCard = true;
      std::vector<std::string> months;
      for (const auto& d : datesInRange(from, to)) {
        std::string m = d.substr(0, 7);
        if (months.empty() || months.back() != m) months.push_back(m);
      }
      for (const auto& m : months) {
        forEachLine("/events/" + m + ".csv", [&](const std::string& line) {
          plantcore::Event e;
          std::string pid;
          if (plantcore::parseEvent(line, e, pid)) consider(e, pid);
        });
      }
    }
  }
  if (!fromCard) {
    Lock lock(ram_, 500);
    if (lock.ok()) {
      for (const auto& r : ramEvents_) consider(r.e, r.plantId);
    }
  }
  for (auto it = found.rbegin(); it != found.rend(); ++it) plantcore::toJson(it->first, it->second, out.add<JsonObject>());
}

std::vector<std::string> LogStore::exportFiles(const std::string& kind, int64_t from, int64_t to) {
  std::vector<std::string> files;
  if (!sdOk_) return files;
  Lock lock(sd_, 3000);
  if (!lock.ok()) return files;
  for (const auto& d : datesInRange(from, to)) {
    std::string path;
    if (kind == "events") {
      path = "/events/" + d.substr(0, 7) + ".csv";
      if (!files.empty() && files.back() == path) continue;
    } else {
      path = (kind == "env" ? "/env/" : "/readings/") + d + ".csv";
    }
    if (SD.exists(path.c_str())) files.push_back(path);
  }
  return files;
}

size_t LogStore::readChunk(const std::string& path, size_t offset, uint8_t* buf, size_t len) {
  Lock lock(sd_, 2000);
  if (!lock.ok()) return 0;
  File f = SD.open(path.c_str(), FILE_READ);
  if (!f) return 0;
  size_t n = 0;
  if (f.seek(offset)) n = f.read(buf, len);
  f.close();
  return n;
}
