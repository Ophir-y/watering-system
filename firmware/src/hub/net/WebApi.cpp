#include "WebApi.h"

#include <ArduinoJson.h>
#include <Update.h>
#include <esp_system.h>
#include <esp_timer.h>

#include <algorithm>
#include <memory>
#include <vector>

#include "../app/Hub.h"
#include "../hal/TimeKeeper.h"
#include "../storage/LogStore.h"
#include "Auth.h"
#include "Json.h"
#include "Notifier.h"
#include "Station.h"
#include "WebAssets.h"
#include "WifiService.h"
#include "config.h"

using namespace plantcore;
using plantlink::CommandResult;
using plantlink::Station;

namespace {

// Collects the request body into a malloc'd buffer (the server frees _tempObject).
class ApiHandler : public AsyncWebHandler {
 public:
  explicit ApiHandler(WebApi& api) : api_(api) {}

  bool canHandle(AsyncWebServerRequest* r) const override {
    const String& u = r->url();
    return u.startsWith("/api/") && u != "/api/stream" && u != "/api/system/ota";
  }

  void handleBody(AsyncWebServerRequest* r, uint8_t* data, size_t len, size_t index, size_t total) override {
    if (total > config::kMaxBodyBytes) return;
    if (index == 0) {
      free(r->_tempObject);
      r->_tempObject = malloc(total + 1);
    }
    char* buf = static_cast<char*>(r->_tempObject);
    if (!buf || index + len > total) return;
    memcpy(buf + index, data, len);
    if (index + len == total) buf[total] = '\0';
  }

  void handleRequest(AsyncWebServerRequest* r) override { api_.dispatch(r); }
  bool isRequestHandlerTrivial() const override { return false; }

 private:
  WebApi& api_;
};

void sendDoc(AsyncWebServerRequest* r, int code, const JsonDocument& doc) {
  AsyncResponseStream* res = r->beginResponseStream("application/json");
  res->setCode(code);
  res->addHeader("Cache-Control", "no-store");
  serializeJson(doc, *res);
  r->send(res);
}

void sendError(AsyncWebServerRequest* r, int code, const std::string& error, const std::string& message = "") {
  JsonDocument doc;
  doc["error"] = error;
  doc["message"] = message.empty() ? error : message;
  sendDoc(r, code, doc);
}

void sendOk(AsyncWebServerRequest* r) {
  JsonDocument doc;
  doc["ok"] = true;
  sendDoc(r, 200, doc);
}

// "" = ok, otherwise a short code.
void sendResult(AsyncWebServerRequest* r, const std::string& err) {
  if (err.empty()) return sendOk(r);
  sendError(r, err == "not_found" ? 404 : 409, err);
}

// A command for a node: queued now, carried out at its next check-in.
void sendCommand(AsyncWebServerRequest* r, const CommandResult& c) {
  if (!c.error.empty()) {
    const int code = c.error == "not_found" ? 404 : c.error == "invalid_amount" ? 400 : 409;
    const char* why = c.error == "no_node"          ? "This plant has no node."
                      : c.error == "busy"           ? "Several commands are already waiting for this plant's node."
                      : c.error == "emergency_stop" ? "Watering is stopped. Resume first."
                                                    : "";
    return sendError(r, code, c.error, why);
  }
  JsonDocument doc;
  doc["queued"] = true;
  doc["id"] = c.id;
  if (c.etaS >= 0) {
    doc["etaS"] = c.etaS;
  } else {
    doc["etaS"] = nullptr;
  }
  sendDoc(r, 202, doc);
}

bool parseBody(AsyncWebServerRequest* r, JsonDocument& doc) {
  const char* body = static_cast<const char*>(r->_tempObject);
  if (!body || r->contentLength() == 0) {
    doc.to<JsonObject>();
    return true;
  }
  DeserializationError e = deserializeJson(doc, body, r->contentLength());
  if (e || !doc.is<JsonObject>()) {
    sendError(r, 400, "bad_json", e ? e.c_str() : "expected a JSON object");
    return false;
  }
  return true;
}

std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start <= s.size()) {
    size_t end = s.find(sep, start);
    if (end == std::string::npos) end = s.size();
    if (end > start) out.push_back(s.substr(start, end - start));
    start = end + 1;
  }
  return out;
}

