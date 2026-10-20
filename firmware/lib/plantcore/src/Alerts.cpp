#include "Alerts.h"

#include <algorithm>

namespace plantcore {

const char* alertCodeName(AlertCode code) {
  switch (code) {
    case AlertCode::ReservoirLow: return "reservoir_low";
    case AlertCode::ReservoirEmpty: return "reservoir_empty";
    case AlertCode::ReservoirUnexplainedLoss: return "reservoir_unexplained_loss";
    case AlertCode::FloatScaleDisagree: return "float_scale_disagree";
    case AlertCode::ScaleFault: return "scale_fault";
    case AlertCode::LeakDetected: return "leak_detected";
    case AlertCode::EmergencyStop: return "emergency_stop";
    case AlertCode::SensorFault: return "sensor_fault";
    case AlertCode::SensorNoResponse: return "sensor_no_response";
    case AlertCode::OutletNoFlow: return "outlet_no_flow";
    case AlertCode::OutletStuckOpen: return "outlet_stuck_open";
    case AlertCode::PlantTooDry: return "plant_too_dry";
    case AlertCode::PlantTooWet: return "plant_too_wet";
    case AlertCode::TargetNotReached: return "target_not_reached";
    case AlertCode::DailyLimitReached: return "daily_limit_reached";
    case AlertCode::TempOutOfRange: return "temp_out_of_range";
    case AlertCode::HumidityOutOfRange: return "humidity_out_of_range";
    case AlertCode::EnvSensorFault: return "env_sensor_fault";
    case AlertCode::InterruptedDispense: return "interrupted_dispense";
    case AlertCode::TimeNotSet: return "time_not_set";
    case AlertCode::StorageFault: return "storage_fault";
    case AlertCode::UnexpectedReset: return "unexpected_reset";
    case AlertCode::CalibrationNeeded: return "calibration_needed";
    case AlertCode::Count: break;
  }
  return "unknown";
}

const char* severityName(Severity s) {
  switch (s) {
    case Severity::Info: return "info";
    case Severity::Warning: return "warning";
    case Severity::Critical: return "critical";
  }
  return "info";
}

EpochS renotifyIntervalS(Severity s) {
  switch (s) {
    case Severity::Critical: return 6 * 3600;
    case Severity::Warning: return 24 * 3600;
    case Severity::Info: return 0;
  }
  return 0;
}

Alert* AlertManager::findActive(AlertCode code, int8_t channel) {
  for (auto& a : alerts_) {
    if (a.active && a.code == code && a.channel == channel) return &a;
  }
  return nullptr;
}

bool AlertManager::raise(AlertCode code, Severity sev, int8_t channel, const std::string& message, EpochS now) {
  if (Alert* a = findActive(code, channel)) {
    a->message = message;
    if (sev > a->severity) {
      // Escalation: treat as new for notification purposes.
      a->severity = sev;
      a->acked = false;
      a->notifiedAt = 0;
    }
    return false;
  }
  Alert a;
  a.id = nextId_++;
  a.code = code;
  a.severity = sev;
  a.channel = channel;
  a.active = true;
  a.raisedAt = now;
  a.message = message;
  alerts_.push_back(a);
  trim();
  return true;
}

bool AlertManager::clear(AlertCode code, int8_t channel, EpochS now) {
  Alert* a = findActive(code, channel);
  if (!a) return false;
  a->active = false;
  a->clearedAt = now;
  trim();
  return true;
}

void AlertManager::clearChannel(int8_t channel, EpochS now) {
  for (auto& a : alerts_) {
    if (a.active && a.channel == channel) {
      a.active = false;
      a.clearedAt = now;
    }
  }
  trim();
}

bool AlertManager::ack(uint32_t id) {
  for (auto& a : alerts_) {
    if (a.id == id) {
      a.acked = true;
      return true;
    }
  }
  return false;
}

bool AlertManager::isActive(AlertCode code, int8_t channel) const {
  return std::any_of(alerts_.begin(), alerts_.end(),
                     [&](const Alert& a) { return a.active && a.code == code && a.channel == channel; });
}

const Alert* AlertManager::find(uint32_t id) const {
  for (const auto& a : alerts_) {
    if (a.id == id) return &a;
  }
  return nullptr;
}

int AlertManager::countActive(Severity s) const {
  return static_cast<int>(
      std::count_if(alerts_.begin(), alerts_.end(), [&](const Alert& a) { return a.active && a.severity == s; }));
}

bool AlertManager::highestActive(Severity& out, bool unackedOnly) const {
  bool found = false;
  for (const auto& a : alerts_) {
    if (!a.active || (unackedOnly && a.acked)) continue;
    if (!found || a.severity > out) out = a.severity;
    found = true;
  }
  return found;
}

std::vector<uint32_t> AlertManager::dueNotifications(EpochS now, Severity minSeverity) const {
  std::vector<uint32_t> due;
  for (const auto& a : alerts_) {
    if (!a.active || a.acked || a.severity < minSeverity) continue;
    if (a.notifiedAt == 0) {
      due.push_back(a.id);
      continue;
    }
    EpochS every = renotifyIntervalS(a.severity);
    if (every > 0 && now - a.notifiedAt >= every) due.push_back(a.id);
  }
  return due;
}

void AlertManager::markNotified(uint32_t id, EpochS now) {
  for (auto& a : alerts_) {
    if (a.id == id) a.notifiedAt = now > 0 ? now : 1;
  }
}

void AlertManager::trim() {
  size_t cleared = std::count_if(alerts_.begin(), alerts_.end(), [](const Alert& a) { return !a.active; });
  // alerts_ is in raise order, so the first cleared entries are the oldest.
  for (auto it = alerts_.begin(); cleared > maxCleared_ && it != alerts_.end();) {
    if (!it->active) {
      it = alerts_.erase(it);
      cleared--;
    } else {
      ++it;
    }
  }
}

}  // namespace plantcore
