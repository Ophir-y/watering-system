// Contract tests: firmware JSON must match shared/api-fixtures, which the dashboard
// also uses for its types, mock device and tests.
#include <unity.h>

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

#include "Alerts.h"
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
  TEST_ASSERT_EQUAL_STRING("a0b1c2d3e4f5", p.node.c_str());
  TEST_ASSERT_EQUAL_FLOAT(1500, p.bottle.capacityMl);
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

void test_plant_node_can_be_unlinked() {
  PlantProfile p;
  std::string err;
  TEST_ASSERT_TRUE(parsePlant(R"({"id":"fern","name":"Fern","node":"a0b1c2d3e4f5"})", p, err));
  TEST_ASSERT_EQUAL_STRING("a0b1c2d3e4f5", p.node.c_str());
  TEST_ASSERT_TRUE(parsePlant(R"({"name":"Fern"})", p, err));
  TEST_ASSERT_EQUAL_STRING("a0b1c2d3e4f5", p.node.c_str());  // absent: kept
  TEST_ASSERT_TRUE(parsePlant(R"({"node":null})", p, err));
  TEST_ASSERT_TRUE(p.node.empty());
  JsonDocument out;
  toJson(p, out.to<JsonObject>());
  TEST_ASSERT_TRUE(out["node"].isNull());
}