int64_t argInt(AsyncWebServerRequest* r, const char* name, int64_t fallback) {
  return r->hasArg(name) ? atoll(r->arg(name).c_str()) : fallback;
}

std::string argStr(AsyncWebServerRequest* r, const char* name) {
  return r->hasArg(name) ? std::string(r->arg(name).c_str()) : std::string();
}

const char* resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "power_on";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt_watchdog";
    case ESP_RST_TASK_WDT: return "task_watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep_sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    default: return "unknown";
  }
}

const PlantProfile* findPlant(const std::vector<PlantProfile>& plants, const std::string& id) {
  for (const auto& p : plants) {
    if (p.id == id) return &p;
  }
  return nullptr;
}

}  // namespace

// ------------------------------------------------------------------ setup

void WebApi::begin() {
  events_.setFilter([this](AsyncWebServerRequest* r) { return authorized(r); });
  events_.onConnect([](AsyncEventSourceClient* c) { c->send("{}", "hello", millis(), 5000); });
  server_.addHandler(&events_);

  server_.on(
      "/api/system/ota", HTTP_POST,
      [this](AsyncWebServerRequest* r) {
        if (!otaAuthorized_) return sendError(r, 401, "unauthorized", "Log in first.");
        if (!otaError_.empty()) return sendError(r, 500, "update_failed", otaError_);
        JsonDocument doc;
        doc["ok"] = true;
        doc["message"] = "Update installed. Restarting.";
        sendDoc(r, 200, doc);
        scheduleReboot(1500);
      },
      [this](AsyncWebServerRequest* r, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
        handleOtaUpload(r, filename, index, data, len, final);
      });

  server_.addHandler(new ApiHandler(*this));
  server_.onNotFound([this](AsyncWebServerRequest* r) { serveStatic(r); });
  server_.begin();
}

bool WebApi::authorized(AsyncWebServerRequest* r) {
  std::string token;
  if (const AsyncWebHeader* h = r->getHeader("Authorization")) {
    String v = h->value();
    if (v.startsWith("Bearer ")) token = v.substring(7).c_str();
  }
  if (token.empty()) token = argStr(r, "token");  // EventSource cannot send headers
  return auth_.check(token);
}

plantlink::SystemInfo WebApi::systemInfo() {
  plantlink::SystemInfo s;
  s.version = PLANTCARE_VERSION;
  s.uptimeS = esp_timer_get_time() / 1000000;
  s.heapFree = ESP.getFreeHeap();
  s.resetReason = resetReasonName(esp_reset_reason());
  s.wifiMode = wifi_.mode();
  s.ssid = wifi_.ssid();
  s.ip = wifi_.ip();
  s.rssi = wifi_.rssi();
  s.apActive = wifi_.apActive();
  s.apSsid = wifi_.apSsid();
  s.internet = wifi_.internet();
  s.sdOk = logs_.sdOk();
  s.sdUsedMb = logs_.usedMb();
  s.sdTotalMb = logs_.totalMb();
  return s;
}

void WebApi::pushStatus() {
  if (events_.count() == 0) return;
  JsonDocument doc;
  if (!hub_.status(doc.to<JsonObject>(), systemInfo())) return;
  std::string json;
  serializeJson(doc, json);
  events_.send(json.c_str(), "status", millis());
}

void WebApi::pushEvent(const char* type, const std::string& json) {
  if (events_.count() > 0) events_.send(json.c_str(), type, millis());
}

// ------------------------------------------------------------------ static files

