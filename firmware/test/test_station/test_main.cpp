// The main station core with simulated nodes on an in-memory radio.
#include <unity.h>

#include <fstream>
#include <sstream>

#include "../link_sim.h"
#include "Json.h"
#include "PumpMonitor.h"
#include "Station.h"

using namespace plantcore;
using namespace plantlink;
using namespace linksim;

void setUp() {}
void tearDown() {}

struct Sink : IStationSink {
  std::vector<std::pair<Event, std::string>> events;
  std::vector<ReadingRow> readings;
  std::vector<PlantProfile> savedPlants;
  std::vector<NodeRecord> savedNodes;
  bool savedStopped = false;
  int keys = 0;
  bool failFlash = false;
  void onEvent(const Event& e, const std::string& plantId) override { events.push_back({e, plantId}); }
  void onReading(const ReadingRow& r) override { readings.push_back(r); }
  bool persistPlants(const std::vector<PlantProfile>& p) override {
    if (failFlash) return false;
    savedPlants = p;
    return true;
  }
  bool persistSettings(const Settings&) override { return !failFlash; }
  bool persistNodes(const std::vector<NodeRecord>& n) override {
    savedNodes = n;
    return true;
  }
  bool persistStopped(bool s) override {
    savedStopped = s;
    return true;
  }
  Key newKey() override { return testKey(static_cast<uint8_t>(++keys)); }
  int count(EventType t, const std::string& code, const std::string& plantId = "*") const {
    int n = 0;
    for (const auto& e : events) {
      if (e.first.type == t && e.first.code == code && (plantId == "*" || e.second == plantId)) n++;
    }
    return n;
  }
};

struct Rig {
  Sink sink;
  Station station{sink};
  int64_t now = 1000;
  Settings settings;

  Rig() {
    settings.tzName = "Asia/Jerusalem";
    station.begin(settings, {}, {}, false);
    station.setRadioChannel(6);
    setClock(1767254400);
  }
  void setClock(EpochS t) {
    ClockInfo c;
    c.valid = true;
    c.now = t;
    c.utcOffsetS = 7200;
    station.setClock(c);
  }
  void advance(int64_t ms) {
    now += ms;
    setClock(1767254400 + (now - 1000) / 1000);
    station.tick(now);
  }
  StationFn radio() {
    return [this](const Mac& from, const uint8_t* d, size_t n, uint8_t* out) {
      return station.onFrame(from, d, n, now, out);
    };
  }
  PlantProfile profile(const std::string& name, const SimNode* node) {
    PlantProfile p;
    p.name = name;
    if (node) p.node = macToId(node->mac);
    return p;
  }
  // The node says Hello, then you add it as a plant, then it pairs.
  std::string addPlant(SimNode& node, const std::string& name) {
    checkIn(node, radio());
    PlantProfile p = profile(name, &node);
    TEST_ASSERT_EQUAL_STRING("", station.savePlant(p, "").c_str());
    checkIn(node, radio());
    TEST_ASSERT_TRUE(node.session.paired());
    return p.id;
  }
  JsonDocument status() {
    JsonDocument doc;
    SystemInfo sys;
    sys.version = "0.2.0";
    station.statusToJson(sys, now, doc.to<JsonObject>());
    return doc;
  }
  int activeFor(AlertCode code, const std::string& plantId) const {
    int n = 0;
    for (const auto& a : station.alerts().all()) {
      if (a.active && a.code == code && station.plantIdForChannel(a.channel) == plantId) n++;
    }
    return n;
  }
};

static plantcore::Event alertEvent(EventType type, const char* code, Severity sev, const char* msg) {
  Event e;
  e.type = type;
  e.code = code;
  e.severity = sev;
  e.detail = msg;
  return e;
}

