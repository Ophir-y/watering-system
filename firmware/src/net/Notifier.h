// Push notifications through ntfy (https://ntfy.sh or a self-hosted server).
// Runs in the network task; waits with exponential backoff while offline.
#pragma once

#include <Arduino.h>

#include <atomic>
#include <string>

#include "Types.h"

class Controller;
class WifiService;
class TimeKeeper;

class Notifier {
 public:
  void begin(Controller& controller, WifiService& wifi, TimeKeeper& clock);
  void loop(uint32_t nowMs);
  void requestTest() { testRequested_ = true; }
  std::string lastTestResult() const;
  int64_t lastTestAt() const { return lastTestAt_; }

 private:
  bool send(const plantcore::NotifyConfig& cfg, const std::string& title, const std::string& message,
            plantcore::Severity severity, std::string& error);

  Controller* controller_ = nullptr;
  WifiService* wifi_ = nullptr;
  TimeKeeper* clock_ = nullptr;
  uint32_t lastPollMs_ = 0;
  uint32_t nextAttemptMs_ = 0;
  uint32_t backoffMs_ = 0;
  std::atomic<bool> testRequested_{false};
  std::string lastTestResult_;
  int64_t lastTestAt_ = 0;
  SemaphoreHandle_t mutex_ = nullptr;
};