void WebApi::serveStatic(AsyncWebServerRequest* r) {
  if (r->url().startsWith("/api/")) return sendError(r, 404, "not_found", "Unknown API endpoint");
  if (r->method() != HTTP_GET && r->method() != HTTP_HEAD) return r->send(405);
  String path = r->url();
  if (path == "/") path = "/index.html";
  const WebAsset* a = findWebAsset(path.c_str());
  // Unknown paths get the app: client-side routes, and captive-portal probes from phones.
  if (!a) a = findWebAsset("/index.html");
  if (!a) return r->send(404);
  if (const AsyncWebHeader* h = r->getHeader("If-None-Match")) {
    if (h->value() == a->etag) return r->send(304);
  }
  AsyncWebServerResponse* res = r->beginResponse(200, a->contentType, a->data, a->length);
  if (a->gzip) res->addHeader("Content-Encoding", "gzip");
  res->addHeader("ETag", a->etag);
  res->addHeader("Cache-Control", a->immutable ? "public, max-age=31536000, immutable" : "no-cache");
  r->send(res);
}

// ------------------------------------------------------------------ OTA

void WebApi::handleOtaUpload(AsyncWebServerRequest* r, const String&, size_t index, uint8_t* data, size_t len,
                             bool final) {
  // Only the main station is updated here; the nodes keep watering meanwhile.
  if (index == 0) {
    otaAuthorized_ = authorized(r);
    otaError_.clear();
    if (!otaAuthorized_) return;
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) otaError_ = Update.errorString();
  }
  if (!otaAuthorized_ || !otaError_.empty()) return;
  if (len && Update.write(data, len) != len) otaError_ = Update.errorString();
  if (final && otaError_.empty() && !Update.end(true)) otaError_ = Update.errorString();
}

// ------------------------------------------------------------------ CSV export

void WebApi::handleExport(AsyncWebServerRequest* r) {
  std::string kind = argStr(r, "kind");
  if (kind.empty()) kind = "readings";
  if (kind != "readings" && kind != "env" && kind != "events") {
    return sendError(r, 400, "bad_kind", "kind must be readings, env or events");
  }
  const int64_t to = argInt(r, "to", clock_.now());
  const int64_t from = argInt(r, "from", to - 7 * 86400);
  auto files = std::make_shared<std::vector<std::string>>(logs_.exportFiles(kind, from, to));
  if (files->empty()) return sendError(r, 404, "no_data", "No logged data in this range, or no SD card.");

  struct State {
    size_t file = 0;
    size_t offset = 0;
  };
  auto st = std::make_shared<State>();
  LogStore* logs = &logs_;
  AsyncWebServerResponse* res =
      r->beginChunkedResponse("text/csv", [files, st, logs](uint8_t* buf, size_t maxLen, size_t) -> size_t {
        while (st->file < files->size()) {
          size_t n = logs->readChunk((*files)[st->file], st->offset, buf, maxLen);
          if (n == 0) {
            st->file++;
            st->offset = 0;
            continue;
          }
          if (st->file > 0 && st->offset == 0) {
            // Keep only the first file's header line.
            size_t skip = 0;
            while (skip < n && buf[skip] != '\n') skip++;
            skip = std::min(n, skip + 1);
            st->offset += skip;
            if (skip >= n) continue;
            memmove(buf, buf + skip, n - skip);
            n -= skip;
          }
          st->offset += n;
          return n;
        }
        return 0;
      });
  const std::string name = "plantcare-" + kind + "-" + clock_.local(from).date + "-to-" + clock_.local(to).date + ".csv";
  res->addHeader("Content-Disposition", ("attachment; filename=\"" + name + "\"").c_str());
  r->send(res);
}

// ------------------------------------------------------------------ REST

