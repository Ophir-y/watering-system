#include "TimeKeeper.h"

#include <esp_sntp.h>
#include <sys/time.h>
#include <time.h>

#include "pins.h"

volatile bool TimeKeeper::ntpSynced_ = false;
volatile bool TimeKeeper::ntpPending_ = false;

namespace {
constexpr int64_t kEarliestValid = 1735689600;  // 2025-01-01: anything earlier is an unset clock
}  // namespace

int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

void TimeKeeper::begin(TwoWire& wire) {
  rtc_.begin(wire, pins::kRtcAddress);
  int64_t epoch = 0;
  if (rtc_.read(epoch) && epoch > kEarliestValid) {
    setSystemTime(epoch);
  } else {
    rtcLost_ = rtc_.present();
  }
}

void TimeKeeper::setTimezone(const std::string& posixTz) {
  tz_ = posixTz.empty() ? "UTC0" : posixTz;
  setenv("TZ", tz_.c_str(), 1);
  tzset();
}

void TimeKeeper::startNtp() {
  if (ntpStarted_) return;
  ntpStarted_ = true;
  sntp_set_time_sync_notification_cb(&TimeKeeper::onNtpSync);
  configTzTime(tz_.c_str(), "pool.ntp.org", "time.google.com", "time.cloudflare.com");
}

void TimeKeeper::onNtpSync(struct timeval*) {
  ntpSynced_ = true;
  ntpPending_ = true;
}

void TimeKeeper::loop() {
  if (ntpPending_) {
    ntpPending_ = false;
    if (rtc_.present() || rtc_.begin(Wire, pins::kRtcAddress)) {
      if (rtc_.write(now())) rtcLost_ = false;
    }
  }
}

bool TimeKeeper::valid() const { return now() > kEarliestValid; }

int64_t TimeKeeper::now() const { return static_cast<int64_t>(time(nullptr)); }

void TimeKeeper::setSystemTime(int64_t epoch) {
  timeval tv{static_cast<time_t>(epoch), 0};
  settimeofday(&tv, nullptr);
}

void TimeKeeper::setManual(int64_t epoch) {
  if (epoch <= kEarliestValid) return;
  setSystemTime(epoch);
  if (rtc_.write(epoch)) rtcLost_ = false;
}

LocalTime TimeKeeper::local(int64_t epoch) const {
  LocalTime lt;
  time_t t = static_cast<time_t>(epoch);
  struct tm tmv;
  localtime_r(&t, &tmv);
  lt.hour = tmv.tm_hour;
  lt.month = tmv.tm_mon + 1;
  lt.day = static_cast<int32_t>(daysFromCivil(tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday));
  strftime(lt.date, sizeof lt.date, "%Y-%m-%d", &tmv);
  return lt;
}
