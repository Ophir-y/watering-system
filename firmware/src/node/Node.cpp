#include "Node.h"

#include <ArduinoJson.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <esp_timer.h>

#include <algorithm>
#include <ctime>

#include "Json.h"
#include "boards.h"

using namespace plantcore;
using namespace plantlink;

namespace {
constexpr int64_t kSoilEveryMs = 50000;         // at most one soil reading per wake, at least 50 s apart
constexpr int64_t kEnvValidMs = 30 * 60000;     // room climate from the station goes stale after this
constexpr uint32_t kReplyTimeoutMs = 60;
constexpr int64_t kUnpairedFastMs = 15 * 60000; // after boot or unpairing: say Hello often...
constexpr uint32_t kUnpairedEveryMs = 20000;    // ...every 20 s, so adding the plant feels instant
constexpr int kScanAfterMissed = 3;
}  // namespace

namespace {
plantlink::Mac selfMac() {
  plantlink::Mac m{};
  esp_read_mac(m.data(), ESP_MAC_WIFI_STA);  // from eFuse: valid before the radio starts
  return m;
}
}  // namespace

Node::Node() : session_(*this, selfMac(), board::kId, PLANTCARE_VERSION), engine_(hw_, *this) {}

int64_t Node::nowMs() const { return esp_timer_get_time() / 1000; }

void Node::begin() {
  hw_.begin();
  store_.begin();
  radio_.begin(board::kLowTxPower);

  Pairing p;
  if (store_.loadPairing(p)) session_.restore(p);

  std::string json;
  if (store_.loadConfig(json)) {
    JsonDocument doc;
    std::string err;
    havePlant_ = !deserializeJson(doc, json) && nodeConfigFromJson(doc.as<JsonObjectConst>(), settings_, plant_, err);
  }
  applyConfig();

  PersistedState st;
  if (!store_.loadState(st)) st = PersistedState{};
  engine_.restore(st, inputs(nowMs(), false));

  const esp_reset_reason_t why = esp_reset_reason();
  if (why == ESP_RST_BROWNOUT) {
    engine_.setSystemAlert(AlertCode::UnexpectedReset, Severity::Warning, true,
                           "The node restarted after a brown-out: the batteries are weak or the pump draws too much.");
  } else if (why == ESP_RST_PANIC || why == ESP_RST_TASK_WDT || why == ESP_RST_INT_WDT || why == ESP_RST_WDT) {
    engine_.setSystemAlert(AlertCode::UnexpectedReset, Severity::Warning, true,
                           "The node restarted after a crash or watchdog timeout.");
  }
  unpairedSinceMs_ = nowMs();
  hw_.flash(session_.paired() ? 1 : 2);
}

void Node::applyConfig() {
  engine_.configure(settings_, havePlant_ ? &plant_ : nullptr);
  hw_.setHardLimitMs(settings_.limits.hardMaxOnTimeMs);
}

// ------------------------------------------------------------------ the loop

uint32_t Node::step() {
  const int64_t now = nowMs();
  handleButton(hw_.pollButton(), now);

  const bool pumping = engine_.doseActive() || hw_.pumpOn();
  const bool sample = !pumping && now - lastSoilMs_ >= kSoilEveryMs;
  engine_.tick(inputs(now, sample));
  hw_.tick();

  if (!engine_.doseActive() && (checkInNow_ || now >= nextCheckInMs_)) checkIn(now);

  if (engine_.doseActive() || hw_.pumpOn()) return 100;  // stay awake while pumping
  if (hw_.blinking()) return 250;
  int64_t next = nextCheckInMs_;
  const int64_t want = engine_.wantTickBy(now);
  if (want >= 0) next = std::min(next, want);
  return static_cast<uint32_t>(std::clamp<int64_t>(next - now, 20, 3600000));
}

void Node::handleButton(NodeHw::Press p, int64_t now) {
  if (p == NodeHw::Press::Short) {
    // "I refilled the bottle" (and: show the dashboard which plant this is).
    engine_.refilled(0);
    session_.pressButton();
    checkInNow_ = true;
    hw_.flash(1);
  } else if (p == NodeHw::Press::Long) {
    // Forget the station and pair again.
    Pairing fresh;
    fresh.seq = session_.pairing().seq;  // never reuse a sequence number
    session_.restore(fresh);
    store_.savePairing(fresh);
    onUnpaired();
    checkInNow_ = true;
  }
  (void)now;
}

Inputs Node::inputs(int64_t now, bool sampleSoil) {
  Inputs in;
  in.monoMs = now;
  if (epochAt_ != 0) {
    const int64_t epoch = int64_t(epochAt_) + (now - epochAtMs_) / 1000;
    const time_t local = static_cast<time_t>(epoch + utcOffsetS_);
    struct tm tm;
    gmtime_r(&local, &tm);
    in.timeValid = true;
    in.now = epoch;
    in.localHour = tm.tm_hour;
    in.localMonth = tm.tm_mon + 1;
    in.localDay = static_cast<int32_t>(local / 86400);
  }
  if (envAtMs_ >= 0 && now - envAtMs_ < kEnvValidMs) in.env = env_;

  if (sampleSoil && havePlant_) {
    int32_t raw = 0;
    in.moisture.ok = hw_.readSoil(raw);
    in.moisture.raw = raw;
    in.moisture.fresh = true;
    lastSoilMs_ = now;
  }
  if (sampleSoil || hw_.pumpOn()) lastBatteryMv_ = hw_.batteryMv();
  in.batteryMv = lastBatteryMv_;
  if (hw_.pumpOn()) {
    in.pumpSense = true;
    in.pumpMa = hw_.pumpMa();
  }
  return in;
}

// ------------------------------------------------------------------ radio

