#include "Json.h"

#include <cmath>
#include <initializer_list>
#include <limits>
#include <type_traits>
#include <utility>

#include "Alerts.h"

namespace plantcore {

const char* wateringStyleName(WateringStyle s) {
  switch (s) {
    case WateringStyle::EvenlyMoist: return "evenly_moist";
    case WateringStyle::DryBetween: return "dry_between";
    case WateringStyle::SoakAndDry: return "soak_and_dry";
  }
  return "evenly_moist";
}

const char* faultPolicyName(SensorFaultPolicy p) {
  return p == SensorFaultPolicy::FallbackSchedule ? "fallback_schedule" : "pause";
}

const char* sensorHealthName(SensorHealth h) {
  switch (h) {
    case SensorHealth::Ok: return "ok";
    case SensorHealth::Suspect: return "suspect";
    case SensorHealth::Failed: return "failed";
  }
  return "ok";
}

const char* eventTypeName(EventType t) {
  switch (t) {
    case EventType::Watering: return "watering";
    case EventType::AlertRaised: return "alert";
    case EventType::AlertCleared: return "alert_cleared";
    case EventType::Refill: return "refill";
    case EventType::System: return "system";
  }
  return "system";
}

bool parseEventType(const std::string& s, EventType& out) {
  for (EventType t : {EventType::Watering, EventType::AlertRaised, EventType::AlertCleared, EventType::Refill,
                      EventType::System}) {
    if (s == eventTypeName(t)) {
      out = t;
      return true;
    }
  }
  return false;
}

namespace {

double r1(double x) { return std::round(x * 10.0) / 10.0; }
double r2(double x) { return std::round(x * 100.0) / 100.0; }

// Reads optional keys into existing values; remembers the first error.
class Reader {
 public:
  explicit Reader(std::string& err) : err_(err) {}

  template <typename T>
  void num(JsonObjectConst o, const char* key, T& out) {
    JsonVariantConst v = o[key];
    if (v.isNull()) return;
    if (!v.is<double>()) return fail(key, "must be a number");
    double d = v.as<double>();
    if (!std::isfinite(d)) return fail(key, "must be finite");
    if constexpr (std::is_integral<T>::value) {
      double lo = static_cast<double>(std::numeric_limits<T>::min());
      double hi = static_cast<double>(std::numeric_limits<T>::max());
      if (d < lo || d > hi) return fail(key, "is out of range");
      out = static_cast<T>(std::llround(d));
    } else {
      out = static_cast<T>(d);
    }
  }

  void boolean(JsonObjectConst o, const char* key, bool& out) {
    JsonVariantConst v = o[key];
    if (v.isNull()) return;
    if (!v.is<bool>()) return fail(key, "must be true or false");
    out = v.as<bool>();
  }

  void str(JsonObjectConst o, const char* key, std::string& out, size_t maxLen) {
    JsonVariantConst v = o[key];
    if (v.isNull()) return;
    if (!v.is<const char*>()) return fail(key, "must be a string");
    std::string s = v.as<std::string>();
    if (s.size() > maxLen) return fail(key, "is too long");
    out = s;
  }

  template <typename E>
  void enumeration(JsonObjectConst o, const char* key, E& out, std::initializer_list<std::pair<const char*, E>> map) {
    JsonVariantConst v = o[key];
    if (v.isNull()) return;
    std::string s = v.is<const char*>() ? v.as<std::string>() : std::string();
    for (const auto& kv : map) {
      if (s == kv.first) {
        out = kv.second;
        return;
      }
    }
    fail(key, "has an unknown value");
  }

  void range(JsonObjectConst o, const char* key, Range& out) {
    JsonObjectConst r = o[key];
    if (r.isNull()) return;
    num(r, "min", out.min);
    num(r, "max", out.max);
  }

  void fail(const char* key, const char* why) {
    if (err_.empty()) err_ = std::string(key) + " " + why;
  }

  bool ok() const { return err_.empty(); }