void test_a_waiting_node_becomes_a_plant() {
  Rig rig;
  SimNode node(1);
  checkIn(node, rig.radio());
  JsonDocument st = rig.status();
  TEST_ASSERT_EQUAL(1, st["newNodes"].size());
  TEST_ASSERT_EQUAL_STRING("a0b1c2d3e401", st["newNodes"][0]["id"]);
  TEST_ASSERT_EQUAL_STRING("esp32-c3-supermini", st["newNodes"][0]["board"]);
  TEST_ASSERT_EQUAL(1, rig.sink.count(EventType::System, "node_found"));

  PlantProfile p = rig.profile("Boston Fern", &node);
  TEST_ASSERT_EQUAL_STRING("", rig.station.savePlant(p, "").c_str());
  TEST_ASSERT_EQUAL_STRING("boston-fern", p.id.c_str());
  TEST_ASSERT_EQUAL(1, rig.sink.savedNodes.size());
  checkIn(node, rig.radio());
  TEST_ASSERT_TRUE(node.session.paired());
  // The node got its plant's config in the same check-in.
  TEST_ASSERT_EQUAL(1, node.app.configs.size());
  JsonDocument cfg;
  deserializeJson(cfg, node.app.configs[0]);
  TEST_ASSERT_EQUAL_STRING("boston-fern", cfg["plant"]["id"]);
  st = rig.status();
  TEST_ASSERT_EQUAL(0, st["newNodes"].size());
  TEST_ASSERT_EQUAL_STRING("a0b1c2d3e401", st["plants"][0]["node"]);
  TEST_ASSERT_TRUE(st["plants"][0]["online"].as<bool>());
  TEST_ASSERT_FALSE(st["plants"][0]["configPending"].as<bool>());
  TEST_ASSERT_EQUAL(1, st["radio"]["online"].as<int>());
}

void test_settings_changes_reach_every_node() {
  Rig rig;
  SimNode a(1), b(2);
  rig.addPlant(a, "Fern");
  rig.addPlant(b, "Basil");
  Settings s = rig.station.settings();
  s.waterWindowStartH = 6;
  TEST_ASSERT_EQUAL_STRING("", rig.station.saveSettings(s).c_str());
  checkIn(a, rig.radio());
  checkIn(b, rig.radio());
  TEST_ASSERT_EQUAL(2, a.app.configs.size());
  TEST_ASSERT_EQUAL(2, b.app.configs.size());
  JsonDocument cfg;
  deserializeJson(cfg, b.app.configs[1]);
  TEST_ASSERT_EQUAL(6, cfg["settings"]["waterWindow"]["startH"].as<int>());
}

void test_two_plants_cannot_share_a_node() {
  Rig rig;
  SimNode node(1);
  rig.addPlant(node, "Fern");
  PlantProfile p = rig.profile("Basil", &node);
  const std::string err = rig.station.savePlant(p, "");
  TEST_ASSERT_TRUE(err.find("already waters") != std::string::npos);
  PlantProfile bad = rig.profile("Moss", nullptr);
  bad.node = "nothex000000";
  TEST_ASSERT_TRUE(rig.station.savePlant(bad, "").find("node must be") != std::string::npos);
  rig.sink.failFlash = true;
  PlantProfile ok = rig.profile("Moss", nullptr);
  TEST_ASSERT_EQUAL_STRING("could not save to flash", rig.station.savePlant(ok, "").c_str());
  TEST_ASSERT_EQUAL(1, rig.station.plants().size());
}

void test_water_now_is_queued_and_delivered() {
  Rig rig;
  SimNode node(1);
  const std::string id = rig.addPlant(node, "Fern");
  rig.advance(120000);
  CommandResult r = rig.station.water(id, 80);
  TEST_ASSERT_EQUAL_STRING("", r.error.c_str());
  TEST_ASSERT_TRUE(r.id > 0);
  TEST_ASSERT_EQUAL(180, r.etaS);  // checks in every 5 minutes, last seen 2 minutes ago
  JsonDocument st = rig.status();
  TEST_ASSERT_EQUAL(1, st["plants"][0]["queued"].size());
  TEST_ASSERT_EQUAL_STRING("water", st["plants"][0]["queued"][0]["kind"]);
  checkIn(node, rig.radio());
  TEST_ASSERT_EQUAL(1, node.app.commands.size());
  TEST_ASSERT_EQUAL_FLOAT(80, node.app.commands[0].arg);
  TEST_ASSERT_EQUAL(0, rig.status()["plants"][0]["queued"].size());

  TEST_ASSERT_EQUAL_STRING("invalid_amount", rig.station.water(id, 1000).error.c_str());
  TEST_ASSERT_EQUAL_STRING("not_found", rig.station.water("nope", 50).error.c_str());
  PlantProfile manual = rig.profile("Moss", nullptr);
  rig.station.savePlant(manual, "");
  TEST_ASSERT_EQUAL_STRING("no_node", rig.station.water(manual.id, 50).error.c_str());
}

