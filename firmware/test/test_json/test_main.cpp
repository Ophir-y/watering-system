// Contract tests: firmware JSON must match shared/api-fixtures, which the dashboard
// also uses for its types, mock device and tests.
#include <unity.h>

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

#include "History.h"
#include "Json.h"

using namespace plantcore;

void setUp() {}
void tearDown() {}

static std::string readFixture(const char* name) {
  std::ifstream f(std::string(FIXTURES_DIR) + "/" + name);
  TEST_ASSERT_TRUE_MESSAGE(f.good(), name);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static JsonDocument loadFixture(const char* name) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, readFixture(name));
  TEST_ASSERT_TRUE_MESSAGE(!err, err.c_str());
  return doc;
}

// Deep comparison: same keys, same types, numbers within 0.01.
static bool same(JsonVariantConst a, JsonVariantConst b, const std::string& path, std::string& why) {
  if (a.is<JsonObjectConst>()) {
    if (!b.is<JsonObjectConst>()) return why = path + ": expected an object", false;
    JsonObjectConst oa = a, ob = b;
    for (JsonPairConst kv : ob) {
      if (oa[kv.key()].isUnbound()) return why = path + "." + kv.key().c_str() + ": unexpected key", false;
    }
    for (JsonPairConst kv : oa) {
      std::string p = path + "." + kv.key().c_str();
      if (ob[kv.key()].isUnbound()) return why = p + ": missing", false;
      if (!same(kv.value(), ob[kv.key()], p, why)) return false;
    }
    return true;
  }
  if (a.is<JsonArrayConst>()) {
    if (!b.is<JsonArrayConst>()) return why = path + ": expected an array", false;
    JsonArrayConst xa = a, xb = b;
    if (xa.size() != xb.size()) return why = path + ": array size differs", false;
    for (size_t i = 0; i < xa.size(); ++i) {
      if (!same(xa[i], xb[i], path + "[" + std::to_string(i) + "]", why)) return false;
    }
    return true;
  }
  if (a.isNull()) return b.isNull() || (why = path + ": expected null", false);
  if (a.is<bool>()) return (b.is<bool>() && a.as<bool>() == b.as<bool>()) || (why = path + ": bool differs", false);
  if (a.is<const char*>()) {
    if (b.is<const char*>() && a.as<std::string>() == b.as<std::string>()) return true;
    return why = path + ": expected \"" + a.as<std::string>() + "\" got \"" + b.as<std::string>() + "\"", false;
  }
  if (a.is<double>()) {
    if (b.is<double>() && std::fabs(a.as<double>() - b.as<double>()) <= 0.01) return true;
    return why = path + ": expected " + std::to_string(a.as<double>()) + " got " + b.as<std::string>(), false;
  }
  return why = path + ": unsupported type", false;
}

static void assertMatches(JsonVariantConst expected, JsonVariantConst actual) {
  std::string why;
  TEST_ASSERT_TRUE_MESSAGE(same(expected, actual, "$", why), why.c_str());
}

void test_comparison_detects_differences() {
  JsonDocument fixture = loadFixture("plant.json");
  std::string why;
  JsonDocument copy = fixture;
  TEST_ASSERT_TRUE(same(fixture.as<JsonVariantConst>(), copy.as<JsonVariantConst>(), "$", why));
  copy["moisture"]["max"] = 76;
  TEST_ASSERT_FALSE(same(fixture.as<JsonVariantConst>(), copy.as<JsonVariantConst>(), "$", why));
  TEST_ASSERT_EQUAL_STRING("$.moisture.max: expected 75.000000 got 76", why.c_str());
  copy = fixture;
  copy.remove("soil");
  TEST_ASSERT_FALSE(same(fixture.as<JsonVariantConst>(), copy.as<JsonVariantConst>(), "$", why));
  copy = fixture;
  copy["extra"] = 1;
  TEST_ASSERT_FALSE(same(fixture.as<JsonVariantConst>(), copy.as<JsonVariantConst>(), "$", why));
  copy = fixture;
  copy["dormantMonths"].as<JsonArray>().add(3);
  TEST_ASSERT_FALSE(same(fixture.as<JsonVariantConst>(), copy.as<JsonVariantConst>(), "$", why));
}

// ------------------------------------------------------------------ plants

