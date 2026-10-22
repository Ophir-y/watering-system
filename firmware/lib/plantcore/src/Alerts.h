// Alert book-keeping: one active alert per (code, channel), acknowledgement,
// a bounded history of cleared alerts, and push-notification scheduling.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Types.h"

namespace plantcore {

const char* alertCodeName(AlertCode code);
bool parseAlertCode(const std::string& name, AlertCode& out);
const char* severityName(Severity s);

// How often an unacknowledged alert is pushed again (0 = never).
EpochS renotifyIntervalS(Severity s);

class AlertManager {
 public:
  explicit AlertManager(size_t maxCleared = 50) : maxCleared_(maxCleared) {}

  // Returns true when a new alert was raised (not just an update of an active one).
  bool raise(AlertCode code, Severity sev, int8_t channel, const std::string& message, EpochS now);
  // Returns true when an active alert was cleared.
  bool clear(AlertCode code, int8_t channel, EpochS now);
  void clearChannel(int8_t channel, EpochS now);
  bool ack(uint32_t id);

  bool isActive(AlertCode code, int8_t channel) const;
  const Alert* find(uint32_t id) const;
  const std::vector<Alert>& all() const { return alerts_; }
  int countActive(Severity s) const;
  // Highest severity among active alerts; unacknowledged only if requested. Returns false if none.
  bool highestActive(Severity& out, bool unackedOnly) const;

  std::vector<uint32_t> dueNotifications(EpochS now, Severity minSeverity) const;
  void markNotified(uint32_t id, EpochS now);

 private:
  Alert* findActive(AlertCode code, int8_t channel);
  void trim();

  std::vector<Alert> alerts_;
  uint32_t nextId_ = 1;
  size_t maxCleared_;
};

}  // namespace plantcore