void test_node_alerts_become_station_alerts() {
  Rig rig;
  SimNode a(1), b(2);
  const std::string fern = rig.addPlant(a, "Fern");
  const std::string basil = rig.addPlant(b, "Basil");
  b.session.queueEvent(alertEvent(EventType::AlertRaised, "bottle_low", Severity::Warning, "Basil's bottle is low."));
  checkIn(b, rig.radio());
  TEST_ASSERT_EQUAL(1, rig.activeFor(AlertCode::BottleLow, basil));
  TEST_ASSERT_EQUAL(0, rig.activeFor(AlertCode::BottleLow, fern));
  TEST_ASSERT_EQUAL(1, rig.sink.count(EventType::AlertRaised, "bottle_low", basil));
  TEST_ASSERT_EQUAL(1, rig.status()["alerts"]["warning"].as<int>());

  // Escalation: the same alert again with a higher severity.
  b.session.queueEvent(alertEvent(EventType::AlertRaised, "bottle_low", Severity::Critical, "Basil: empty soon."));
  checkIn(b, rig.radio());
  TEST_ASSERT_EQUAL(1, rig.status()["alerts"]["critical"].as<int>());

  b.session.queueEvent(alertEvent(EventType::AlertCleared, "bottle_low", Severity::Info, ""));
  checkIn(b, rig.radio());
  TEST_ASSERT_EQUAL(0, rig.activeFor(AlertCode::BottleLow, basil));
  // Ordinary events are logged under the plant.
  Event w;
  w.type = EventType::Watering;
  w.code = "auto";
  w.ml = 120;
  a.session.queueEvent(w);
  checkIn(a, rig.radio());
  TEST_ASSERT_EQUAL(1, rig.sink.count(EventType::Watering, "auto", fern));
}

void test_lost_alert_events_are_caught_up_from_the_report() {
  Rig rig;
  SimNode node(1);
  const std::string id = rig.addPlant(node, "Fern");
  node.app.base.activeAlerts = 1u << static_cast<uint8_t>(AlertCode::BottleEmpty);
  checkIn(node, rig.radio());
  TEST_ASSERT_EQUAL(1, rig.activeFor(AlertCode::BottleEmpty, id));
  node.app.base.activeAlerts = 0;
  checkIn(node, rig.radio());
  TEST_ASSERT_EQUAL(0, rig.activeFor(AlertCode::BottleEmpty, id));
}

void test_silent_node_raises_offline_alert() {
  Rig rig;
  SimNode node(1);
  const std::string id = rig.addPlant(node, "Fern");
  rig.advance(10 * 60000);
  TEST_ASSERT_EQUAL(0, rig.activeFor(AlertCode::NodeOffline, id));
  rig.advance(7 * 60000);  // 17 minutes: three check-ins missed
  TEST_ASSERT_EQUAL(1, rig.activeFor(AlertCode::NodeOffline, id));
  TEST_ASSERT_FALSE(rig.status()["plants"][0]["online"].as<bool>());
  checkIn(node, rig.radio());
  rig.advance(1000);
  TEST_ASSERT_EQUAL(0, rig.activeFor(AlertCode::NodeOffline, id));
}

void test_emergency_stop_rides_on_every_reply() {
  Rig rig;
  SimNode node(1);
  const std::string id = rig.addPlant(node, "Fern");
  rig.station.emergencyStop("stopped from the dashboard");
  TEST_ASSERT_TRUE(rig.sink.savedStopped);
  TEST_ASSERT_EQUAL_STRING("emergency_stop", rig.station.water(id, 50).error.c_str());
  checkIn(node, rig.radio());
  TEST_ASSERT_TRUE(node.app.states.back().stop);
  rig.station.resume();
  checkIn(node, rig.radio());
  TEST_ASSERT_FALSE(node.app.states.back().stop);
  TEST_ASSERT_EQUAL(1, rig.sink.count(EventType::System, "emergency_stop"));
  TEST_ASSERT_EQUAL(1, rig.sink.count(EventType::System, "resume"));
  // The time and room climate ride along too.
  TEST_ASSERT_EQUAL_UINT32(1767254400, node.app.states.back().epoch);
  TEST_ASSERT_EQUAL_INT32(7200, node.app.states.back().utcOffsetS);
}

