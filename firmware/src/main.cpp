// PlantCare firmware entry point: brings up the hardware in a safe order and
// starts the tasks.
//
//   safety  (core 1, prio 6, 50 ms)  interlocks, control watchdog, button, LED/buzzer
//   control (core 1, prio 5, 100 ms) sensors -> engine -> outlets, logging
//   storage (core 0, prio 2)         SD card writes
//   net     (core 0, prio 1, 20 ms)  Wi-Fi, NTP, notifications, live status, OTA health
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_task_wdt.h>

#include "app/Controller.h"
#include "config.h"
#include "hal/I2cDevices.h"
#include "hal/Outlets.h"
#include "hal/Panel.h"
#include "hal/Sensors.h"
#include "hal/TimeKeeper.h"
#include "net/Auth.h"
#include "net/Notifier.h"
#include "net/WebApi.h"
#include "net/WifiService.h"
#include "pins.h"
#include "storage/ConfigStore.h"
#include "storage/LogStore.h"

using namespace plantcore;

namespace {

Outlets outlets;
MoistureBank moisture;
EnvSensors environment;
Scale scale;
TimeKeeper timeKeeper;
ConfigStore configStore;
LogStore logStore;
PanelInputs inputs;
Indicators indicators;
Controller controller(outlets, moisture, environment, scale, timeKeeper, configStore, logStore, inputs, indicators);
Auth auth;
WifiService wifi;
Notifier notifier;
WebApi web(controller, auth, wifi, logStore, timeKeeper, notifier);

void systemAlert(AlertCode code, Severity sev, bool active, const char* message) {
  controller.withEngine([&](Engine& e) { e.setSystemAlert(code, sev, active, message); }, 500);
}

void reportResetReason() {
  const esp_reset_reason_t reason = esp_reset_reason();
  const char* what = nullptr;
  switch (reason) {
    case ESP_RST_PANIC: what = "The controller restarted after a crash."; break;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: what = "The controller restarted after a watchdog timeout."; break;
    case ESP_RST_BROWNOUT: what = "The controller restarted after a brown-out: check the power supply."; break;
    default: break;
  }
  if (what) systemAlert(AlertCode::UnexpectedReset, Severity::Warning, true, what);
}

void updateSystemAlerts() {
  systemAlert(AlertCode::TimeNotSet, Severity::Warning, !timeKeeper.valid(),
              "The clock is not set (no RTC time and no internet). Open the dashboard to set it from your "
              "browser; watering windows and daily limits need it.");
  systemAlert(AlertCode::StorageFault, Severity::Warning, !logStore.sdOk(),
              "The SD card is missing or failing. History is kept in memory for the last day only.");
  systemAlert(AlertCode::EnvSensorFault, Severity::Info, !environment.climatePresent(),
              "No temperature/humidity sensor found: environmental watering adjustments are off.");
}

void controlTask(void*) { controller.controlTask(); }
void safetyTask(void*) { controller.safetyTask(); }
void storageTask(void*) { logStore.task(); }

[[noreturn]] void netTask(void*) {
  uint32_t lastPushMs = 0;
  uint32_t lastAlertsMs = 0;
  bool firmwareConfirmed = false;
  int32_t lastBackupDay = -1;
  for (;;) {
    const uint32_t now = millis();
    wifi.loop(now);
    controller.setApActive(wifi.apActive());
    timeKeeper.loop();
    notifier.loop(now);

    if (now - lastPushMs >= config::kStatusPushMs) {
      lastPushMs = now;
      web.pushStatus();
    }
    if (now - lastAlertsMs >= 10000) {
      lastAlertsMs = now;
      updateSystemAlerts();
    }
    // New firmware stays "pending verify" until it has run cleanly for a minute;
    // if it crashes before that, the bootloader rolls back to the previous image.
    if (!firmwareConfirmed && now > config::kHealthyAfterMs && controller.healthy()) {
      esp_ota_mark_app_valid_cancel_rollback();
      firmwareConfirmed = true;
    }
    // Nightly configuration backup to the SD card.
    if (timeKeeper.valid() && logStore.sdOk()) {
      const LocalTime lt = timeKeeper.local(timeKeeper.now());
      if (lt.hour == 3 && lt.day != lastBackupDay) {
        lastBackupDay = lt.day;
        JsonDocument doc;
        doc["version"] = PLANTCARE_VERSION;
        toJson(controller.settings(), doc["settings"].to<JsonObject>());
        JsonArray arr = doc["plants"].to<JsonArray>();
        for (const auto& p : controller.plants()) toJson(p, arr.add<JsonObject>());
        std::string json;
        serializeJson(doc, json);
        logStore.writeBackup(json);
      }
    }
    if (web.rebootRequested()) {
      outlets.closeAll();
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
  Outlets::forceSafe();  // before anything else: every actuator off
  Serial.begin(115200);
  log_i("PlantCare %s starting", PLANTCARE_VERSION);

  indicators.begin();
  esp_task_wdt_init(8, true);
  outlets.begin();
  configStore.begin();

  Wire.begin(pins::kI2cSda, pins::kI2cScl, 100000);
  Wire.setTimeOut(50);
  I2cLock::init();
  timeKeeper.begin(Wire);
  moisture.begin(Wire);
  environment.begin(Wire);
  scale.begin(pins::kScaleDout, pins::kScaleSck);
  inputs.begin();
  logStore.begin(timeKeeper);

  controller.begin();
  reportResetReason();
  updateSystemAlerts();

  auth.begin(configStore);
  wifi.begin(configStore, timeKeeper);
  notifier.begin(controller, wifi, timeKeeper);
  controller.setPusher([](const char* type, const std::string& json) { web.pushEvent(type, json); });
  controller.setSetupApRequest([] { wifi.requestSetupAp(); });
  web.begin();

  xTaskCreatePinnedToCore(safetyTask, "safety", 6144, nullptr, 6, nullptr, 1);
  xTaskCreatePinnedToCore(controlTask, "control", 8192, nullptr, 5, nullptr, 1);
  xTaskCreatePinnedToCore(storageTask, "storage", 6144, nullptr, 2, nullptr, 0);
  xTaskCreatePinnedToCore(netTask, "net", 10240, nullptr, 1, nullptr, 0);
  indicators.chirp(1);
  log_i("Started: %s", wifi.configured() ? "joining Wi-Fi" : "setup access point");
}

void loop() { vTaskDelete(nullptr); }
