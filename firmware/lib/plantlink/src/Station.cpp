#include "Station.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "Bottle.h"
#include "Engine.h"
#include "EnvModel.h"
#include "Json.h"
#include "Planner.h"
#include "PumpMonitor.h"
#include "SensorMonitor.h"

using namespace plantcore;

namespace plantlink {

namespace {

double r1(double x) { return std::round(x * 10.0) / 10.0; }
double r2(double x) { return std::round(x * 100.0) / 100.0; }

std::string newPlantId(const std::string& name, const std::vector<PlantProfile>& plants) {
  std::string base;
  for (char c : name) {
    if (base.size() >= 16) break;
    if (std::isalnum(static_cast<unsigned char>(c))) {
      base += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
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

// Used when the node's own wording of an alert was lost on the way.
std::string genericMessage(AlertCode code, const std::string& name) {
  const char* what = "needs attention";
  switch (code) {
    case AlertCode::BottleLow: what = "bottle is low"; break;
    case AlertCode::BottleEmpty: what = "bottle is empty: refill it, then press the node's button"; break;
    case AlertCode::PumpDisconnected: what = "pump drew no current: check its wires"; break;
    case AlertCode::PumpJammed: what = "pump looks jammed"; break;
    case AlertCode::BatteryLow: what = "node batteries are low"; break;
    case AlertCode::SensorFault: what = "moisture sensor has a problem"; break;
    case AlertCode::SensorNoResponse: what = "moisture did not rise after watering"; break;
    case AlertCode::PlantTooDry: what = "is very dry"; break;
    case AlertCode::PlantTooWet: what = "has stayed too wet"; break;
    case AlertCode::TargetNotReached: what = "did not reach its target moisture"; break;
    case AlertCode::DailyLimitReached: what = "reached its daily watering limit"; break;
    case AlertCode::TempOutOfRange: what = "temperature is outside its range"; break;
    case AlertCode::HumidityOutOfRange: what = "humidity is outside its range"; break;
    case AlertCode::InterruptedDispense: what = "node restarted while watering"; break;
    case AlertCode::UnexpectedReset: what = "node restarted unexpectedly"; break;
    case AlertCode::CalibrationNeeded: what = "moisture sensor needs calibrating"; break;
    default: break;
  }
  return name + ": " + what + ".";
}

// Alerts only the station raises: a node report never clears them.
bool stationOnly(AlertCode c) {
  return c == AlertCode::NodeOffline || c == AlertCode::EmergencyStop || c == AlertCode::TimeNotSet ||
         c == AlertCode::StorageFault || c == AlertCode::EnvSensorFault;
}

const char* plannerName(uint8_t s) {
  return s <= static_cast<uint8_t>(PlannerState::Soaking) ? plannerStateName(static_cast<PlannerState>(s)) : "idle";
}

}  // namespace

float batteryPct(float mv) {
  if (!(mv > 0)) return -1;
  return std::clamp((mv - kBatteryEmptyMv) / (kBatteryFullMv - kBatteryEmptyMv) * 100.0f, 0.0f, 100.0f);
}

// ------------------------------------------------------------------ setup

void Station::begin(const Settings& s, const std::vector<PlantProfile>& plants, const std::vector<NodeRecord>& nodes,
                    bool stopped) {
  settings_ = s;
  plants_ = plants;
  stopped_ = stopped;
  fleet_.restore(nodes);
  assignSlots();
  pushConfigs();
  if (stopped_) {
    raise(AlertCode::EmergencyStop, Severity::Info, -1,
          "Emergency stop is still active after a restart. Resume from the dashboard.");
  }
}

int Station::plantIndex(const std::string& id) const {
  for (size_t i = 0; i < plants_.size(); ++i) {
    if (plants_[i].id == id) return static_cast<int>(i);
  }
  return -1;
}

int Station::plantForNode(const std::string& nodeId) const {
  for (size_t i = 0; i < plants_.size(); ++i) {
    if (!nodeId.empty() && plants_[i].node == nodeId) return static_cast<int>(i);
  }
  return -1;
}

const PlantProfile* Station::plant(const std::string& id) const {
  const int i = plantIndex(id);
  return i >= 0 ? &plants_[i] : nullptr;
}

int Station::slotOf(const std::string& plantId) const {
  for (size_t s = 0; s < slots_.size(); ++s) {
    if (slots_[s] == plantId) return static_cast<int>(s);
  }
  return -1;
}

std::string Station::plantIdForChannel(int8_t ch) const {
  return ch >= 0 && ch < static_cast<int>(slots_.size()) ? slots_[ch] : std::string();
}

void Station::assignSlots() {
  for (size_t s = 0; s < slots_.size(); ++s) {
    if (!slots_[s].empty() && plantIndex(slots_[s]) < 0) {
      alerts_.clearChannel(static_cast<int8_t>(s), clock_.now);
      slots_[s].clear();
      lastLogMs_[s] = buttonMs_[s] = 0;
    }
  }
  for (const auto& p : plants_) {
    if (slotOf(p.id) >= 0) continue;
    for (auto& slot : slots_) {
      if (slot.empty()) {
        slot = p.id;
        break;
      }
    }
  }
}

void Station::pushConfigs() {
  for (const auto& p : plants_) {
    if (p.node.empty()) continue;
    JsonDocument doc;
    nodeConfigToJson(settings_, p, doc.to<JsonObject>());
    std::string json;
    serializeJson(doc, json);
    fleet_.setConfig(p.node, json);
  }
}

// ------------------------------------------------------------------ plants and settings

std::string Station::validatePlants(const std::vector<PlantProfile>& plants) const {
  if (plants.size() > static_cast<size_t>(kMaxNodes)) return "at most " + std::to_string(kMaxNodes) + " plants";
  std::string err;
  for (size_t i = 0; i < plants.size(); ++i) {
    if (!validateProfile(plants[i], err)) return plants[i].id + ": " + err;
    for (size_t j = i + 1; j < plants.size(); ++j) {
      if (plants[i].id == plants[j].id) return "duplicate plant id " + plants[i].id;
      if (!plants[i].node.empty() && plants[i].node == plants[j].node) {
        return "node " + plants[i].node + " already waters " + plants[i].name;
      }
    }
  }
  return "";
}

std::string Station::applyPlants(std::vector<PlantProfile> plants) {
  const std::string err = validatePlants(plants);
  if (!err.empty()) return err;
  if (!sink_.persistPlants(plants)) return "could not save to flash";

  // Nodes no plant uses any more are told to unpair; new ones are added.
  for (const auto& old : plants_) {
    if (old.node.empty()) continue;
    const bool kept = std::any_of(plants.begin(), plants.end(), [&](const PlantProfile& p) { return p.node == old.node; });
    if (!kept) fleet_.forget(old.node);
  }
  for (const auto& p : plants) {
    if (p.node.empty()) continue;
    const NodeView* n = fleet_.find(p.node);
    if (!n || !n->adopted) {
      fleet_.adopt(p.node, sink_.newKey());
      onNodeForgotten(p.node);  // announce it again if it ever needs re-pairing
    }
  }
  plants_ = std::move(plants);
  assignSlots();
  pushConfigs();
  return "";
}

std::string Station::savePlant(PlantProfile& input, const std::string& replaceId) {
  std::vector<PlantProfile> plants = plants_;
  PlantProfile p = input;
  if (replaceId.empty()) {
    if (p.id.empty()) p.id = newPlantId(p.name, plants);
    plants.push_back(p);
  } else {
    auto it = std::find_if(plants.begin(), plants.end(), [&](const PlantProfile& x) { return x.id == replaceId; });
    if (it == plants.end()) return "not_found";
    p.id = replaceId;  // ids are immutable: history is keyed by them
    *it = p;
  }
  const std::string err = applyPlants(plants);
  if (err.empty()) input = p;
  return err;
}

std::string Station::deletePlant(const std::string& id) {
  std::vector<PlantProfile> plants = plants_;
  auto it = std::find_if(plants.begin(), plants.end(), [&](const PlantProfile& p) { return p.id == id; });
  if (it == plants.end()) return "not_found";
  plants.erase(it);
  return applyPlants(plants);
}

std::string Station::saveSettings(const Settings& s) {
  std::string err;
  if (!validateSettings(s, err)) return err;
  if (!sink_.persistSettings(s)) return "could not save to flash";
  const bool automationChanged = s.automationEnabled != settings_.automationEnabled;
  settings_ = s;
  pushConfigs();
  if (automationChanged) {
    log(EventType::System, "", s.automationEnabled ? "automation_on" : "automation_off",
        0, s.automationEnabled ? "Automatic watering enabled" : "Automatic watering paused");
  }
  return "";
}

std::string Station::restoreConfig(const Settings& s, const std::vector<PlantProfile>& plants) {
  std::string err;
  if (!validateSettings(s, err)) return "settings: " + err;
  err = validatePlants(plants);
  if (!err.empty()) return err;
  if (!sink_.persistSettings(s)) return "could not save to flash";
  settings_ = s;
  return applyPlants(plants);
}

std::string Station::setAutomation(bool enabled) {
  Settings s = settings_;
  s.automationEnabled = enabled;
  return saveSettings(s);
}

std::string Station::repairNode(const std::string& nodeId) {
  const NodeView* n = fleet_.find(nodeId);
  if (!n) return "not_found";
  if (plantForNode(nodeId) < 0) return "no plant uses this node";
  fleet_.adopt(nodeId, sink_.newKey());
  onNodeForgotten(nodeId);
  pushConfigs();
  return "";
}

// ------------------------------------------------------------------ commands

CommandResult Station::command(const std::string& plantId, CommandKind kind, float arg) {
  CommandResult r;
  const PlantProfile* p = plant(plantId);
  if (!p) {
    r.error = "not_found";
    return r;
  }
  if (p->node.empty()) {
    r.error = "no_node";
    return r;
  }
  r.id = fleet_.queue(p->node, kind, arg);
  if (!r.id) {
    r.error = "busy";
    return r;
  }
  r.etaS = fleet_.etaS(p->node, nowMs_, settings_.checkInMin * 60);
  return r;
}

CommandResult Station::water(const std::string& plantId, float ml) {
  if (!(ml >= kMinDoseMl && ml <= settings_.limits.hardMaxPulseMl)) {
    CommandResult r;
    r.error = "invalid_amount";
    return r;
  }
  if (stopped_) {
    CommandResult r;
    r.error = "emergency_stop";
    return r;
  }
  return command(plantId, CommandKind::Water, ml);
}

CommandResult Station::timedRun(const std::string& plantId, float seconds) {
  if (!(seconds >= 1 && seconds <= 60)) {
    CommandResult r;
    r.error = "invalid_amount";
    return r;
  }
  return command(plantId, CommandKind::TimedRun, seconds);
}

CommandResult Station::refilled(const std::string& plantId, float ml) {
  return command(plantId, CommandKind::Refilled, std::max(0.0f, ml));
}

CommandResult Station::clearFaults(const std::string& plantId) { return command(plantId, CommandKind::ClearFaults, 0); }

CommandResult Station::identify(const std::string& plantId) { return command(plantId, CommandKind::Identify, 0); }

std::string Station::calibrate(const std::string& plantId, const std::string& point, int32_t& raw, int64_t& ageS) {
  if (point != "dry" && point != "wet") return "point must be dry or wet";
  const int i = plantIndex(plantId);
  if (i < 0) return "not_found";
  const NodeView* n = plants_[i].node.empty() ? nullptr : fleet_.find(plants_[i].node);
  if (!n || !n->haveReport) return "no reading from this plant's node yet";
  raw = n->report.raw;
  ageS = (nowMs_ - n->reportMs) / 1000;
  PlantProfile p = plants_[i];
  (point == "dry" ? p.cal.rawDry : p.cal.rawWet) = raw;
  return savePlant(p, plantId);
}

void Station::emergencyStop(const std::string& why) {
  if (stopped_) return;
  stopped_ = true;
  sink_.persistStopped(true);
  raise(AlertCode::EmergencyStop, Severity::Info, -1,
        "Emergency stop (" + why + "). Every node stops watering at its next check-in, until you resume.");
  log(EventType::System, "", "emergency_stop", 0, why);
}

void Station::resume() {
  if (!stopped_) return;
  stopped_ = false;
  sink_.persistStopped(false);
  clear(AlertCode::EmergencyStop, -1);
  log(EventType::System, "", "resume", 0, "Watering resumed");
}

void Station::ackAll() {
  std::vector<uint32_t> ids;
  for (const auto& a : alerts_.all()) {
    if (a.active) ids.push_back(a.id);
  }
  for (uint32_t id : ids) alerts_.ack(id);
}

void Station::setSystemAlert(AlertCode code, Severity sev, bool active, const std::string& message) {
  if (active) {
    raise(code, sev, -1, message);
  } else {
    clear(code, -1);
  }
}

// ------------------------------------------------------------------ radio

ReplyContext Station::replyContext() const {
  ReplyContext c;
  c.epoch = clock_.valid ? static_cast<uint32_t>(clock_.now) : 0;
  c.utcOffsetS = clock_.utcOffsetS;
  c.env = env_;
  c.automation = settings_.automationEnabled;
  c.stop = stopped_;
  c.channel = channel_;
  c.checkInS = static_cast<uint16_t>(settings_.checkInMin * 60);
  return c;
}

size_t Station::onFrame(const Mac& from, const uint8_t* d, size_t n, int64_t nowMs, uint8_t* out) {
  nowMs_ = nowMs;
  return fleet_.onFrame(from, d, n, nowMs, replyContext(), out);
}

void Station::tick(int64_t nowMs) {
  nowMs_ = nowMs;
  const uint16_t checkInS = static_cast<uint16_t>(settings_.checkInMin * 60);
  fleet_.tick(nowMs, checkInS);
  const int64_t offlineMs = int64_t(kOfflineAfterCheckIns) * checkInS * 1000 + 60000;
  for (const auto& p : plants_) {
    const int slot = slotOf(p.id);
    if (p.node.empty() || slot < 0) continue;
    const NodeView* n = fleet_.find(p.node);
    const bool online = n && n->online;
    // Give every node a chance to check in after the station starts.
    if (online || nowMs < offlineMs) {
      clear(AlertCode::NodeOffline, slot);
      continue;
    }
    const int64_t silentMin = n && n->lastSeenMs >= 0 ? (nowMs - n->lastSeenMs) / 60000 : nowMs / 60000;
    char buf[200];
    snprintf(buf, sizeof buf,
             "%s's node has not checked in for %lld minutes. Check its batteries and that it is in range of the "
             "main station.",
             p.name.c_str(), static_cast<long long>(silentMin));
    raise(AlertCode::NodeOffline, Severity::Warning, slot, buf);
  }
}

void Station::onNodeEvent(const NodeView& n, const Event& in) {
  const int i = plantForNode(n.id);
  if (i < 0) return;
  const PlantProfile& p = plants_[i];
  const int slot = slotOf(p.id);
  Event e = in;
  if (e.ts <= 0) e.ts = clock_.valid ? clock_.now : 0;
  // The station logs its own stop and resume.
  if (e.type == EventType::System && (e.code == "emergency_stop" || e.code == "resume")) return;

  AlertCode code;
  if (e.type == EventType::AlertRaised && parseAlertCode(e.code, code)) {
    alerts_.raise(code, e.severity, static_cast<int8_t>(slot), e.detail, e.ts);
  } else if (e.type == EventType::AlertCleared && parseAlertCode(e.code, code)) {
    if (!alerts_.clear(code, static_cast<int8_t>(slot), e.ts)) return;
  }
  sink_.onEvent(e, p.id);
}

void Station::onNodeReport(const NodeView& n) {
  const int i = plantForNode(n.id);
  if (i < 0) return;
  const PlantProfile& p = plants_[i];
  const int slot = slotOf(p.id);
  if (slot < 0) return;
  const ReportMsg& r = n.report;
  clear(AlertCode::NodeOffline, slot);
  if (r.button) buttonMs_[slot] = nowMs_;

  // The report lists the node's active alerts: catch up on anything whose event was lost.
  for (uint8_t c = 0; c < static_cast<uint8_t>(AlertCode::Count); ++c) {
    const AlertCode code = static_cast<AlertCode>(c);
    if (stationOnly(code)) continue;
    const bool nodeHas = (r.activeAlerts >> c) & 1u;
    const bool weHave = alerts_.isActive(code, static_cast<int8_t>(slot));
    if (nodeHas && !weHave && !r.outbox) {
      raise(code, code == AlertCode::BottleEmpty ? Severity::Critical : Severity::Warning, slot,
            genericMessage(code, p.name));
    } else if (!nodeHas && weHave && !r.outbox) {
      clear(code, slot);
    }
  }

  // Moisture history, at the configured interval.
  const int64_t everyMs = int64_t(settings_.logIntervalMin) * 60000;
  if (r.haveMoisture && clock_.valid && (lastLogMs_[slot] == 0 || nowMs_ - lastLogMs_[slot] >= everyMs - 30000)) {
    lastLogMs_[slot] = nowMs_;
    ReadingRow row;
    row.ts = clock_.now;
    row.plantId = p.id;
    row.raw = r.raw;
    row.pct = r.moisturePct;
    row.bottleMl = r.bottleMl;
    row.batteryMv = r.batteryMv > 0 ? r.batteryMv : -1;
    sink_.onReading(row);
  }
}

void Station::onNodeHello(const NodeView& n) {
  if (std::find(announced_.begin(), announced_.end(), n.id) != announced_.end()) return;
  if (n.adopted && !n.wantsRepair) return;
  announced_.push_back(n.id);
  const int i = plantForNode(n.id);
  if (n.wantsRepair && i >= 0) {
    log(EventType::System, plants_[i].id, "node_repair", 0,
        plants_[i].name + "'s node lost its pairing and asks to pair again. Confirm it in the dashboard.");
  } else if (!n.adopted) {
    log(EventType::System, "", "node_found", 0,
        std::string("A new plant node (") + boardName(n.board) + ") is waiting to be added.");
  }
}

void Station::onNodeRecordsChanged() { sink_.persistNodes(fleet_.records()); }

void Station::onNodeForgotten(const std::string& id) {
  announced_.erase(std::remove(announced_.begin(), announced_.end(), id), announced_.end());
}

// ------------------------------------------------------------------ helpers

void Station::raise(AlertCode code, Severity sev, int slot, const std::string& msg) {
  const std::string plantId = plantIdForChannel(static_cast<int8_t>(slot));
  if (alerts_.raise(code, sev, static_cast<int8_t>(slot), msg, clock_.now)) {
    Event e;
    e.ts = clock_.valid ? clock_.now : 0;
    e.type = EventType::AlertRaised;
    e.code = alertCodeName(code);
    e.detail = msg;
    e.severity = sev;
    sink_.onEvent(e, plantId);
  }
}

void Station::clear(AlertCode code, int slot) {
  if (alerts_.clear(code, static_cast<int8_t>(slot), clock_.now)) {
    log(EventType::AlertCleared, plantIdForChannel(static_cast<int8_t>(slot)), alertCodeName(code), 0, "");
  }
}

void Station::log(EventType type, const std::string& plantId, const std::string& code, float ml,
                  const std::string& detail) {
  Event e;
  e.ts = clock_.valid ? clock_.now : 0;
  e.type = type;
  e.code = code;
  e.ml = ml;
  e.detail = detail;
  sink_.onEvent(e, plantId);
}

EpochS Station::epochAt(int64_t ms, int64_t nowMs) const {
  if (!clock_.valid || ms < 0) return 0;
  return clock_.now - (nowMs - ms) / 1000;
}

// ------------------------------------------------------------------ status

void Station::statusToJson(const SystemInfo& sys, int64_t nowMs, JsonObject o) const {
  JsonObject dev = o["device"].to<JsonObject>();
  dev["name"] = settings_.deviceName;
  dev["version"] = sys.version;
  dev["uptimeS"] = sys.uptimeS;
  dev["timeValid"] = clock_.valid;
  if (clock_.valid) {
    dev["now"] = clock_.now;
  } else {
    dev["now"] = nullptr;
  }
  dev["tzName"] = settings_.tzName;
  dev["heapFree"] = sys.heapFree;
  dev["resetReason"] = sys.resetReason;

  JsonObject net = o["network"].to<JsonObject>();
  net["mode"] = sys.wifiMode;
  net["ssid"] = sys.ssid;
  net["ip"] = sys.ip;
  net["rssi"] = sys.rssi;
  net["apActive"] = sys.apActive;
  net["apSsid"] = sys.apSsid;
  net["internet"] = sys.internet;

  JsonObject sto = o["storage"].to<JsonObject>();
  sto["sdOk"] = sys.sdOk;
  sto["sdUsedMb"] = r1(sys.sdUsedMb);
  sto["sdTotalMb"] = r1(sys.sdTotalMb);

  JsonObject aut = o["automation"].to<JsonObject>();
  aut["enabled"] = settings_.automationEnabled;
  aut["emergencyStop"] = stopped_;

  JsonObject env = o["environment"].to<JsonObject>();
  env["valid"] = env_.valid;
  if (env_.valid) {
    env["tempC"] = r1(env_.tempC);
    env["humidityPct"] = r1(env_.humidityPct);
    env["vpdKPa"] = r2(vpdKPa(env_.tempC, env_.humidityPct));
  } else {
    env["tempC"] = nullptr;
    env["humidityPct"] = nullptr;
    env["vpdKPa"] = nullptr;
  }
  if (env_.luxValid) {
    env["lux"] = r1(env_.lux);
  } else {
    env["lux"] = nullptr;
  }

  int online = 0, total = 0;
  float mlToday = 0;
  const uint16_t checkInS = static_cast<uint16_t>(settings_.checkInMin * 60);
  JsonArray plants = o["plants"].to<JsonArray>();
  for (const auto& p : plants_) {
    const NodeView* n = p.node.empty() ? nullptr : fleet_.find(p.node);
    const bool rep = n && n->haveReport;
    const ReportMsg& r = rep ? n->report : ReportMsg{};
    const int slot = slotOf(p.id);
    if (n) {
      total++;
      online += n->online ? 1 : 0;
    }
    if (rep) mlToday += r.mlToday;

    JsonObject j = plants.add<JsonObject>();
    j["id"] = p.id;
    j["name"] = p.name;
    if (!p.node.empty()) {
      j["node"] = p.node;
    } else {
      j["node"] = nullptr;
    }
    j["online"] = n && n->online;
    const EpochS seen = n ? epochAt(n->lastSeenMs, nowMs) : 0;
    if (seen > 0) {
      j["lastSeenAt"] = seen;
    } else {
      j["lastSeenAt"] = nullptr;
    }
    const int64_t eta = n ? fleet_.etaS(p.node, nowMs, checkInS) : -1;
    if (eta >= 0) {
      j["nextCheckInS"] = eta;
    } else {
      j["nextCheckInS"] = nullptr;
    }
    if (n && !n->fw.empty()) {
      j["firmware"] = n->fw;
    } else {
      j["firmware"] = nullptr;
    }
    if (rep && r.batteryMv > 0) {
      JsonObject b = j["battery"].to<JsonObject>();
      b["mv"] = r.batteryMv;
      b["pct"] = std::lround(batteryPct(r.batteryMv));
      b["low"] = r.batteryLow;
    } else {
      j["battery"] = nullptr;
    }
    if (rep && r.linkPct <= 100) {
      j["linkPct"] = r.linkPct;
    } else {
      j["linkPct"] = nullptr;
    }

    if (rep && r.haveMoisture) {
      j["moisturePct"] = r1(r.moisturePct);
      j["raw"] = r.raw;
    } else {
      j["moisturePct"] = nullptr;
      j["raw"] = nullptr;
    }
    if (rep && r.hasPlant) {
      j["sensor"] = sensorHealthName(static_cast<SensorHealth>(r.health));
      j["issue"] = sensorIssueName(static_cast<SensorIssue>(r.issue));
      j["state"] = plannerName(r.planner);
      j["triggerPct"] = r1(r.triggerPct);
      j["targetPct"] = r1(r.targetPct);
    } else {
      j["sensor"] = "unknown";
      j["issue"] = "unknown";
      j["state"] = "unknown";
      j["triggerPct"] = r1(p.moisture.min);
      j["targetPct"] = r1(sessionTargetPct(p));
    }
    j["mlToday"] = rep ? r1(r.mlToday) : 0.0;
    if (rep && r.lastWateredAt > 0) {
      j["lastWateredAt"] = r.lastWateredAt;
    } else {
      j["lastWateredAt"] = nullptr;
    }
    j["lockedOut"] = rep && r.lockedOut;
    if (rep && r.mlPerPct > 0) {
      j["mlPerPct"] = r2(r.mlPerPct);
    } else {
      j["mlPerPct"] = nullptr;
    }
    j["soakRemainingS"] = rep ? r.soakRemainingS : 0;

    if (rep) {
      JsonObject b = j["bottle"].to<JsonObject>();
      const float cap = r.bottleCapacityMl > 0 ? r.bottleCapacityMl : p.bottle.capacityMl;
      b["ml"] = std::lround(r.bottleMl);
      b["pct"] = r1(cap > 0 ? std::clamp(r.bottleMl / cap * 100.0f, 0.0f, 100.0f) : 0.0f);
      b["capacityMl"] = std::lround(cap);
      if (r.daysLeft >= 0) {
        b["daysLeft"] = r1(r.daysLeft);
      } else {
        b["daysLeft"] = nullptr;
      }
      b["low"] = r.bottleLow;
      b["empty"] = r.bottleEmpty;
      b["emptyReason"] = emptyReasonName(static_cast<EmptyReason>(r.emptyReason));
      b["flowFactor"] = std::round(r.flowFactor * 100.0) / 100.0;
    } else {
      j["bottle"] = nullptr;
    }
    JsonObject pump = j["pump"].to<JsonObject>();
    pump["learned"] = rep && r.pumpReady;
    pump["lastRun"] = pumpVerdictName(static_cast<PumpVerdict>(rep ? r.pumpVerdict : 0));
    if (rep && r.doseActive) {
      JsonObject d = j["dosing"].to<JsonObject>();
      d["ml"] = r1(r.doseMl);
      d["dispensedMl"] = r1(r.doseDispensedMl);
    } else {
      j["dosing"] = nullptr;
    }
    JsonArray q = j["queued"].to<JsonArray>();
    if (n) {
      for (const auto& c : n->queue) {
        JsonObject x = q.add<JsonObject>();
        x["id"] = c.id;
        x["kind"] = commandKindName(c.kind);
        x["arg"] = r1(c.arg);
      }
    }
    j["configPending"] = n && n->configPending();
    j["repairRequested"] = n && n->wantsRepair;
    j["buttonPressed"] = slot >= 0 && buttonMs_[slot] > 0 && nowMs - buttonMs_[slot] < kButtonHighlightMs;
  }

  JsonArray waiting = o["newNodes"].to<JsonArray>();
  for (const auto& n : fleet_.nodes()) {
    if (n.adopted) continue;
    JsonObject w = waiting.add<JsonObject>();
    w["id"] = n.id;
    w["board"] = boardName(n.board);
    w["firmware"] = n.fw;
    const EpochS seen = epochAt(n.lastSeenMs, nowMs);
    if (seen > 0) {
      w["lastSeenAt"] = seen;
    } else {
      w["lastSeenAt"] = nullptr;
    }
  }

  JsonObject radio = o["radio"].to<JsonObject>();
  radio["channel"] = channel_;
  radio["checkInMin"] = settings_.checkInMin;
  radio["online"] = online;
  radio["total"] = total;
  o["mlTodayTotal"] = r1(mlToday);

  JsonObject al = o["alerts"].to<JsonObject>();
  al["critical"] = alerts_.countActive(Severity::Critical);
  al["warning"] = alerts_.countActive(Severity::Warning);
  al["info"] = alerts_.countActive(Severity::Info);
}

}  // namespace plantlink