void test_calibrate_from_the_latest_reading() {
  Rig rig;
  SimNode node(1);
  const std::string id = rig.addPlant(node, "Fern");
  node.app.base.raw = 9100;  // the sensor is in a glass of water
  checkIn(node, rig.radio());
  rig.advance(20000);
  int32_t raw = 0;
  int64_t age = 0;
  TEST_ASSERT_EQUAL_STRING("", rig.station.calibrate(id, "wet", raw, age).c_str());
  TEST_ASSERT_EQUAL_INT32(9100, raw);
  TEST_ASSERT_EQUAL(20, age);
  TEST_ASSERT_EQUAL_INT32(9100, rig.station.plant(id)->cal.rawWet);
  TEST_ASSERT_TRUE(rig.status()["plants"][0]["configPending"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("point must be dry or wet", rig.station.calibrate(id, "damp", raw, age).c_str());
}

void test_deleting_a_plant_unpairs_its_node() {
  Rig rig;
  SimNode node(1);
  const std::string id = rig.addPlant(node, "Fern");
  TEST_ASSERT_EQUAL_STRING("", rig.station.deletePlant(id).c_str());
  checkIn(node, rig.radio());
  TEST_ASSERT_FALSE(node.session.paired());
  TEST_ASSERT_EQUAL(0, rig.sink.savedNodes.size());
  checkIn(node, rig.radio());
  TEST_ASSERT_EQUAL(1, rig.status()["newNodes"].size());  // free to be added again
}

void test_readings_are_logged_at_the_interval() {
  Rig rig;
  SimNode node(1);
  rig.addPlant(node, "Fern");
  const size_t first = rig.sink.readings.size();
  TEST_ASSERT_EQUAL(1, first);
  TEST_ASSERT_EQUAL_FLOAT(900, rig.sink.readings[0].bottleMl);
  TEST_ASSERT_EQUAL_FLOAT(4300, rig.sink.readings[0].batteryMv);
  rig.advance(5 * 60000);
  checkIn(node, rig.radio());
  TEST_ASSERT_EQUAL(1, rig.sink.readings.size());  // 10-minute log interval
  rig.advance(5 * 60000);
  checkIn(node, rig.radio());
  TEST_ASSERT_EQUAL(2, rig.sink.readings.size());
}

void test_button_press_highlights_the_plant() {
  Rig rig;
  SimNode node(1);
  rig.addPlant(node, "Fern");
  node.session.pressButton();
  checkIn(node, rig.radio());
  TEST_ASSERT_TRUE(rig.status()["plants"][0]["buttonPressed"].as<bool>());
  rig.advance(kButtonHighlightMs);
  TEST_ASSERT_FALSE(rig.status()["plants"][0]["buttonPressed"].as<bool>());
}

void test_node_that_lost_its_key_can_be_paired_again() {
  Rig rig;
  SimNode node(1);
  const std::string id = rig.addPlant(node, "Fern");
  SimNode wiped(1);  // same board, flash erased
  checkIn(wiped, rig.radio());
  TEST_ASSERT_FALSE(wiped.session.paired());
  TEST_ASSERT_TRUE(rig.status()["plants"][0]["repairRequested"].as<bool>());
  TEST_ASSERT_EQUAL(1, rig.sink.count(EventType::System, "node_repair", id));
  TEST_ASSERT_EQUAL_STRING("", rig.station.repairNode("a0b1c2d3e401").c_str());
  checkIn(wiped, rig.radio());
  TEST_ASSERT_TRUE(wiped.session.paired());
  TEST_ASSERT_EQUAL(1, wiped.app.configs.size());
  TEST_ASSERT_FALSE(rig.status()["plants"][0]["repairRequested"].as<bool>());
}

// ------------------------------------------------------------------ the API contract

static std::string readFixture(const char* name) {
  std::ifstream f(std::string(FIXTURES_DIR) + "/" + name);
  TEST_ASSERT_TRUE_MESSAGE(f.good(), name);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// Same keys, same types, numbers within 0.01.
static bool same(JsonVariantConst a, JsonVariantConst b, const std::string& path, std::string& why) {
  if (a.is<JsonObjectConst>()) {
    if (!b.is<JsonObjectConst>()) return why = path + ": expected an object", false;
    for (JsonPairConst kv : b.as<JsonObjectConst>()) {
      if (a[kv.key()].isUnbound()) return why = path + "." + kv.key().c_str() + ": unexpected key", false;
    }
    for (JsonPairConst kv : a.as<JsonObjectConst>()) {
      if (b[kv.key()].isUnbound()) return why = path + "." + kv.key().c_str() + ": missing", false;
      if (!same(kv.value(), b[kv.key()], path + "." + kv.key().c_str(), why)) return false;
    }
    return true;
  }
  if (a.is<JsonArrayConst>()) {
    if (!b.is<JsonArrayConst>() || a.size() != b.size()) return why = path + ": array differs", false;
    for (size_t i = 0; i < a.size(); ++i) {
      if (!same(a[i], b[i], path + "[" + std::to_string(i) + "]", why)) return false;
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
    return why = path + ": expected " + a.as<std::string>() + " got " + b.as<std::string>(), false;
  }
  return why = path + ": unsupported type", false;
}

void test_status_matches_fixture() {
  Rig rig;
  rig.station.setEnvironment(EnvSnapshot{true, 23.4f, 48, true, 5400});
  SimNode monstera(1);
  monstera.app.base.moisturePct = 33.2f;
  monstera.app.base.raw = 17348;
  monstera.app.base.planner = static_cast<uint8_t>(PlannerState::Pulsing);
  monstera.app.base.triggerPct = 36.5f;
  monstera.app.base.targetPct = 55;
  monstera.app.base.mlToday = 120;
  monstera.app.base.lastWateredAt = 1767175200;
  monstera.app.base.mlPerPct = 11.25f;
  monstera.app.base.batteryMv = 4210;
  monstera.app.base.bottleMl = 312;
  monstera.app.base.bottleCapacityMl = 1500;
  monstera.app.base.daysLeft = 1.4f;
  monstera.app.base.bottleLow = true;
  monstera.app.base.flowFactor = 1.125f;
  monstera.app.base.pumpReady = true;
  monstera.app.base.doseActive = true;
  monstera.app.base.doseMl = 60;
  monstera.app.base.doseDispensedMl = 22.5f;
  const std::string m = rig.addPlant(monstera, "Monstera");
  checkIn(monstera, rig.radio());  // a second report so the link quality is known

  SimNode snake(2);
  snake.app.base.health = static_cast<uint8_t>(SensorHealth::Failed);
  snake.app.base.issue = static_cast<uint8_t>(SensorIssue::Disconnected);
  snake.app.base.haveMoisture = false;
  snake.app.base.triggerPct = 15;
  snake.app.base.targetPct = 40;
  snake.app.base.batteryMv = 3520;
  snake.app.base.batteryLow = true;
  snake.app.base.bottleMl = 0;
  snake.app.base.bottleCapacityMl = 800;
  snake.app.base.bottleEmpty = true;
  snake.app.base.emptyReason = static_cast<uint8_t>(EmptyReason::PumpDry);
  snake.app.base.pumpVerdict = static_cast<uint8_t>(PumpVerdict::Dry);
  snake.app.base.lockedOut = false;
  rig.addPlant(snake, "Snake plant");

  PlantProfile pothos = rig.profile("Pothos", nullptr);
  pothos.moisture = {15, 25, 50, 70};
  rig.station.savePlant(pothos, "");

  SimNode waiting(3);
  checkIn(waiting, rig.radio());

  rig.advance(90000);
  rig.station.water(m, 80);
  snake.session.queueEvent(alertEvent(EventType::AlertRaised, "sensor_fault", Severity::Warning,
                                      "Snake plant: moisture sensor looks disconnected or shorted."));
  checkIn(snake, rig.radio());

  JsonDocument out;
  SystemInfo sys;
  sys.version = "0.2.0";
  sys.uptimeS = 86400;
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
  rig.station.statusToJson(sys, rig.now, out.to<JsonObject>());
  std::string actual;
  serializeJsonPretty(out, actual);
  JsonDocument fixture;
  DeserializationError err = deserializeJson(fixture, readFixture("status.json"));
  TEST_ASSERT_TRUE_MESSAGE(!err, err.c_str());
  std::string why;
  const bool ok = same(fixture.as<JsonVariantConst>(), out.as<JsonVariantConst>(), "$", why);
  if (!ok) printf("actual status:\n%s\n", actual.c_str());
  TEST_ASSERT_TRUE_MESSAGE(ok, why.c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_waiting_node_becomes_a_plant);
  RUN_TEST(test_settings_changes_reach_every_node);
  RUN_TEST(test_two_plants_cannot_share_a_node);
  RUN_TEST(test_water_now_is_queued_and_delivered);
  RUN_TEST(test_node_alerts_become_station_alerts);
  RUN_TEST(test_lost_alert_events_are_caught_up_from_the_report);
  RUN_TEST(test_silent_node_raises_offline_alert);
  RUN_TEST(test_emergency_stop_rides_on_every_reply);
  RUN_TEST(test_calibrate_from_the_latest_reading);
  RUN_TEST(test_deleting_a_plant_unpairs_its_node);
  RUN_TEST(test_readings_are_logged_at_the_interval);
  RUN_TEST(test_button_press_highlights_the_plant);
  RUN_TEST(test_node_that_lost_its_key_can_be_paired_again);
  RUN_TEST(test_status_matches_fixture);
  return UNITY_END();
}