 private:
  std::string& err_;
};

void writeRange(JsonObject parent, const char* key, const Range& r) {
  JsonObject o = parent[key].to<JsonObject>();
  o["min"] = r1(r.min);
  o["max"] = r1(r.max);
}

bool validId(const std::string& id) {
  if (id.empty() || id.size() > 24) return false;
  for (char c : id) {
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (!ok) return false;
  }
  return true;
}

bool validNodeId(const std::string& id) {
  if (id.size() != 12) return false;
  for (char c : id) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

bool between(double v, double lo, double hi) { return v >= lo && v <= hi; }

bool fail(std::string& err, const char* msg) {
  err = msg;
  return false;
}

const char* severityKey(uint8_t s) { return s >= 2 ? "critical" : s == 1 ? "warning" : "info"; }

}  // namespace

// ------------------------------------------------------------------ plants

void toJson(const PlantProfile& p, JsonObject o) {
  o["id"] = p.id;
  o["name"] = p.name;
  o["species"] = p.species;
  o["enabled"] = p.enabled;
  o["autoWater"] = p.autoWater;
  if (!p.node.empty()) {
    o["node"] = p.node;
  } else {
    o["node"] = nullptr;
  }
  JsonObject pump = o["pump"].to<JsonObject>();
  pump["flowMlPerS"] = r2(p.pump.flowMlPerS);
  pump["currentSense"] = p.pump.currentSense;
  JsonObject bottle = o["bottle"].to<JsonObject>();
  bottle["capacityMl"] = r1(p.bottle.capacityMl);
  bottle["lowPct"] = r1(p.bottle.lowPct);
  bottle["reserveMl"] = r1(p.bottle.reserveMl);
  o["potVolumeMl"] = r1(p.potVolumeMl);
  o["soil"] = p.soil;
  JsonObject m = o["moisture"].to<JsonObject>();
  m["critical"] = r1(p.moisture.critical);
  m["min"] = r1(p.moisture.min);
  m["target"] = r1(p.moisture.target);
  m["max"] = r1(p.moisture.max);
  writeRange(o, "lightLux", p.lightLux);
  writeRange(o, "tempC", p.tempC);
  writeRange(o, "humidityPct", p.humidityPct);
  o["style"] = wateringStyleName(p.style);
  o["minIntervalH"] = r1(p.minIntervalH);
  o["maxDailyMl"] = r1(p.maxDailyMl);
  o["maxPulseMl"] = r1(p.maxPulseMl);
  o["soakMin"] = r1(p.soakMin);
  JsonArray months = o["dormantMonths"].to<JsonArray>();
  for (int m1 = 1; m1 <= 12; ++m1) {
    if ((p.dormantMonths >> (m1 - 1)) & 1u) months.add(m1);
  }
  o["dormancyFactor"] = r2(p.dormancyFactor);
  o["onSensorFault"] = faultPolicyName(p.onSensorFault);
  o["fallbackEveryDays"] = r1(p.fallbackEveryDays);
  o["fallbackMl"] = r1(p.fallbackMl);
  JsonObject cal = o["calibration"].to<JsonObject>();
  cal["rawDry"] = p.cal.rawDry;
  cal["rawWet"] = p.cal.rawWet;
  o["careNotes"] = p.careNotes;
}

bool fromJson(JsonObjectConst o, PlantProfile& p, std::string& err) {
  err.clear();
  Reader r(err);
  r.str(o, "id", p.id, 24);
  r.str(o, "name", p.name, 40);
  r.str(o, "species", p.species, 60);
  r.boolean(o, "enabled", p.enabled);
  r.boolean(o, "autoWater", p.autoWater);
  // "node": null unlinks the plant from its node.
  for (JsonPairConst kv : o) {
    if (kv.key() == "node" && kv.value().isNull()) p.node.clear();
  }
  r.str(o, "node", p.node, 12);
  JsonObjectConst pump = o["pump"];
  if (!pump.isNull()) {
    r.num(pump, "flowMlPerS", p.pump.flowMlPerS);
    r.boolean(pump, "currentSense", p.pump.currentSense);
  }
  JsonObjectConst bottle = o["bottle"];
  if (!bottle.isNull()) {
    r.num(bottle, "capacityMl", p.bottle.capacityMl);
    r.num(bottle, "lowPct", p.bottle.lowPct);
    r.num(bottle, "reserveMl", p.bottle.reserveMl);
  }
  r.num(o, "potVolumeMl", p.potVolumeMl);
  r.str(o, "soil", p.soil, 120);
  JsonObjectConst m = o["moisture"];
  if (!m.isNull()) {
    r.num(m, "critical", p.moisture.critical);
    r.num(m, "min", p.moisture.min);
    r.num(m, "target", p.moisture.target);
    r.num(m, "max", p.moisture.max);
  }
  r.range(o, "lightLux", p.lightLux);
  r.range(o, "tempC", p.tempC);
  r.range(o, "humidityPct", p.humidityPct);
  r.enumeration(o, "style", p.style,
                {{"evenly_moist", WateringStyle::EvenlyMoist},
                 {"dry_between", WateringStyle::DryBetween},
                 {"soak_and_dry", WateringStyle::SoakAndDry}});
  r.num(o, "minIntervalH", p.minIntervalH);
  r.num(o, "maxDailyMl", p.maxDailyMl);
  r.num(o, "maxPulseMl", p.maxPulseMl);
  r.num(o, "soakMin", p.soakMin);
  JsonVariantConst months = o["dormantMonths"];
  if (!months.isNull()) {
    if (!months.is<JsonArrayConst>()) {
      r.fail("dormantMonths", "must be a list of months 1-12");
    } else {
      uint16_t mask = 0;
      for (JsonVariantConst v : months.as<JsonArrayConst>()) {
        int month = v.as<int>();
        if (!v.is<int>() || month < 1 || month > 12) {
          r.fail("dormantMonths", "must be a list of months 1-12");
          break;
        }
        mask |= static_cast<uint16_t>(1u << (month - 1));
      }
      p.dormantMonths = mask;
    }
  }
  r.num(o, "dormancyFactor", p.dormancyFactor);
  r.enumeration(o, "onSensorFault", p.onSensorFault,
                {{"pause", SensorFaultPolicy::Pause}, {"fallback_schedule", SensorFaultPolicy::FallbackSchedule}});
  r.num(o, "fallbackEveryDays", p.fallbackEveryDays);
  r.num(o, "fallbackMl", p.fallbackMl);
  JsonObjectConst cal = o["calibration"];
  if (!cal.isNull()) {
    r.num(cal, "rawDry", p.cal.rawDry);
    r.num(cal, "rawWet", p.cal.rawWet);
  }
  r.str(o, "careNotes", p.careNotes, 1000);
  if (!r.ok()) return false;
  return validateProfile(p, err);
}

bool validateProfile(const PlantProfile& p, std::string& err) {
  if (!validId(p.id)) return fail(err, "id must be 1-24 characters: a-z, 0-9, - or _");
  if (p.name.empty()) return fail(err, "name is required");
  if (!p.node.empty() && !validNodeId(p.node)) return fail(err, "node must be 12 hex digits");
  if (!between(p.pump.flowMlPerS, 0.05, 100)) return fail(err, "pump.flowMlPerS must be 0.05-100");
  if (!between(p.bottle.capacityMl, 100, 20000)) return fail(err, "bottle.capacityMl must be 100-20000");
  if (!between(p.bottle.lowPct, 0, 90)) return fail(err, "bottle.lowPct must be 0-90");
  if (!(p.bottle.reserveMl >= 0 && p.bottle.reserveMl < p.bottle.capacityMl / 2)) {
    return fail(err, "bottle.reserveMl must be below half the capacity");
  }
  if (!between(p.potVolumeMl, 50, 100000)) return fail(err, "potVolumeMl must be 50-100000");
  const MoistureBand& m = p.moisture;
  if (!(m.critical >= 0 && m.critical < m.min && m.min < m.target && m.target < m.max && m.max <= 100)) {
    return fail(err, "moisture must satisfy 0 <= critical < min < target < max <= 100");
  }
  if (!(p.lightLux.min >= 0 && p.lightLux.min < p.lightLux.max && p.lightLux.max <= 200000)) {
    return fail(err, "lightLux must be 0 <= min < max <= 200000");
  }
  if (!(p.tempC.min >= -10 && p.tempC.min < p.tempC.max && p.tempC.max <= 50)) {
    return fail(err, "tempC must be -10 <= min < max <= 50");
  }
  if (!(p.humidityPct.min >= 0 && p.humidityPct.min < p.humidityPct.max && p.humidityPct.max <= 100)) {
    return fail(err, "humidityPct must be 0 <= min < max <= 100");
  }
  if (!between(p.minIntervalH, 0.5, 336)) return fail(err, "minIntervalH must be 0.5-336");
  if (!between(p.maxDailyMl, 5, 20000)) return fail(err, "maxDailyMl must be 5-20000");
  if (!between(p.maxPulseMl, 5, 2000)) return fail(err, "maxPulseMl must be 5-2000");
  if (!between(p.soakMin, 2, 60)) return fail(err, "soakMin must be 2-60");
  if (!between(p.dormancyFactor, 0.1, 1)) return fail(err, "dormancyFactor must be 0.1-1");
  if (!between(p.fallbackEveryDays, 0.5, 60)) return fail(err, "fallbackEveryDays must be 0.5-60");
  if (!between(p.fallbackMl, 5, 2000)) return fail(err, "fallbackMl must be 5-2000");
  if (!between(p.cal.rawDry, 0, 32767) || !between(p.cal.rawWet, 0, 32767)) {
    return fail(err, "calibration values must be 0-32767");
  }
  return true;
}

// ------------------------------------------------------------------ settings

void toJson(const Settings& s, JsonObject o) {
  o["deviceName"] = s.deviceName;
  o["timezone"] = s.timezone;
  o["tzName"] = s.tzName;
  o["southernHemisphere"] = s.southernHemisphere;
  JsonObject w = o["waterWindow"].to<JsonObject>();
  w["startH"] = s.waterWindowStartH;
  w["endH"] = s.waterWindowEndH;
  o["checkInMin"] = s.checkInMin;
  o["logIntervalMin"] = s.logIntervalMin;
  o["automationEnabled"] = s.automationEnabled;
  JsonObject l = o["limits"].to<JsonObject>();
  l["hardMaxPulseMl"] = r1(s.limits.hardMaxPulseMl);
  l["hardMaxDailyMlPerPlant"] = r1(s.limits.hardMaxDailyMlPerPlant);
  l["hardMaxOnTimeS"] = s.limits.hardMaxOnTimeMs / 1000;
  JsonObject n = o["notify"].to<JsonObject>();
  n["enabled"] = s.notify.enabled;
  n["server"] = s.notify.server;
  n["topic"] = s.notify.topic;
  n["tokenSet"] = !s.notify.token.empty();
  n["minSeverity"] = severityKey(s.notify.minSeverity);
}

bool fromJson(JsonObjectConst o, Settings& s, std::string& err) {
  err.clear();
  Reader r(err);
  r.str(o, "deviceName", s.deviceName, 32);
  r.str(o, "timezone", s.timezone, 60);
  r.str(o, "tzName", s.tzName, 60);
  r.boolean(o, "southernHemisphere", s.southernHemisphere);
  JsonObjectConst w = o["waterWindow"];
  if (!w.isNull()) {
    r.num(w, "startH", s.waterWindowStartH);
    r.num(w, "endH", s.waterWindowEndH);
  }
  r.num(o, "checkInMin", s.checkInMin);
  r.num(o, "logIntervalMin", s.logIntervalMin);
  r.boolean(o, "automationEnabled", s.automationEnabled);
  JsonObjectConst l = o["limits"];
  if (!l.isNull()) {
    r.num(l, "hardMaxPulseMl", s.limits.hardMaxPulseMl);
    r.num(l, "hardMaxDailyMlPerPlant", s.limits.hardMaxDailyMlPerPlant);
    uint32_t onS = s.limits.hardMaxOnTimeMs / 1000;
    r.num(l, "hardMaxOnTimeS", onS);
    s.limits.hardMaxOnTimeMs = onS * 1000;
  }
  JsonObjectConst n = o["notify"];
  if (!n.isNull()) {
    r.boolean(n, "enabled", s.notify.enabled);
    r.str(n, "server", s.notify.server, 100);
    r.str(n, "topic", s.notify.topic, 64);
    r.str(n, "token", s.notify.token, 128);
    r.enumeration(n, "minSeverity", s.notify.minSeverity,
                  {{"info", uint8_t{0}}, {"warning", uint8_t{1}}, {"critical", uint8_t{2}}});
  }
  if (!r.ok()) return false;
  return validateSettings(s, err);
}

bool validateSettings(const Settings& s, std::string& err) {
  if (s.deviceName.empty()) return fail(err, "deviceName is required");
  if (s.timezone.empty()) return fail(err, "timezone is required");
  if (s.waterWindowStartH > 23 || s.waterWindowEndH > 23) return fail(err, "waterWindow hours must be 0-23");
  if (!between(s.checkInMin, 1, 60)) return fail(err, "checkInMin must be 1-60");
  if (!between(s.logIntervalMin, 1, 60)) return fail(err, "logIntervalMin must be 1-60");
  if (!between(s.limits.hardMaxPulseMl, 5, 2000)) return fail(err, "limits.hardMaxPulseMl must be 5-2000");
  if (!between(s.limits.hardMaxDailyMlPerPlant, 10, 20000)) {
    return fail(err, "limits.hardMaxDailyMlPerPlant must be 10-20000");
  }
  if (!between(s.limits.hardMaxOnTimeMs, 5000, 600000)) return fail(err, "limits.hardMaxOnTimeS must be 5-600");
  const NotifyConfig& n = s.notify;
  if (n.server.rfind("https://", 0) != 0 && n.server.rfind("http://", 0) != 0) {
    return fail(err, "notify.server must start with http:// or https://");
  }
  for (char c : n.topic) {
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (!ok) return fail(err, "notify.topic may only contain letters, digits, - and _");
  }
  if (n.enabled && n.topic.empty()) return fail(err, "notify.topic is required when notifications are enabled");
  return true;
}

// ------------------------------------------------------------------ persisted state

void toJson(const PersistedState& s, JsonObject o) {
  o["v"] = 2;
  o["day"] = s.day;
  o["stopped"] = s.stopped;
  if (s.dispenseInProgress) o["inProgressMl"] = r1(s.inProgressMl);
  JsonObject p = o["plant"].to<JsonObject>();
  p["last"] = s.plant.lastSessionEndS;
  p["lastFb"] = s.plant.lastFallbackS;
  p["mlToday"] = r1(s.plant.mlToday);
  p["mlPerPct"] = r2(s.plant.mlPerPct);
  p["locked"] = s.plant.lockedOut;
  JsonObject b = o["bottle"].to<JsonObject>();
  b["ml"] = r1(s.bottle.ml);
  b["pumped"] = r1(s.bottle.pumpedSinceRefillMl);
  b["full"] = s.bottle.knownFull;
  b["flow"] = std::round(s.bottle.flowFactor * 1000.0) / 1000.0;
  b["usage"] = r1(s.bottle.usagePerDayMl);
  b["empty"] = s.bottle.empty;
  b["why"] = s.bottle.emptyReason;
  JsonObject pu = o["pump"].to<JsonObject>();
  pu["wet"] = r2(s.pump.wetMaPerV);
  pu["runs"] = s.pump.runs;
}

bool fromJson(JsonObjectConst o, PersistedState& s) {
  if (o.isNull() || o["v"].as<int>() != 2) return false;
  s = PersistedState{};
  s.day = o["day"].as<int32_t>();
  s.stopped = o["stopped"].as<bool>();
  if (o["inProgressMl"].is<float>()) {
    s.dispenseInProgress = true;
    s.inProgressMl = o["inProgressMl"].as<float>();
  }
  JsonObjectConst p = o["plant"];
  s.plant.lastSessionEndS = p["last"].as<int64_t>();
  s.plant.lastFallbackS = p["lastFb"].as<int64_t>();
  s.plant.mlToday = p["mlToday"].as<float>();
  s.plant.mlPerPct = p["mlPerPct"].as<float>();
  s.plant.lockedOut = p["locked"].as<bool>();
  JsonObjectConst b = o["bottle"];
  s.bottle.ml = b["ml"] | -1.0f;
  s.bottle.pumpedSinceRefillMl = b["pumped"].as<float>();
  s.bottle.knownFull = b["full"].as<bool>();
  s.bottle.flowFactor = b["flow"] | 1.0f;
  s.bottle.usagePerDayMl = b["usage"].as<float>();
  s.bottle.empty = b["empty"].as<bool>();
  s.bottle.emptyReason = b["why"].as<uint8_t>();
  JsonObjectConst pu = o["pump"];
  s.pump.wetMaPerV = pu["wet"].as<float>();
  s.pump.runs = pu["runs"].as<uint8_t>();
  return true;
}

// ------------------------------------------------------------------ alerts / events

void toJson(const Alert& a, const std::string& plantId, JsonObject o) {
  o["id"] = a.id;
  o["code"] = alertCodeName(a.code);
  o["severity"] = severityName(a.severity);
  if (!plantId.empty()) {
    o["plantId"] = plantId;
  } else {
    o["plantId"] = nullptr;
  }
  o["active"] = a.active;
  o["acked"] = a.acked;
  o["raisedAt"] = a.raisedAt;
  if (a.clearedAt > 0) {
    o["clearedAt"] = a.clearedAt;
  } else {
    o["clearedAt"] = nullptr;
  }
  o["message"] = a.message;
}

void toJson(const Event& e, const std::string& plantId, JsonObject o) {
  o["ts"] = e.ts;
  o["type"] = eventTypeName(e.type);
  if (!plantId.empty()) {
    o["plantId"] = plantId;
  } else {
    o["plantId"] = nullptr;
  }
  o["code"] = e.code;
  o["ml"] = r1(e.ml);
  o["detail"] = e.detail;
}

// ------------------------------------------------------------------ node config

void nodeConfigToJson(const Settings& s, const PlantProfile& p, JsonObject o) {
  JsonObject plant = o["plant"].to<JsonObject>();
  toJson(p, plant);
  plant.remove("careNotes");
  plant.remove("soil");
  plant.remove("species");
  plant.remove("node");
  JsonObject set = o["settings"].to<JsonObject>();
  set["southernHemisphere"] = s.southernHemisphere;
  JsonObject w = set["waterWindow"].to<JsonObject>();
  w["startH"] = s.waterWindowStartH;
  w["endH"] = s.waterWindowEndH;
  set["checkInMin"] = s.checkInMin;
  set["automationEnabled"] = s.automationEnabled;
  JsonObject l = set["limits"].to<JsonObject>();
  l["hardMaxPulseMl"] = r1(s.limits.hardMaxPulseMl);
  l["hardMaxDailyMlPerPlant"] = r1(s.limits.hardMaxDailyMlPerPlant);
  l["hardMaxOnTimeS"] = s.limits.hardMaxOnTimeMs / 1000;
}

bool nodeConfigFromJson(JsonObjectConst o, Settings& s, PlantProfile& p, std::string& err) {
  if (o["plant"].isNull() || o["settings"].isNull()) return fail(err, "plant and settings are required");
  Settings ns = s;
  PlantProfile np = p;
  if (!fromJson(o["settings"].as<JsonObjectConst>(), ns, err)) return false;
  if (!fromJson(o["plant"].as<JsonObjectConst>(), np, err)) return false;
  s = ns;
  p = np;
  return true;
}

}  // namespace plantcore
