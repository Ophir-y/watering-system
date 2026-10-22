#include "Hub.h"

#include <esp_random.h>
#include <esp_timer.h>

#include <ctime>

#include "Json.h"
#include "config.h"

using namespace plantcore;
using namespace plantlink;

namespace {

class Lock {
 public:
  Lock(SemaphoreHandle_t m, uint32_t timeoutMs) : m_(m) {
    ok_ = xSemaphoreTakeRecursive(m_, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
  }
  ~Lock() {
    if (ok_) xSemaphoreGiveRecursive(m_);
  }
  bool ok() const { return ok_; }

 private:
  SemaphoreHandle_t m_;
  bool ok_;
};

int64_t monoMs() { return esp_timer_get_time() / 1000; }

// Seconds to add to UTC for local time right now (follows daylight saving).
int32_t utcOffsetNow(int64_t epoch) {
  const time_t t = static_cast<time_t>(epoch);
  struct tm l, g;
  localtime_r(&t, &l);
  gmtime_r(&t, &g);
  auto secs = [](const struct tm& x) {
    return daysFromCivil(x.tm_year + 1900, x.tm_mon + 1, x.tm_mday) * 86400 + x.tm_hour * 3600 + x.tm_min * 60 +
           x.tm_sec;
  };
  return static_cast<int32_t>(secs(l) - secs(g));
}

}  // namespace

void Hub::begin() {
  mutex_ = xSemaphoreCreateRecursiveMutex();
  Settings s;
  if (!config_.loadSettings(s)) s = Settings{};
  std::vector<PlantProfile> plants;
  config_.loadPlants(plants);
  std::vector<NodeRecord> nodes;
  config_.loadNodes(nodes);
  clock_.setTimezone(s.timezone);
  Lock lock(mutex_, portMAX_DELAY);
  station_.setClock(clockInfo());
  station_.begin(s, plants, nodes, config_.loadStopped());
}

bool Hub::with(const std::function<void(Station&)>& fn, uint32_t timeoutMs) {
  Lock lock(mutex_, timeoutMs);
  if (!lock.ok()) return false;
  fn(station_);
  return true;
}

ClockInfo Hub::clockInfo() const {
  ClockInfo c;
  c.valid = clock_.valid();
  c.now = clock_.now();
  c.utcOffsetS = c.valid ? utcOffsetNow(c.now) : 0;
  return c;
}

size_t Hub::onFrame(const Mac& from, const uint8_t* d, size_t n, uint8_t* reply) {
  Lock lock(mutex_, 40);  // the node waits only ~60 ms for its reply
  if (!lock.ok()) return 0;
  station_.setClock(clockInfo());
  return station_.onFrame(from, d, n, monoMs(), reply);
}

void Hub::tick(uint32_t nowMs, uint8_t radioChannel, bool apActive) {
  EnvSnapshot env;
  const bool sampleEnv = lastEnvMs_ == 0 || nowMs - lastEnvMs_ >= config::kEnvSampleMs;
  if (sampleEnv) {
    lastEnvMs_ = nowMs;
    env = env_.read();
  }
  Severity sev = Severity::Info;
  bool anyAlert = false;
  bool stopped = false;
  int64_t logEveryS = 600;
  {
    Lock lock(mutex_, 500);
    if (!lock.ok()) return;
    station_.setClock(clockInfo());
    station_.setRadioChannel(radioChannel);
    if (sampleEnv) station_.setEnvironment(env);
    station_.tick(monoMs());
    anyAlert = station_.alerts().highestActive(sev, true);
    stopped = station_.emergencyStopped();
    logEveryS = static_cast<int64_t>(station_.settings().logIntervalMin) * 60;
  }
  if (sampleEnv) logEnvironment(env, logEveryS);

  LedPattern p = LedPattern::Ok;
  if (stopped) {
    p = LedPattern::Stopped;
  } else if (anyAlert && sev == Severity::Critical) {
    p = LedPattern::Critical;
  } else if (apActive) {
    p = LedPattern::SetupAp;
  } else if (anyAlert && sev == Severity::Warning) {
    p = LedPattern::Warning;
  }
  led_ = static_cast<uint8_t>(p);
}

void Hub::logEnvironment(const EnvSnapshot& env, int64_t logEveryS) {
  if (!clock_.valid() || !(env.valid || env.luxValid)) return;
  const int64_t now = clock_.now();
  if (lastEnvLogTs_ && now - lastEnvLogTs_ < logEveryS) return;
  lastEnvLogTs_ = now;
  EnvRow row;
  row.ts = now;
  row.valid[0] = row.valid[1] = env.valid;
  row.values[0] = env.tempC;
  row.values[1] = env.humidityPct;
  row.valid[2] = env.luxValid;
  row.values[2] = env.lux;
  logs_.env(row);
}

void Hub::flushPushes() {
  std::vector<std::pair<const char*, std::string>> out;
  {
    Lock lock(mutex_, 100);
    if (!lock.ok()) return;
    out.swap(pendingPushes_);
  }
  if (!pusher_) return;
  for (const auto& p : out) pusher_(p.first, p.second);
}

bool Hub::status(JsonObject out, const SystemInfo& sys) {
  Lock lock(mutex_, 1000);
  if (!lock.ok()) return false;
  station_.statusToJson(sys, monoMs(), out);
  return true;
}

Settings Hub::settings() {
  Settings s;
  with([&](Station& st) { s = st.settings(); });
  return s;
}

std::vector<PlantProfile> Hub::plants() {
  std::vector<PlantProfile> p;
  with([&](Station& st) { p = st.plants(); });
  return p;
}

// ------------------------------------------------------------------ sink

void Hub::onEvent(const Event& e, const std::string& plantId) {
  logs_.event(e, plantId);
  JsonDocument doc;
  toJson(e, plantId, doc.to<JsonObject>());
  std::string json;
  serializeJson(doc, json);
  Lock lock(mutex_, 100);
  if (lock.ok() && pendingPushes_.size() < 32) pendingPushes_.emplace_back("event", json);
}

void Hub::onReading(const ReadingRow& r) { logs_.reading(r); }

bool Hub::persistPlants(const std::vector<PlantProfile>& plants) { return config_.savePlants(plants); }

bool Hub::persistSettings(const Settings& s) {
  clock_.setTimezone(s.timezone);
  return config_.saveSettings(s);
}

bool Hub::persistNodes(const std::vector<NodeRecord>& nodes) { return config_.saveNodes(nodes); }

bool Hub::persistStopped(bool stopped) { return config_.saveStopped(stopped); }

Key Hub::newKey() {
  Key k;
  esp_fill_random(k.data(), k.size());  // hardware RNG (true random while Wi-Fi is on)
  return k;
}
