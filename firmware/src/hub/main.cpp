// PlantCare main station: the dashboard, history, alerts and the radio link to the
// plant nodes. It drives no pumps, so a crash here never floods anything: every
// node keeps watering on its own until the station is back.
//
//   radio   (core 1, prio 5)  answers node frames at once (a node waits ~60 ms)
//   storage (core 0, prio 2)  SD card writes
//   net     (core 0, prio 1)  Wi-Fi, NTP, notifications, live status, button/LED, OTA health
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_task_wdt.h>

#include "app/Hub.h"
#include "config.h"
#include "Json.h"
#include "hal/I2cDevices.h"
#include "hal/Panel.h"
#include "hal/Sensors.h"
#include "hal/TimeKeeper.h"
#include "net/Auth.h"
#include "net/NodeLink.h"
#include "net/Notifier.h"
#include "net/WebApi.h"
#include "net/WifiService.h"
#include "pins.h"
#include "storage/ConfigStore.h"
#include "storage/LogStore.h"

using namespace plantcore;

namespace {

EnvSensors environment;
TimeKeeper timeKeeper;
ConfigStore configStore;
LogStore logStore;
PanelButton button;
StatusLed led;
Hub hub(configStore, logStore, timeKeeper, environment);
Auth auth;
WifiService wifi;
Notifier notifier;
NodeLink nodeLink;
WebApi web(hub, auth, wifi, logStore, timeKeeper, notifier);

void systemAlert(AlertCode code, Severity sev, bool active, const char* message) {
  hub.with([&](plantlink::Station& st) { st.setSystemAlert(code, sev, active, message); }, 500);
}

void reportResetReason() {
  const char* what = nullptr;
  switch (esp_reset_reason()) {
    case ESP_RST_PANIC: what = "The main station restarted after a crash."; break;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: what = "The main station restarted after a watchdog timeout."; break;
    case ESP_RST_BROWNOUT: what = "The main station restarted after a brown-out: check its USB power supply."; break;
    default: break;
  }
  if (what) systemAlert(AlertCode::UnexpectedReset, Severity::Warning, true, what);
}

void updateSystemAlerts() {
  systemAlert(AlertCode::TimeNotSet, Severity::Warning, !timeKeeper.valid(),
              "The clock is not set (no internet and no RTC time). Open the dashboard to set it from your "
              "browser; watering windows and daily limits need it.");
  systemAlert(AlertCode::StorageFault, Severity::Info, !logStore.sdOk(),
              "No SD card: history is kept in memory for the last day only.");
  systemAlert(AlertCode::EnvSensorFault, Severity::Info, !environment.climatePresent(),
              "No room temperature/humidity sensor: the nodes water without climate adjustments.");
}

void radioTask(void*) { nodeLink.task(); }
void storageTask(void*) { logStore.task(); }

void nightlyBackup(int32_t& lastBackupDay) {
  if (!timeKeeper.valid() || !logStore.sdOk()) return;
  const LocalTime lt = timeKeeper.local(timeKeeper.now());
  if (lt.hour != 3 || lt.day == lastBackupDay) return;
  lastBackupDay = lt.day;
  JsonDocument doc;
  doc["version"] = PLANTCARE_VERSION;
  toJson(hub.settings(), doc["settings"].to<JsonObject>());
  JsonArray arr = doc["plants"].to<JsonArray>();
  for (const auto& p : hub.plants()) toJson(p, arr.add<JsonObject>());
  std::string json;
  serializeJson(doc, json);
  logStore.writeBackup(json);
}

[[noreturn]] void netTask(void*) {
  uint32_t lastPushMs = 0, lastAlertsMs = 0, lastTickMs = 0;
  bool firmwareConfirmed = false;
  int32_t lastBackupDay = -1;
  for (;;) {
    const uint32_t now = millis();
    wifi.loop(now);
    timeKeeper.loop();
    notifier.loop(now);

    button.poll(now);
    if (button.takePress() == PanelButton::Press::Long) wifi.requestSetupAp();
    led.update(now);

    if (now - lastTickMs >= config::kStationTickMs) {
      lastTickMs = now;
      hub.tick(now, nodeLink.channel(), wifi.apActive());
      led.setPattern(hub.ledPattern());
    }
    hub.flushPushes();
    if (now - lastPushMs >= config::kStatusPushMs) {
      lastPushMs = now;
      web.pushStatus();
    }
    if (now - lastAlertsMs >= 10000) {
      lastAlertsMs = now;
      updateSystemAlerts();
    }
    // New firmware stays "pending verify" until it has run for a minute; if it
    // crashes before that, the bootloader rolls back to the previous image.
    if (!firmwareConfirmed && now > config::kHealthyAfterMs) {
      esp_ota_mark_app_valid_cancel_rollback();
      firmwareConfirmed = true;
    }
    nightlyBackup(lastBackupDay);
    if (web.rebootRequested()) {
      delay(100);
      ESP.restart();
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

}  // namespace

// Keep a freshly flashed image unconfirmed until netTask decides it is healthy.
extern "C" bool verifyRollbackLater() { return true; }

void setup() {
  Serial.begin(115200);
  log_i("PlantCare main station %s starting", PLANTCARE_VERSION);

  led.begin();
  button.begin();
  esp_task_wdt_init(8, true);
  configStore.begin();

  Wire.begin(pins::kI2cSda, pins::kI2cScl, 100000);
  Wire.setTimeOut(50);
  I2cLock::init();
  timeKeeper.begin(Wire);
  environment.begin(Wire);
  logStore.begin(timeKeeper);

  hub.begin();
  reportResetReason();
  updateSystemAlerts();

  auth.begin(configStore);
  wifi.begin(configStore, timeKeeper);
  nodeLink.begin(hub);  // ESP-NOW needs Wi-Fi started
  notifier.begin(hub, wifi, timeKeeper);
  hub.setPusher([](const char* type, const std::string& json) { web.pushEvent(type, json); });
  web.begin();

  xTaskCreatePinnedToCore(radioTask, "radio", 8192, nullptr, 5, nullptr, 1);
  xTaskCreatePinnedToCore(storageTask, "storage", 6144, nullptr, 2, nullptr, 0);
  xTaskCreatePinnedToCore(netTask, "net", 10240, nullptr, 1, nullptr, 0);
  log_i("Started: %s", wifi.configured() ? "joining Wi-Fi" : "setup access point");
}

void loop() { vTaskDelete(nullptr); }