void WebApi::dispatch(AsyncWebServerRequest* r) {
  if (r->contentLength() > config::kMaxBodyBytes) return sendError(r, 413, "too_large");

  const std::vector<std::string> seg = split(std::string(r->url().c_str() + 5), '/');
  const WebRequestMethodComposite m = r->method();
  auto is = [&](WebRequestMethodComposite method, std::initializer_list<const char*> parts) {
    if (m != method || seg.size() != parts.size()) return false;
    size_t i = 0;
    for (const char* p : parts) {
      if (strcmp(p, "*") != 0 && seg[i] != p) return false;
      ++i;
    }
    return true;
  };
  JsonDocument body;
  // Runs fn on the locked station core; false (and a 503 sent) if it stayed busy.
  auto station = [&](const std::function<void(Station&)>& fn) {
    if (hub_.with(fn)) return true;
    sendError(r, 503, "busy", "The main station is busy. Try again.");
    return false;
  };

  // ---- public
  if (is(HTTP_GET, {"info"})) {
    JsonDocument doc;
    doc["name"] = hub_.settings().deviceName;
    doc["version"] = PLANTCARE_VERSION;
    doc["setupRequired"] = auth_.setupRequired();
    doc["timeValid"] = clock_.valid();
    doc["apActive"] = wifi_.apActive();
    doc["dashboardEmbedded"] = webAssetsEmbedded();
    return sendDoc(r, 200, doc);
  }
  if (is(HTTP_POST, {"setup"})) {
    if (!parseBody(r, body)) return;
    if (!auth_.setupRequired()) return sendError(r, 409, "already_set_up", "This device already has a password.");
    const std::string password = body["password"] | "";
    if (password.size() < Auth::kMinPasswordLength) {
      return sendError(r, 400, "weak_password", "Use at least 8 characters.");
    }
    JsonObjectConst patch = body["settings"];
    if (!patch.isNull()) {
      Settings s = hub_.settings();
      std::string err;
      if (!fromJson(patch, s, err)) return sendError(r, 400, "invalid", err);
      if (!station([&](Station& st) { err = st.saveSettings(s); })) return;
      if (!err.empty()) return sendError(r, 400, "invalid", err);
    }
    if (!clock_.valid() && body["now"].is<int64_t>()) clock_.setManual(body["now"].as<int64_t>());
    const std::string token = auth_.setup(password);
    if (token.empty()) return sendError(r, 500, "setup_failed");
    JsonDocument doc;
    doc["token"] = token;
    return sendDoc(r, 200, doc);
  }
  if (is(HTTP_POST, {"login"})) {
    if (!parseBody(r, body)) return;
    if (uint32_t wait = auth_.lockedForMs()) {
      JsonDocument doc;
      doc["error"] = "locked";
      doc["message"] = "Too many attempts. Try again shortly.";
      doc["retryAfterS"] = wait / 1000 + 1;
      return sendDoc(r, 429, doc);
    }
    const std::string token = auth_.login(body["password"] | "");
    if (token.empty()) return sendError(r, 401, "wrong_password", "Wrong password.");
    JsonDocument doc;
    doc["token"] = token;
    return sendDoc(r, 200, doc);
  }

  if (!authorized(r)) return sendError(r, 401, "unauthorized", "Log in first.");
  if (m != HTTP_GET && !parseBody(r, body)) return;

  // ---- session
  if (is(HTTP_POST, {"logout"})) {
    if (const AsyncWebHeader* h = r->getHeader("Authorization")) auth_.logout(h->value().substring(7).c_str());
    return sendOk(r);
  }
  if (is(HTTP_POST, {"account", "password"})) {
    std::string token;
    if (!auth_.changePassword(body["current"] | "", body["next"] | "", token)) {
      return sendError(r, 400, "rejected", "Current password wrong, or the new one is shorter than 8 characters.");
    }
    JsonDocument doc;
    doc["token"] = token;
    return sendDoc(r, 200, doc);
  }

  // ---- status
  if (is(HTTP_GET, {"status"})) {
    JsonDocument doc;
    if (!hub_.status(doc.to<JsonObject>(), systemInfo())) return sendError(r, 503, "busy");
    return sendDoc(r, 200, doc);
  }

  // ---- plants
  if (is(HTTP_GET, {"plants"})) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (const auto& p : hub_.plants()) toJson(p, arr.add<JsonObject>());
    return sendDoc(r, 200, doc);
  }
  if (is(HTTP_POST, {"plants"})) {
    // With "node": the id of a node waiting to be added; it pairs at its next check-in.
    PlantProfile p;
    const bool hasId = body["id"].is<const char*>();
    if (!hasId) p.id = "new";  // placeholder so validation passes; a real id is generated
    std::string err;
    if (!fromJson(body.as<JsonObjectConst>(), p, err)) return sendError(r, 400, "invalid", err);
    if (!hasId) p.id.clear();
    if (!station([&](Station& st) { err = st.savePlant(p, ""); })) return;
    if (!err.empty()) return sendError(r, 400, "invalid", err);
    JsonDocument doc;
    toJson(p, doc.to<JsonObject>());
    return sendDoc(r, 201, doc);
  }
  if (seg.size() >= 2 && seg[0] == "plants") {
    const std::string id = seg[1];
    const std::vector<PlantProfile> plants = hub_.plants();
    const PlantProfile* existing = findPlant(plants, id);
    if (!existing) return sendError(r, 404, "not_found", "No plant with id " + id);

    if (is(HTTP_GET, {"plants", "*"})) {
      JsonDocument doc;
      toJson(*existing, doc.to<JsonObject>());
      return sendDoc(r, 200, doc);
    }
    if (is(HTTP_PUT, {"plants", "*"})) {
      PlantProfile p = *existing;
      std::string err;
      body.remove("id");
      if (!fromJson(body.as<JsonObjectConst>(), p, err)) return sendError(r, 400, "invalid", err);
      if (!station([&](Station& st) { err = st.savePlant(p, id); })) return;
      if (!err.empty()) return sendError(r, 400, "invalid", err);
      JsonDocument doc;
      toJson(p, doc.to<JsonObject>());
      return sendDoc(r, 200, doc);
    }
    if (is(HTTP_DELETE, {"plants", "*"})) {
      std::string err;
      if (!station([&](Station& st) { err = st.deletePlant(id); })) return;
      return sendResult(r, err);
    }
    CommandResult c;
    if (is(HTTP_POST, {"plants", "*", "water"})) {
      const float ml = body["ml"] | 0.0f;
      if (!station([&](Station& st) { c = st.water(id, ml); })) return;
      return sendCommand(r, c);
    }
    if (is(HTTP_POST, {"plants", "*", "timed-run"})) {
      const float seconds = body["seconds"] | 0.0f;
      if (!station([&](Station& st) { c = st.timedRun(id, seconds); })) return;
      return sendCommand(r, c);
    }
    if (is(HTTP_POST, {"plants", "*", "refilled"})) {
      const float ml = body["ml"] | 0.0f;  // 0 or absent: filled to capacity
      if (!station([&](Station& st) { c = st.refilled(id, ml); })) return;
      return sendCommand(r, c);
    }
    if (is(HTTP_POST, {"plants", "*", "clear-faults"})) {
      if (!station([&](Station& st) { c = st.clearFaults(id); })) return;
      return sendCommand(r, c);
    }
    if (is(HTTP_POST, {"plants", "*", "identify"})) {
      if (!station([&](Station& st) { c = st.identify(id); })) return;
      return sendCommand(r, c);
    }
    if (is(HTTP_POST, {"plants", "*", "calibrate"})) {
      const std::string point = body["point"] | "";
      int32_t raw = 0;
      int64_t ageS = 0;
      std::string err;
      PlantProfile saved;
      if (!station([&](Station& st) {
            err = st.calibrate(id, point, raw, ageS);
            if (const PlantProfile* p = st.plant(id)) saved = *p;
          })) {
        return;
      }
      if (!err.empty()) return sendError(r, 400, "invalid", err);
      JsonDocument doc;
      doc["raw"] = raw;
      doc["ageS"] = ageS;
      doc["calibration"]["rawDry"] = saved.cal.rawDry;
      doc["calibration"]["rawWet"] = saved.cal.rawWet;
      return sendDoc(r, 200, doc);
    }
    return sendError(r, 404, "not_found", "Unknown API endpoint");
  }

  // ---- nodes
  if (is(HTTP_POST, {"nodes", "*", "repair"})) {
    std::string err;
    if (!station([&](Station& st) { err = st.repairNode(seg[1]); })) return;
    return sendResult(r, err);
  }

  // ---- history and events
  const int64_t now = clock_.now();
  if (is(HTTP_GET, {"history"})) {
    const std::string plant = argStr(r, "plant");
    if (plant.empty()) return sendError(r, 400, "invalid", "plant is required");
    const int64_t to = argInt(r, "to", now);
    const int64_t from = argInt(r, "from", to - 86400);
    if (from >= to || to - from > 400LL * 86400) return sendError(r, 400, "invalid", "bad time range");
    JsonDocument doc;
    logs_.history(plant, from, to, static_cast<size_t>(argInt(r, "points", 200)), doc.to<JsonObject>());
    return sendDoc(r, 200, doc);
  }
  if (is(HTTP_GET, {"env", "history"})) {
    const int64_t to = argInt(r, "to", now);
    const int64_t from = argInt(r, "from", to - 86400);
    if (from >= to || to - from > 400LL * 86400) return sendError(r, 400, "invalid", "bad time range");
    JsonDocument doc;
    logs_.envHistory(from, to, static_cast<size_t>(argInt(r, "points", 200)), doc.to<JsonObject>());
    return sendDoc(r, 200, doc);
  }
  if (is(HTTP_GET, {"events"})) {
    const int64_t to = argInt(r, "to", now);
    const int64_t from = argInt(r, "from", to - 30 * 86400LL);
    JsonDocument doc;
    logs_.events(from, to, argStr(r, "type"), argStr(r, "plant"), static_cast<size_t>(argInt(r, "limit", 100)),
                 doc["events"].to<JsonArray>());
    return sendDoc(r, 200, doc);
  }
  if (is(HTTP_GET, {"export.csv"})) return handleExport(r);

  // ---- alerts
  if (is(HTTP_GET, {"alerts"})) {
    JsonDocument doc;
    JsonArray arr = doc["alerts"].to<JsonArray>();
    if (!station([&](Station& st) {
          std::vector<Alert> alerts = st.alerts().all();
          std::sort(alerts.begin(), alerts.end(), [](const Alert& a, const Alert& b) {
            if (a.active != b.active) return a.active;
            return (a.active ? a.raisedAt : a.clearedAt) > (b.active ? b.raisedAt : b.clearedAt);
          });
          for (const auto& a : alerts) toJson(a, st.plantIdForChannel(a.channel), arr.add<JsonObject>());
        })) {
      return;
    }
    return sendDoc(r, 200, doc);
  }
  if (is(HTTP_POST, {"alerts", "ack-all"})) {
    if (!station([](Station& st) { st.ackAll(); })) return;
    return sendOk(r);
  }
  if (is(HTTP_POST, {"alerts", "*", "ack"})) {
    bool found = false;
    const uint32_t id = static_cast<uint32_t>(atol(seg[1].c_str()));
    if (!station([&](Station& st) { found = st.ackAlert(id); })) return;
    return found ? sendOk(r) : sendError(r, 404, "not_found");
  }

  // ---- settings
  if (is(HTTP_GET, {"settings"})) {
    JsonDocument doc;
    toJson(hub_.settings(), doc.to<JsonObject>());
    return sendDoc(r, 200, doc);
  }
  if (is(HTTP_PUT, {"settings"})) {
    Settings s = hub_.settings();
    std::string err;
    if (!fromJson(body.as<JsonObjectConst>(), s, err)) return sendError(r, 400, "invalid", err);
    if (!station([&](Station& st) { err = st.saveSettings(s); })) return;
    if (!err.empty()) return sendError(r, 400, "invalid", err);
    JsonDocument doc;
    toJson(s, doc.to<JsonObject>());
    return sendDoc(r, 200, doc);
  }

  // ---- system
  if (is(HTTP_POST, {"system", "stop"})) {
    const std::string reason = body["reason"] | "stopped from the dashboard";
    if (!station([&](Station& st) { st.emergencyStop(reason); })) return;
    return sendOk(r);
  }
  if (is(HTTP_POST, {"system", "resume"})) {
    if (!station([](Station& st) { st.resume(); })) return;
    return sendOk(r);
  }
  if (is(HTTP_POST, {"system", "automation"})) {
    if (!body["enabled"].is<bool>()) return sendError(r, 400, "invalid", "enabled must be true or false");
    const bool enabled = body["enabled"];
    std::string err;
    if (!station([&](Station& st) { err = st.setAutomation(enabled); })) return;
    return sendResult(r, err);
  }
  if (is(HTTP_POST, {"system", "reboot"})) {
    sendOk(r);
    return scheduleReboot(1000);
  }
  if (is(HTTP_POST, {"system", "time"})) {
    const int64_t epoch = body["epoch"] | static_cast<int64_t>(0);
    if (epoch < 1735689600) return sendError(r, 400, "invalid", "epoch must be a current unix time in seconds");
    clock_.setManual(epoch);
    return sendOk(r);
  }

  // ---- configuration backup
  if (is(HTTP_GET, {"config", "backup"})) {
    JsonDocument doc;
    doc["version"] = PLANTCARE_VERSION;
    doc["exportedAt"] = now;
    toJson(hub_.settings(), doc["settings"].to<JsonObject>());
    JsonArray arr = doc["plants"].to<JsonArray>();
    for (const auto& p : hub_.plants()) toJson(p, arr.add<JsonObject>());
    return sendDoc(r, 200, doc);
  }
  if (is(HTTP_POST, {"config", "restore"})) {
    Settings s = hub_.settings();
    std::string err;
    if (!fromJson(body["settings"].as<JsonObjectConst>(), s, err)) return sendError(r, 400, "invalid", "settings: " + err);
    std::vector<PlantProfile> plants;
    for (JsonObjectConst o : body["plants"].as<JsonArrayConst>()) {
      PlantProfile p;
      if (!fromJson(o, p, err)) return sendError(r, 400, "invalid", "plant: " + err);
      plants.push_back(p);
    }
    if (!station([&](Station& st) { err = st.restoreConfig(s, plants); })) return;
    return err.empty() ? sendOk(r) : sendError(r, 400, "invalid", err);
  }

  // ---- Wi-Fi
  if (is(HTTP_GET, {"wifi"})) {
    JsonDocument doc;
    doc["configured"] = wifi_.configured();
    doc["ssid"] = wifi_.ssid();
    doc["mode"] = wifi_.mode();
    doc["ip"] = wifi_.ip();
    doc["rssi"] = wifi_.rssi();
    doc["apActive"] = wifi_.apActive();
    doc["apSsid"] = wifi_.apSsid();
    doc["internet"] = wifi_.internet();
    return sendDoc(r, 200, doc);
  }
  if (is(HTTP_POST, {"wifi", "scan"})) {
    wifi_.requestScan();
    return sendOk(r);
  }
  if (is(HTTP_GET, {"wifi", "scan"})) {
    JsonDocument doc;
    doc["scanning"] = !wifi_.scanResults(doc["networks"].to<JsonArray>());
    return sendDoc(r, 200, doc);
  }
  if (is(HTTP_POST, {"wifi"})) {
    const std::string ssid = body["ssid"] | "";
    const std::string password = body["password"] | "";
    const std::string apPassword = body["apPassword"] | "";
    if (ssid.empty() || ssid.size() > 32) return sendError(r, 400, "invalid", "ssid must be 1-32 characters");
    if (password.size() > 63) return sendError(r, 400, "invalid", "password is too long");
    if (!apPassword.empty() && apPassword.size() < 8) {
      return sendError(r, 400, "invalid", "the access point password needs at least 8 characters");
    }
    wifi_.setCredentials(ssid, password, apPassword);
    return sendOk(r);
  }

  // ---- notifications
  if (is(HTTP_POST, {"notify", "test"})) {
    notifier_.requestTest();
    return sendOk(r);
  }
  if (is(HTTP_GET, {"notify", "test"})) {
    JsonDocument doc;
    doc["result"] = notifier_.lastTestResult();
    doc["at"] = notifier_.lastTestAt();
    return sendDoc(r, 200, doc);
  }

  sendError(r, 404, "not_found", "Unknown API endpoint");
}
