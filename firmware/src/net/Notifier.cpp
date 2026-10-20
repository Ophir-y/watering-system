#include "Notifier.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <vector>

#include "../app/Controller.h"
#include "../hal/TimeKeeper.h"
#include "Alerts.h"
#include "CaRoots.h"
#include "WifiService.h"
#include "config.h"

using namespace plantcore;

namespace {

// "reservoir_low" -> "Reservoir low" (HTTP header values must stay ASCII).
std::string titleFor(const char* code) {
  std::string t = code;
  for (auto& c : t) {
    if (c == '_') c = ' ';
  }
  if (!t.empty()) t[0] = static_cast<char>(toupper(static_cast<unsigned char>(t[0])));
  return t;
}

const char* priorityFor(Severity s) {
  switch (s) {
    case Severity::Critical: return "urgent";
    case Severity::Warning: return "high";
    default: return "default";
  }
}

const char* tagsFor(Severity s) {
  switch (s) {
    case Severity::Critical: return "rotating_light,potted_plant";
    case Severity::Warning: return "warning,potted_plant";
    default: return "potted_plant";
  }
}

}  // namespace

void Notifier::begin(Controller& controller, WifiService& wifi, TimeKeeper& clock) {
  controller_ = &controller;
  wifi_ = &wifi;
  clock_ = &clock;
  mutex_ = xSemaphoreCreateMutex();
}

std::string Notifier::lastTestResult() const {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  std::string r = lastTestResult_;
  xSemaphoreGive(mutex_);
  return r;
}

bool Notifier::send(const NotifyConfig& cfg, const std::string& title, const std::string& message, Severity severity,
                    std::string& error) {
  if (cfg.topic.empty()) {
    error = "no topic configured";
    return false;
  }
  std::string url = cfg.server;
  if (!url.empty() && url.back() == '/') url.pop_back();
  url += "/" + cfg.topic;

  WiFiClientSecure secure;
  WiFiClient plain;
  HTTPClient http;
  bool started;
  if (url.rfind("https://", 0) == 0) {
    secure.setCACert(kCaRoots);
    started = http.begin(secure, url.c_str());
  } else {
    started = http.begin(plain, url.c_str());
  }
  if (!started) {
    error = "invalid server URL";
    return false;
  }
  http.setConnectTimeout(config::kNotifyTimeoutMs);
  http.setTimeout(config::kNotifyTimeoutMs);
  http.addHeader("Content-Type", "text/plain; charset=utf-8");
  http.addHeader("Title", title.c_str());
  http.addHeader("Priority", priorityFor(severity));
  http.addHeader("Tags", tagsFor(severity));
  if (!cfg.token.empty()) http.addHeader("Authorization", ("Bearer " + cfg.token).c_str());
  const int code = http.POST(reinterpret_cast<uint8_t*>(const_cast<char*>(message.data())), message.size());
  http.end();
  if (code >= 200 && code < 300) return true;
  error = code > 0 ? "server answered HTTP " + std::to_string(code) : std::string("connection failed: ") +
                                                                            HTTPClient::errorToString(code).c_str();
  return false;
}

void Notifier::loop(uint32_t nowMs) {
  const bool test = testRequested_.exchange(false);
  if (!test && nowMs - lastPollMs_ < config::kNotifyPollMs) return;
  lastPollMs_ = nowMs;

  const Settings settings = controller_->settings();
  const NotifyConfig& cfg = settings.notify;

  if (test) {
    std::string error;
    bool ok = wifi_->staConnected() &&
              send(cfg, settings.deviceName + ": test", "Notifications from " + settings.deviceName + " are working.",
                   Severity::Info, error);
    if (!wifi_->staConnected()) error = "not connected to Wi-Fi";
    xSemaphoreTake(mutex_, portMAX_DELAY);
    lastTestResult_ = ok ? "ok" : error;
    lastTestAt_ = clock_->now();
    xSemaphoreGive(mutex_);
  }

  if (!cfg.enabled || !wifi_->staConnected() || !clock_->valid()) return;
  if (static_cast<int32_t>(nextAttemptMs_ - nowMs) > 0) return;

  struct Pending {
    uint32_t id;
    Severity severity;
    std::string title;
    std::string message;
  };
  std::vector<Pending> due;
  const int64_t now = clock_->now();
  controller_->withEngine([&](Engine& e) {
    for (uint32_t id : e.alerts().dueNotifications(now, static_cast<Severity>(cfg.minSeverity))) {
      const Alert* a = e.alerts().find(id);
      if (a) due.push_back({id, a->severity, settings.deviceName + ": " + titleFor(alertCodeName(a->code)), a->message});
    }
  });

  for (const auto& p : due) {
    std::string error;
    if (!send(cfg, p.title, p.message, p.severity, error)) {
      backoffMs_ = backoffMs_ ? std::min<uint32_t>(backoffMs_ * 2, config::kNotifyMaxBackoffMs) : 60000;
      nextAttemptMs_ = nowMs + backoffMs_;
      log_w("Notification failed (%s), retrying in %u s", error.c_str(), backoffMs_ / 1000);
      return;
    }
    controller_->withEngine([&](Engine& e) { e.alerts().markNotified(p.id, now); });
  }
  backoffMs_ = 0;
}
