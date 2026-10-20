// Wall-clock time: the DS3231 keeps it through power and internet outages, NTP
// corrects it when online, and the dashboard can set it when there is no internet.
#pragma once

#include <Arduino.h>
#include <Wire.h>

#include <string>

#include "I2cDevices.h"

// Days since 1970-01-01 for a calendar date.
int64_t daysFromCivil(int year, unsigned month, unsigned day);

struct LocalTime {
  int hour = 12;
  int month = 1;
  int32_t day = 0;  // days since epoch, local
  char date[11] = "1970-01-01";
};

class TimeKeeper {
 public:
  void begin(TwoWire& wire);
  void setTimezone(const std::string& posixTz);
  void startNtp();
  void loop();  // network task: copy fresh NTP time into the RTC
  bool valid() const;
  int64_t now() const;
  void setManual(int64_t epoch);
  LocalTime local(int64_t epoch) const;

  bool rtcPresent() const { return rtc_.present(); }
  bool rtcLostPower() const { return rtcLost_; }
  bool ntpSynced() const { return ntpSynced_; }

 private:
  static void onNtpSync(struct timeval* tv);
  void setSystemTime(int64_t epoch);

  Ds3231 rtc_;
  bool rtcLost_ = false;
  bool ntpStarted_ = false;
  std::string tz_ = "UTC0";
  static volatile bool ntpSynced_;
  static volatile bool ntpPending_;
};