uint32_t Node::checkInIntervalMs(int64_t now) const {
  if (!session_.paired() && now - unpairedSinceMs_ < kUnpairedFastMs) return kUnpairedEveryMs;
  const uint32_t s = havePlant_ ? settings_.checkInMin * 60u : session_.checkInS();
  return std::max<uint32_t>(s, 60) * 1000;
}

bool Node::converse() {
  OutFrame f;
  session_.start(f);
  for (int guard = 0; guard < 64; ++guard) {
    radio_.send(f);
    Step s = Step::Wait;
    const uint32_t t0 = millis();
    RadioFrame rx;
    while (s == Step::Wait && millis() - t0 < kReplyTimeoutMs) {
      if (radio_.receive(rx, kReplyTimeoutMs - (millis() - t0))) s = session_.onReceive(rx.from, rx.data, rx.len, f);
    }
    if (s == Step::Wait) s = session_.onTimeout(f);
    if (s == Step::Done) break;
  }
  return session_.missedInARow() == 0;
}

void Node::checkIn(int64_t now) {
  checkInNow_ = false;
  radio_.up(session_.channel());
  bool ok = false;
  if (session_.haveHub() && session_.missedInARow() < kScanAfterMissed) {
    ok = converse();
  }
  if (!ok) {
    // Find the station: it sits on the home router's channel, which can change.
    for (uint8_t ch = 1; ch <= 13 && !ok; ++ch) {
      radio_.setChannel(ch);
      if (converse()) {
        session_.setChannel(ch);
        ok = true;
      }
    }
  }
  radio_.down();
  nextCheckInMs_ = nowMs() + checkInIntervalMs(now);
}

void Node::fillReport(ReportMsg& r) {
  const NodeStatus s = engine_.status();
  r.uptimeS = static_cast<uint32_t>(nowMs() / 1000);
  r.batteryMv = static_cast<uint16_t>(std::clamp(s.batteryMv, 0.0f, 65535.0f));
  r.hasPlant = s.hasPlant;
  r.haveMoisture = s.haveMoisture;
  r.lockedOut = s.lockedOut;
  r.doseActive = s.doseActive;
  r.stopped = s.stopped;
  r.pumpReady = s.pumpReady;
  r.bottleEmpty = s.bottle.empty;
  r.bottleLow = s.bottle.low;
  r.batteryLow = s.batteryLow;
  r.timeValid = epochAt_ != 0;
  r.moisturePct = s.moisturePct;
  r.raw = s.raw;
  r.health = static_cast<uint8_t>(s.health);
  r.issue = static_cast<uint8_t>(s.issue);
  r.planner = static_cast<uint8_t>(s.planner);
  r.triggerPct = s.triggerPct;
  r.targetPct = s.targetPct;
  r.mlToday = s.mlToday;
  r.lastWateredAt = static_cast<uint32_t>(std::max<int64_t>(0, s.lastWateredAt));
  r.mlPerPct = s.mlPerPct;
  r.soakRemainingS = static_cast<uint16_t>(std::min<int64_t>(s.soakRemainingMs / 1000, 65535));
  r.bottleMl = s.bottle.ml;
  r.bottleCapacityMl = s.bottle.capacityMl;
  r.daysLeft = s.bottle.daysLeft;
  r.emptyReason = static_cast<uint8_t>(s.bottle.reason);
  r.flowFactor = s.bottle.flowFactor;
  r.pumpVerdict = static_cast<uint8_t>(s.pumpVerdict);
  r.doseMl = s.doseMl;
  r.doseDispensedMl = s.doseDispensedMl;
  r.lastDoseMl = s.lastDose.valid ? s.lastDose.ml : 0;
  r.lastDoseOnMs = s.lastDose.valid ? s.lastDose.onTimeMs : 0;
  r.lastDoseAt = s.lastDose.valid ? static_cast<uint32_t>(std::max<int64_t>(0, s.lastDose.at)) : 0;
  r.activeAlerts = s.activeAlerts;
}

void Node::onStationState(const ReplyMsg& r) {
  if (r.epoch) {
    epochAt_ = r.epoch;
    epochAtMs_ = nowMs();
    utcOffsetS_ = r.utcOffsetS;
  }
  env_ = r.env;
  envAtMs_ = r.env.valid || r.env.luxValid ? nowMs() : -1;
  engine_.setStopped(r.stop, "stopped from the dashboard");
  engine_.setAutomation(r.automation);
}

void Node::onCommand(const Command& c) {
  switch (c.kind) {
    case CommandKind::Water: engine_.manualWater(c.arg); break;
    case CommandKind::TimedRun: engine_.timedRun(c.arg); break;
    case CommandKind::Refilled: engine_.refilled(c.arg); break;
    case CommandKind::ClearFaults: engine_.clearFaults(); break;
    case CommandKind::Identify: hw_.blink(10000); break;
    case CommandKind::Forget:
    case CommandKind::None: break;
  }
}

bool Node::onConfig(const std::string& json) {
  JsonDocument doc;
  std::string err;
  Settings s = settings_;
  PlantProfile p = plant_;
  if (deserializeJson(doc, json) || !nodeConfigFromJson(doc.as<JsonObjectConst>(), s, p, err)) return false;
  settings_ = s;
  plant_ = p;
  havePlant_ = true;
  store_.saveConfig(json);
  applyConfig();
  return true;
}

void Node::onPairingChanged() { store_.savePairing(session_.pairing()); }

void Node::onUnpaired() {
  havePlant_ = false;
  store_.clearConfig();
  applyConfig();
  unpairedSinceMs_ = nowMs();
  hw_.flash(3);
}

void Node::onEvent(const Event& e) { session_.queueEvent(e); }

void Node::onPersist(const PersistedState& s) { store_.saveState(s); }