void test_plant_fixture_round_trip() {
  JsonDocument fixture = loadFixture("plant.json");
  PlantProfile p;
  std::string err;
  TEST_ASSERT_TRUE_MESSAGE(fromJson(fixture.as<JsonObjectConst>(), p, err), err.c_str());
  TEST_ASSERT_EQUAL_STRING("monstera", p.id.c_str());
  TEST_ASSERT_EQUAL(OutletType::Pump, p.outlet.type);
  TEST_ASSERT_EQUAL_HEX16((1u << 0) | (1u << 1) | (1u << 10) | (1u << 11), p.dormantMonths);
  JsonDocument out;
  toJson(p, out.to<JsonObject>());
  assertMatches(fixture.as<JsonVariantConst>(), out.as<JsonVariantConst>());
}

static bool parsePlant(const char* json, PlantProfile& p, std::string& err) {
  JsonDocument doc;
  deserializeJson(doc, json);
  return fromJson(doc.as<JsonObjectConst>(), p, err);
}

void test_plant_partial_update_keeps_other_fields() {
  PlantProfile p;
  std::string err;
  TEST_ASSERT_TRUE(parsePlant(R"({"id":"fern","name":"Fern","potVolumeMl":1500})", p, err));
  TEST_ASSERT_TRUE(parsePlant(R"({"name":"Boston fern","moisture":{"target":60}})", p, err));
  TEST_ASSERT_EQUAL_STRING("fern", p.id.c_str());
  TEST_ASSERT_EQUAL_STRING("Boston fern", p.name.c_str());
  TEST_ASSERT_EQUAL_FLOAT(1500, p.potVolumeMl);
  TEST_ASSERT_EQUAL_FLOAT(60, p.moisture.target);
  TEST_ASSERT_EQUAL_FLOAT(35, p.moisture.min);
}

void test_plant_validation_errors() {
  struct Case {
    const char* json;
    const char* error;
  };
  const Case cases[] = {
      {R"({"id":"Bad Id","name":"x"})", "id must be"},
      {R"({"id":"a","name":""})", "name is required"},
      {R"({"id":"a","name":"x","channel":8})", "channel must be"},
      {R"({"id":"a","name":"x","channel":-1})", "channel is out of range"},
      {R"({"id":"a","name":"x","moisture":{"min":60}})", "moisture must satisfy"},
      {R"({"id":"a","name":"x","potVolumeMl":"big"})", "potVolumeMl must be a number"},
      {R"({"id":"a","name":"x","style":"drown_it"})", "style has an unknown value"},
      {R"({"id":"a","name":"x","dormantMonths":[0]})", "dormantMonths must be"},
      {R"({"id":"a","name":"x","maxPulseMl":1})", "maxPulseMl must be"},
      {R"({"id":"a","name":"x","outlet":{"flowMlPerS":0}})", "outlet.flowMlPerS must be"},
  };
  for (const auto& c : cases) {
    PlantProfile p;
    std::string err;
    TEST_ASSERT_FALSE_MESSAGE(parsePlant(c.json, p, err), c.json);
    TEST_ASSERT_TRUE_MESSAGE(err.find(c.error) == 0, (std::string(c.json) + " -> " + err).c_str());
  }
}

// ------------------------------------------------------------------ settings

void test_settings_fixture_round_trip() {
  JsonDocument fixture = loadFixture("settings.json");
  Settings s;
  std::string err;
  TEST_ASSERT_TRUE_MESSAGE(fromJson(fixture.as<JsonObjectConst>(), s, err), err.c_str());
  TEST_ASSERT_EQUAL_UINT32(120000, s.limits.hardMaxOnTimeMs);
  JsonDocument out;
  toJson(s, out.to<JsonObject>());
  assertMatches(fixture.as<JsonVariantConst>(), out.as<JsonVariantConst>());
}

void test_settings_token_is_write_only() {
  Settings s;
  std::string err;
  JsonDocument in;
  deserializeJson(in, R"({"notify":{"token":"tk_secret"}})");
  TEST_ASSERT_TRUE(fromJson(in.as<JsonObjectConst>(), s, err));
  TEST_ASSERT_EQUAL_STRING("tk_secret", s.notify.token.c_str());
  JsonDocument out;
  toJson(s, out.to<JsonObject>());
  std::string text;
  serializeJson(out, text);
  TEST_ASSERT_TRUE(text.find("tk_secret") == std::string::npos);
  TEST_ASSERT_TRUE(out["notify"]["tokenSet"].as<bool>());
}

void test_settings_validation() {
  const char* bad[] = {
      R"({"notify":{"enabled":true,"topic":""}})",
      R"({"notify":{"topic":"has space"}})",
      R"({"notify":{"server":"ftp://x"}})",
      R"({"waterWindow":{"startH":24}})",
      R"({"waterWindow":{"startH":300}})",
      R"({"sampleIntervalS":1})",
      R"({"reservoir":{"emptyMl":6000}})",
      R"({"limits":{"hardMaxOnTimeS":1000}})",
  };
  for (const char* json : bad) {
    Settings s;
    std::string err;
    JsonDocument in;
    deserializeJson(in, json);
    TEST_ASSERT_FALSE_MESSAGE(fromJson(in.as<JsonObjectConst>(), s, err), json);
  }
}

