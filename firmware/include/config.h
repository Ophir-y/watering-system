// Compile-time constants. Runtime settings live in plantcore::Settings (editable in the dashboard).
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifndef PLANTCARE_VERSION
#define PLANTCARE_VERSION "dev"
#endif

namespace config {

// Tasks
constexpr uint32_t kControlPeriodMs = 100;
constexpr uint32_t kSafetyPeriodMs = 50;
constexpr uint32_t kStatusPushMs = 5000;        // live status to dashboard clients
constexpr uint32_t kControlStallCloseMs = 2000; // control task silent: close outlets
constexpr uint32_t kControlStallRebootMs = 15000;
constexpr uint32_t kHealthyAfterMs = 60000;     // new firmware confirmed (no OTA rollback)

// Outlets: independent hardware-timer cut-off beyond the software limit
constexpr uint32_t kHardCutoffMarginMs = 5000;

// Sensors
constexpr int kMoistureSamples = 9;             // median of N conversions per reading
constexpr uint32_t kScaleStaleMs = 1500;        // no HX711 reading for this long = fault
constexpr bool kLeakActiveLow = true;
constexpr bool kFloatOpenMeansLow = true;

// Wi-Fi
constexpr const char* kHostname = "plantcare";
constexpr const char* kDefaultApPassword = "plantcare";  // >= 8 chars (WPA2); change in Settings
constexpr uint32_t kStaRetryMs = 30000;
constexpr uint32_t kFallbackApAfterMs = 10UL * 60 * 1000;
constexpr uint32_t kApOffAfterStaMs = 5UL * 60 * 1000;
constexpr uint32_t kSetupApMs = 15UL * 60 * 1000;
constexpr uint32_t kInternetCheckMs = 5UL * 60 * 1000;

// Logging
constexpr size_t kRamReadingsPerPlant = 144;    // 24 h at 10-minute intervals
constexpr size_t kRamEnvRows = 144;
constexpr size_t kRamEvents = 100;
constexpr int kLongRangeDays = 14;              // longer queries use daily summaries
constexpr float kSdMinFreeMb = 64;              // prune oldest days below this

// Web / auth
constexpr size_t kMaxBodyBytes = 16 * 1024;
constexpr uint32_t kTokenLifetimeS = 30UL * 24 * 3600;
constexpr int kMaxTokens = 8;
constexpr int kLoginFailuresBeforeDelay = 5;
constexpr uint32_t kLoginLockoutMs = 30000;

// Notifications
constexpr uint32_t kNotifyPollMs = 10000;
constexpr uint32_t kNotifyMaxBackoffMs = 30UL * 60 * 1000;
constexpr uint16_t kNotifyTimeoutMs = 8000;

}  // namespace config
