#include <unity.h>

#include "Alerts.h"

using namespace plantcore;

void setUp() {}
void tearDown() {}

void test_raise_is_deduplicated_per_code_and_channel() {
  AlertManager m;
  TEST_ASSERT_TRUE(m.raise(AlertCode::PlantTooDry, Severity::Warning, 0, "dry", 100));
  TEST_ASSERT_FALSE(m.raise(AlertCode::PlantTooDry, Severity::Warning, 0, "drier", 200));
  TEST_ASSERT_TRUE(m.raise(AlertCode::PlantTooDry, Severity::Warning, 1, "dry", 200));
  TEST_ASSERT_EQUAL(2, m.countActive(Severity::Warning));
  TEST_ASSERT_EQUAL_STRING("drier", m.all()[0].message.c_str());
}

void test_clear_and_reraise() {
  AlertManager m;
  m.raise(AlertCode::ReservoirLow, Severity::Warning, -1, "low", 100);
  TEST_ASSERT_TRUE(m.clear(AlertCode::ReservoirLow, -1, 200));
  TEST_ASSERT_FALSE(m.clear(AlertCode::ReservoirLow, -1, 300));
  TEST_ASSERT_FALSE(m.isActive(AlertCode::ReservoirLow, -1));
  TEST_ASSERT_TRUE(m.raise(AlertCode::ReservoirLow, Severity::Warning, -1, "low again", 400));
  TEST_ASSERT_EQUAL(2u, m.all().size());
  TEST_ASSERT_EQUAL(200, m.all()[0].clearedAt);
}

void test_notifications_and_renotify() {
  AlertManager m;
  m.raise(AlertCode::LeakDetected, Severity::Critical, -1, "leak", 1000);
  m.raise(AlertCode::TempOutOfRange, Severity::Info, 0, "cold", 1000);
  auto due = m.dueNotifications(1000, Severity::Warning);
  TEST_ASSERT_EQUAL(1u, due.size());
  m.markNotified(due[0], 1000);
  TEST_ASSERT_EQUAL(0u, m.dueNotifications(2000, Severity::Warning).size());
  TEST_ASSERT_EQUAL(1u, m.dueNotifications(1000 + 6 * 3600, Severity::Warning).size());
  m.ack(due[0]);
  TEST_ASSERT_EQUAL(0u, m.dueNotifications(1000 + 12 * 3600, Severity::Warning).size());
}

void test_escalation_renotifies() {
  AlertManager m;
  m.raise(AlertCode::SensorFault, Severity::Info, 2, "suspect", 10);
  m.raise(AlertCode::SensorFault, Severity::Warning, 2, "failed", 20);
  auto due = m.dueNotifications(20, Severity::Warning);
  TEST_ASSERT_EQUAL(1u, due.size());
  TEST_ASSERT_EQUAL(Severity::Warning, m.find(due[0])->severity);
}

void test_highest_active() {
  AlertManager m;
  Severity s = Severity::Info;
  TEST_ASSERT_FALSE(m.highestActive(s, false));
  m.raise(AlertCode::ReservoirLow, Severity::Warning, -1, "", 0);
  uint32_t crit = 0;
  m.raise(AlertCode::LeakDetected, Severity::Critical, -1, "", 0);
  for (const auto& a : m.all()) {
    if (a.severity == Severity::Critical) crit = a.id;
  }
  TEST_ASSERT_TRUE(m.highestActive(s, true));
  TEST_ASSERT_EQUAL(Severity::Critical, s);
  m.ack(crit);
  TEST_ASSERT_TRUE(m.highestActive(s, true));
  TEST_ASSERT_EQUAL(Severity::Warning, s);
}

void test_history_is_bounded() {
  AlertManager m(5);
  for (int i = 0; i < 20; ++i) {
    m.raise(AlertCode::PlantTooWet, Severity::Warning, 0, "wet", i);
    m.clear(AlertCode::PlantTooWet, 0, i);
  }
  m.raise(AlertCode::LeakDetected, Severity::Critical, -1, "leak", 30);
  TEST_ASSERT_EQUAL(6u, m.all().size());
  TEST_ASSERT_TRUE(m.isActive(AlertCode::LeakDetected, -1));
}

void test_clear_channel() {
  AlertManager m;
  m.raise(AlertCode::PlantTooDry, Severity::Warning, 3, "", 0);
  m.raise(AlertCode::SensorFault, Severity::Warning, 3, "", 0);
  m.raise(AlertCode::ReservoirLow, Severity::Warning, -1, "", 0);
  m.clearChannel(3, 10);
  TEST_ASSERT_EQUAL(1, m.countActive(Severity::Warning));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_raise_is_deduplicated_per_code_and_channel);
  RUN_TEST(test_clear_and_reraise);
  RUN_TEST(test_notifications_and_renotify);
  RUN_TEST(test_escalation_renotifies);
  RUN_TEST(test_highest_active);
  RUN_TEST(test_history_is_bounded);
  RUN_TEST(test_clear_channel);
  return UNITY_END();
}