// ------------------------------------------------------------------ status

void test_status_matches_fixture() {
  Settings settings;
  settings.tzName = "Asia/Jerusalem";

  SystemInfo sys;
  sys.version = "0.1.0";
  sys.uptimeS = 86400;
  sys.timeValid = true;
  sys.now = 1767261600;
  sys.heapFree = 154320;
  sys.resetReason = "power_on";
  sys.wifiMode = "sta";
  sys.ssid = "HomeWiFi";
  sys.ip = "192.168.1.50";
  sys.rssi = -58;
  sys.apSsid = "PlantCare-1A2B";
  sys.internet = true;
  sys.sdOk = true;
  sys.sdUsedMb = 12.5f;
  sys.sdTotalMb = 29819.4f;
  sys.scaleCalibrated = true;
  sys.scaleRaw = 1843210;

  EngineStatus st;
  st.reservoir.scaleValid = true;
  st.reservoir.ml = 3250;
  st.reservoir.pct = 65;
  st.reservoir.daysLeft = 8.6f;
  st.env = EnvSnapshot{true, 23.4f, 48, true, 5400};
  st.vpdKPa = 1.5f;
  st.dose.active = true;
  st.dose.plantId = "monstera";
  st.dose.ml = 60;
  st.dose.dispensedMl = 22.5f;
  st.lastDose.valid = true;
  st.lastDose.plantId = "monstera";
  st.lastDose.ml = 98.2f;
  st.lastDose.onTimeMs = 54500;
  st.lastDose.closedLoop = true;
  st.lastDose.at = 1767258000;
  st.mlTodayTotal = 230;
  PlantStatus a;
  a.id = "monstera";
  a.name = "Monstera";
  a.haveMoisture = true;
  a.moisturePct = 33.2f;
  a.raw = 17348;
  a.planner = PlannerState::Pulsing;
  a.triggerPct = 36.5f;
  a.targetPct = 55;
  a.mlToday = 120;
  a.lastWateredAt = 1767175200;
  a.mlPerPct = 11.25f;
  PlantStatus b;
  b.id = "snake-plant";
  b.name = "Snake plant";
  b.channel = 1;
  b.health = SensorHealth::Failed;
  b.issue = SensorIssue::Disconnected;
  b.triggerPct = 15;
  b.targetPct = 40;
  st.plants = {a, b};
  st.alertsWarning = 1;

  JsonDocument out;
  statusToJson(st, sys, settings, out.to<JsonObject>());
  JsonDocument fixture = loadFixture("status.json");
  assertMatches(fixture.as<JsonVariantConst>(), out.as<JsonVariantConst>());
}

// ------------------------------------------------------------------ alerts / events

void test_alerts_match_fixture() {
  Alert low;
  low.id = 7;
  low.code = AlertCode::ReservoirLow;
  low.severity = Severity::Warning;
  low.active = true;
  low.raisedAt = 1767250000;
  low.message = "The reservoir is low: 900 ml (18%), about 1.9 days left.";
  Alert sensor;
  sensor.id = 5;
  sensor.code = AlertCode::SensorFault;
  sensor.severity = Severity::Warning;
  sensor.channel = 1;
  sensor.acked = true;
  sensor.raisedAt = 1767200000;
  sensor.clearedAt = 1767210000;
  sensor.message = "Snake plant: moisture sensor looks disconnected or shorted.";

  JsonDocument out;
  JsonArray arr = out["alerts"].to<JsonArray>();
  toJson(low, "", arr.add<JsonObject>());
  toJson(sensor, "snake-plant", arr.add<JsonObject>());
  JsonDocument fixture = loadFixture("alerts.json");
  assertMatches(fixture.as<JsonVariantConst>(), out.as<JsonVariantConst>());
}