void test_plant_validation_errors() {
  struct Case {
    const char* json;
    const char* error;
  };
  const Case cases[] = {
      {R"({"id":"Bad Id","name":"x"})", "id must be"},
      {R"({"id":"a","name":""})", "name is required"},
      {R"({"id":"a","name":"x","node":"A0B1C2D3E4F5"})", "node must be"},
      {R"({"id":"a","name":"x","node":"a0b1"})", "node must be"},
      {R"({"id":"a","name":"x","node":7})", "node must be a string"},
      {R"({"id":"a","name":"x","bottle":{"capacityMl":50}})", "bottle.capacityMl must be"},
      {R"({"id":"a","name":"x","bottle":{"capacityMl":400,"reserveMl":250}})", "bottle.reserveMl must be"},
      {R"({"id":"a","name":"x","moisture":{"min":60}})", "moisture must satisfy"},
      {R"({"id":"a","name":"x","potVolumeMl":"big"})", "potVolumeMl must be a number"},
      {R"({"id":"a","name":"x","style":"drown_it"})", "style has an unknown value"},
      {R"({"id":"a","name":"x","dormantMonths":[0]})", "dormantMonths must be"},
      {R"({"id":"a","name":"x","maxPulseMl":1})", "maxPulseMl must be"},
      {R"({"id":"a","name":"x","pump":{"flowMlPerS":0}})", "pump.flowMlPerS must be"},
      {R"({"id":"a","name":"x","pump":{"currentSense":1}})", "currentSense must be"},
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
      R"({"checkInMin":0})",
      R"({"checkInMin":90})",
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

// ------------------------------------------------------------------ node config

void test_node_config_round_trip_and_size() {
  JsonDocument fixture = loadFixture("plant.json");
  PlantProfile p;
  std::string err;
  TEST_ASSERT_TRUE(fromJson(fixture.as<JsonObjectConst>(), p, err));
  Settings s;
  s.waterWindowStartH = 6;
  s.checkInMin = 10;
  s.limits.hardMaxPulseMl = 200;
  s.notify.token = "never-sent";

  JsonDocument doc;
  nodeConfigToJson(s, p, doc.to<JsonObject>());
  std::string text;
  serializeJson(doc, text);
  // Sent to the node in radio-sized chunks: keep it a handful of frames.
  TEST_ASSERT_TRUE_MESSAGE(text.size() < 900, std::to_string(text.size()).c_str());
  TEST_ASSERT_TRUE(text.find("careNotes") == std::string::npos);
  TEST_ASSERT_TRUE(text.find("never-sent") == std::string::npos);

  Settings ns;
  PlantProfile np;
  JsonDocument back;
  deserializeJson(back, text);
  TEST_ASSERT_TRUE_MESSAGE(nodeConfigFromJson(back.as<JsonObjectConst>(), ns, np, err), err.c_str());
  TEST_ASSERT_EQUAL_STRING("monstera", np.id.c_str());
  TEST_ASSERT_EQUAL_STRING("Monstera", np.name.c_str());
  TEST_ASSERT_EQUAL_FLOAT(1.8f, np.pump.flowMlPerS);
  TEST_ASSERT_EQUAL_FLOAT(1500, np.bottle.capacityMl);
  TEST_ASSERT_EQUAL_INT32(9800, np.cal.rawWet);
  TEST_ASSERT_EQUAL(6, ns.waterWindowStartH);
  TEST_ASSERT_EQUAL(10, ns.checkInMin);
  TEST_ASSERT_EQUAL_FLOAT(200, ns.limits.hardMaxPulseMl);
}

void test_node_config_rejects_bad_input_without_changes() {
  Settings s;
  PlantProfile p;
  p.id = "fern";
  p.name = "Fern";
  std::string err;
  JsonDocument bad;
  deserializeJson(bad, R"({"plant":{"id":"fern","name":"Fern","moisture":{"min":90}},"settings":{}})");
  TEST_ASSERT_FALSE(nodeConfigFromJson(bad.as<JsonObjectConst>(), s, p, err));
  TEST_ASSERT_EQUAL_FLOAT(35, p.moisture.min);
  JsonDocument missing;
  deserializeJson(missing, R"({"plant":{"id":"fern","name":"Fern"}})");
  TEST_ASSERT_FALSE(nodeConfigFromJson(missing.as<JsonObjectConst>(), s, p, err));
}

// ------------------------------------------------------------------ alerts / events

void test_alerts_match_fixture() {
  Alert low;
  low.id = 7;
  low.code = AlertCode::BottleLow;
  low.severity = Severity::Warning;
  low.channel = 0;
  low.active = true;
  low.raisedAt = 1767250000;
  low.message = "Monstera's bottle is low: about 300 ml (20%), about 1.9 days left.";
  Alert sensor;
  sensor.id = 5;
  sensor.code = AlertCode::SensorFault;
  sensor.severity = Severity::Warning;
  sensor.channel = 1;
  sensor.acked = true;
  sensor.raisedAt = 1767200000;
  sensor.clearedAt = 1767210000;
  sensor.message = "Snake plant: moisture sensor looks disconnected or shorted.";
  Alert clock;
  clock.id = 3;
  clock.code = AlertCode::TimeNotSet;
  clock.severity = Severity::Warning;
  clock.raisedAt = 1767190000;
  clock.clearedAt = 1767190100;
  clock.message = "The clock is not set.";

  JsonDocument out;
  JsonArray arr = out["alerts"].to<JsonArray>();
  toJson(low, "monstera", arr.add<JsonObject>());
  toJson(sensor, "snake-plant", arr.add<JsonObject>());
  toJson(clock, "", arr.add<JsonObject>());
  JsonDocument fixture = loadFixture("alerts.json");
  assertMatches(fixture.as<JsonVariantConst>(), out.as<JsonVariantConst>());
}

void test_alert_codes_have_names() {
  for (uint8_t i = 0; i < static_cast<uint8_t>(AlertCode::Count); ++i) {
    const AlertCode code = static_cast<AlertCode>(i);
    AlertCode back = AlertCode::Count;
    TEST_ASSERT_TRUE(parseAlertCode(alertCodeName(code), back));
    TEST_ASSERT_EQUAL(code, back);
  }
  AlertCode x;
  TEST_ASSERT_FALSE(parseAlertCode("reservoir_low", x));
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
  a.code = "bottle_low";
  a.detail = "Monstera's bottle is low: about 300 ml (20%), about 1.9 days left.";
  Event r;
  r.ts = 1767240000;
  r.type = EventType::Refill;
  r.code = "refill";
  r.ml = 1500;
  r.detail = "Bottle refilled";
  Event sys;
  sys.ts = 1767230000;
  sys.code = "automation_off";
  sys.detail = "Automatic watering paused";

  JsonDocument out;
  JsonArray arr = out["events"].to<JsonArray>();
  toJson(w, "monstera", arr.add<JsonObject>());
  toJson(a, "monstera", arr.add<JsonObject>());
  toJson(r, "monstera", arr.add<JsonObject>());
  toJson(sys, "", arr.add<JsonObject>());
  JsonDocument fixture = loadFixture("events.json");
  assertMatches(fixture.as<JsonVariantConst>(), out.as<JsonVariantConst>());
}

// ------------------------------------------------------------------ history

void test_history_matches_fixture() {
  const EpochS from = 1767225600, to = from + 3 * 3600;
  Downsampler d(from, to, 3, 2);
  const char* lines[] = {"1767226200,monstera,15200,40.0,1210,4380", "1767227400,monstera,15000,42.0,1190,4370",
                         "1767229800,monstera,12100,55.0,1090,"};
  for (const char* line : lines) {
    ReadingRow r;
    TEST_ASSERT_TRUE(parseReading(line, r));
    const float v[2] = {r.pct, r.bottleMl};
    const bool ok[2] = {true, r.bottleMl >= 0};
    d.add(r.ts, v, ok);
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
  // The middle row was logged by the single-board build (fifth column: reservoir ml).
  const char* lines[] = {"1767226200,22.0,50.0,5000.0", "1767227400,23.0,50.0,5400.0,3300.0",
                         "1767229800,23.0,,"};
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
  s.stopped = true;
  s.dispenseInProgress = true;
  s.inProgressMl = 60;
  s.plant.lastSessionEndS = 1767175200;
  s.plant.mlPerPct = 11.25f;
  s.plant.lockedOut = true;
  s.bottle.ml = 640;
  s.bottle.pumpedSinceRefillMl = 820;
  s.bottle.knownFull = true;
  s.bottle.flowFactor = 1.125f;
  s.bottle.usagePerDayMl = 140;
  s.bottle.empty = true;
  s.bottle.emptyReason = static_cast<uint8_t>(EmptyReason::PumpDry);
  s.pump.wetMaPerV = 41.5f;
  s.pump.runs = 7;
  JsonDocument doc;
  toJson(s, doc.to<JsonObject>());
  std::string text;
  serializeJson(doc, text);
  JsonDocument back;
  deserializeJson(back, text);
  PersistedState r;
  TEST_ASSERT_TRUE(fromJson(back.as<JsonObjectConst>(), r));
  TEST_ASSERT_EQUAL_INT32(20454, r.day);
  TEST_ASSERT_TRUE(r.stopped);
  TEST_ASSERT_TRUE(r.dispenseInProgress);
  TEST_ASSERT_EQUAL_FLOAT(60, r.inProgressMl);
  TEST_ASSERT_EQUAL_INT64(1767175200, r.plant.lastSessionEndS);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 11.25f, r.plant.mlPerPct);
  TEST_ASSERT_TRUE(r.plant.lockedOut);
  TEST_ASSERT_EQUAL_FLOAT(640, r.bottle.ml);
  TEST_ASSERT_EQUAL_FLOAT(820, r.bottle.pumpedSinceRefillMl);
  TEST_ASSERT_TRUE(r.bottle.knownFull);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.125f, r.bottle.flowFactor);
  TEST_ASSERT_TRUE(r.bottle.empty);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(EmptyReason::PumpDry), r.bottle.emptyReason);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 41.5f, r.pump.wetMaPerV);
  TEST_ASSERT_EQUAL(7, r.pump.runs);

  PersistedState idle;
  JsonDocument d2;
  toJson(idle, d2.to<JsonObject>());
  TEST_ASSERT_TRUE(fromJson(d2.as<JsonObjectConst>(), r));
  TEST_ASSERT_FALSE(r.dispenseInProgress);
  TEST_ASSERT_EQUAL_FLOAT(-1, r.bottle.ml);  // never set: the gauge starts full

  JsonDocument oldVersion;
  deserializeJson(oldVersion, R"({"v":1,"plants":[]})");
  TEST_ASSERT_FALSE(fromJson(oldVersion.as<JsonObjectConst>(), r));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_comparison_detects_differences);
  RUN_TEST(test_plant_fixture_round_trip);
  RUN_TEST(test_plant_partial_update_keeps_other_fields);
  RUN_TEST(test_plant_node_can_be_unlinked);
  RUN_TEST(test_plant_validation_errors);
  RUN_TEST(test_settings_fixture_round_trip);
  RUN_TEST(test_settings_token_is_write_only);
  RUN_TEST(test_settings_validation);
  RUN_TEST(test_node_config_round_trip_and_size);
  RUN_TEST(test_node_config_rejects_bad_input_without_changes);
  RUN_TEST(test_alerts_match_fixture);
  RUN_TEST(test_alert_codes_have_names);
  RUN_TEST(test_events_match_fixture);
  RUN_TEST(test_history_matches_fixture);
  RUN_TEST(test_env_history_matches_fixture);
  RUN_TEST(test_persisted_state_round_trip);
  return UNITY_END();
}