void test_events_match_fixture() {
  Event w;
  w.ts = 1767254400;
  w.type = EventType::Watering;
  w.code = "auto";
  w.ml = 205;
  w.detail = "2 pulses, 33% -> 55%, target reached";
  Event a;
  a.ts = 1767250000;
  a.type = EventType::AlertRaised;
  a.code = "reservoir_low";
  a.detail = "The reservoir is low: 900 ml (18%), about 1.9 days left.";
  Event r;
  r.ts = 1767240000;
  r.type = EventType::Refill;
  r.code = "refill";
  r.ml = 3800;
  r.detail = "Reservoir refilled (+3800 ml)";

  JsonDocument out;
  JsonArray arr = out["events"].to<JsonArray>();
  toJson(w, "monstera", arr.add<JsonObject>());
  toJson(a, "", arr.add<JsonObject>());
  toJson(r, "", arr.add<JsonObject>());
  JsonDocument fixture = loadFixture("events.json");
  assertMatches(fixture.as<JsonVariantConst>(), out.as<JsonVariantConst>());
}

// ------------------------------------------------------------------ history

void test_history_matches_fixture() {
  const EpochS from = 1767225600, to = from + 3 * 3600;
  Downsampler d(from, to, 3, 1);
  const std::pair<EpochS, float> rows[] = {{from + 600, 40}, {from + 1800, 42}, {from + 4200, 55}};
  for (const auto& r : rows) {
    bool ok = true;
    d.add(r.first, &r.second, &ok);
  }
  JsonDocument out;
  out["plantId"] = "monstera";
  seriesToJson(d, from, to, out.as<JsonObject>());
  JsonDocument fixture = loadFixture("history.json");
  assertMatches(fixture.as<JsonVariantConst>(), out.as<JsonVariantConst>());
}

void test_env_history_matches_fixture() {
  const EpochS from = 1767225600, to = from + 3 * 3600;
  Downsampler d(from, to, 3, kEnvColumns);
  EnvRow rows[3];
  const char* lines[] = {"1767226200,22.0,50.0,5000.0,3300.0", "1767227400,23.0,50.0,5400.0,3300.0",
                         "1767229800,23.0,,,3250.0"};
  for (int i = 0; i < 3; ++i) {
    TEST_ASSERT_TRUE(parseEnv(lines[i], rows[i]));
    d.add(rows[i].ts, rows[i].values, rows[i].valid);
  }
  JsonDocument out;
  seriesToJson(d, from, to, out.to<JsonObject>());
  JsonDocument fixture = loadFixture("env-history.json");
  assertMatches(fixture.as<JsonVariantConst>(), out.as<JsonVariantConst>());
}

// ------------------------------------------------------------------ persisted state

void test_persisted_state_round_trip() {
  PersistedState s;
  s.day = 20454;
  s.mlTodayTotal = 230;
  s.usagePerDayMl = 410;
  s.emergencyStop = true;
  s.dispenseInProgress = true;
  s.inProgressChannel = 2;
  s.inProgressMl = 60;
  s.plants[2].lastSessionEndS = 1767175200;
  s.plants[2].mlPerPct = 11.25f;
  s.plants[2].lockedOut = true;
  JsonDocument doc;
  toJson(s, doc.to<JsonObject>());
  std::string text;
  serializeJson(doc, text);
  JsonDocument back;
  deserializeJson(back, text);
  PersistedState r;
  TEST_ASSERT_TRUE(fromJson(back.as<JsonObjectConst>(), r));
  TEST_ASSERT_EQUAL_INT32(20454, r.day);
  TEST_ASSERT_TRUE(r.emergencyStop);
  TEST_ASSERT_TRUE(r.dispenseInProgress);
  TEST_ASSERT_EQUAL(2, r.inProgressChannel);
  TEST_ASSERT_EQUAL_FLOAT(60, r.inProgressMl);
  TEST_ASSERT_EQUAL_INT64(1767175200, r.plants[2].lastSessionEndS);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 11.25f, r.plants[2].mlPerPct);
  TEST_ASSERT_TRUE(r.plants[2].lockedOut);
  TEST_ASSERT_FALSE(r.plants[0].lockedOut);

  JsonDocument wrongVersion;
  deserializeJson(wrongVersion, R"({"v":99})");
  TEST_ASSERT_FALSE(fromJson(wrongVersion.as<JsonObjectConst>(), r));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_comparison_detects_differences);
  RUN_TEST(test_plant_fixture_round_trip);
  RUN_TEST(test_plant_partial_update_keeps_other_fields);
  RUN_TEST(test_plant_validation_errors);
  RUN_TEST(test_settings_fixture_round_trip);
  RUN_TEST(test_settings_token_is_write_only);
  RUN_TEST(test_settings_validation);
  RUN_TEST(test_status_matches_fixture);
  RUN_TEST(test_alerts_match_fixture);
  RUN_TEST(test_events_match_fixture);
  RUN_TEST(test_history_matches_fixture);
  RUN_TEST(test_env_history_matches_fixture);
  RUN_TEST(test_persisted_state_round_trip);
  return UNITY_END();
}
