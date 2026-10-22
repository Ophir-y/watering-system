// Judging the pump by its current: learning the wet current, dry, unplugged, jammed.
#include <unity.h>

#include "PumpMonitor.h"

using namespace plantcore;

void setUp() {}
void tearDown() {}

// One run of `ms` at a constant current, sampled every 100 ms.
static PumpVerdict runAt(PumpMonitor& m, MonoMs& now, MonoMs ms, float mA, float mv = 4200, bool learn = true) {
  m.start(now);
  PumpVerdict v = PumpVerdict::Ok;
  for (MonoMs t = 0; t <= ms && v == PumpVerdict::Ok; t += 100) v = m.sample(now + t, mA, mv);
  now += ms + 1000;
  m.finish(learn);
  return v;
}

static PumpMonitor learned(MonoMs& now, float wetMa = 180) {
  PumpMonitor m;
  for (int i = 0; i < kLearnRuns; ++i) runAt(m, now, 5000, wetMa);
  return m;
}

void test_learns_wet_current_from_healthy_runs() {
  MonoMs now = 0;
  PumpMonitor m;
  TEST_ASSERT_FALSE(m.ready());
  runAt(m, now, 5000, 180, 4500);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 40.0f, m.learned().wetMaPerV);  // 180 mA / 4.5 V
  TEST_ASSERT_FALSE(m.ready());
  runAt(m, now, 5000, 180, 4500);
  runAt(m, now, 5000, 180, 4500);
  TEST_ASSERT_TRUE(m.ready());
  TEST_ASSERT_EQUAL(3, m.learned().runs);
}

void test_short_or_unconfirmed_runs_teach_nothing() {
  MonoMs now = 0;
  PumpMonitor m;
  runAt(m, now, 800, 180);  // barely past the inrush
  runAt(m, now, 5000, 180, 4200, false);  // the engine doubted the bottle had water
  TEST_ASSERT_EQUAL(0, m.learned().runs);
}

void test_dry_pump_detected() {
  MonoMs now = 0;
  PumpMonitor m = learned(now);
  m.start(now);
  PumpVerdict v = PumpVerdict::Ok;
  MonoMs t = 0;
  for (; t < 3000 && v == PumpVerdict::Ok; t += 100) v = m.sample(now + t, 180, 4200);
  const MonoMs dryAt = now + t;
  for (; t < 10000 && v == PumpVerdict::Ok; t += 100) v = m.sample(now + t, 95, 4200);
  TEST_ASSERT_EQUAL(PumpVerdict::Dry, v);
  TEST_ASSERT_INT_WITHIN(100, dryAt, m.drySinceMs());
  TEST_ASSERT_INT_WITHIN(200, kPumpDryForMs, now + t - 100 - dryAt);
  m.finish(true);
  TEST_ASSERT_EQUAL(3, m.learned().runs);  // a dry run never teaches the wet current
}

void test_battery_sag_is_not_mistaken_for_dry() {
  MonoMs now = 0;
  PumpMonitor m = learned(now, 180);  // learned at 4.2 V
  // Months later the cells give 3.4 V: the wet current drops in proportion.
  const float ma = 180 * 3.4f / 4.2f;
  TEST_ASSERT_EQUAL(PumpVerdict::Ok, runAt(m, now, 8000, ma, 3400));
}

void test_unplugged_pump_needs_no_learning() {
  MonoMs now = 0;
  PumpMonitor m;
  TEST_ASSERT_EQUAL(PumpVerdict::Disconnected, runAt(m, now, 5000, 3));
}

void test_jammed_pump_detected() {
  MonoMs now = 0;
  PumpMonitor m = learned(now);
  TEST_ASSERT_EQUAL(PumpVerdict::Jammed, runAt(m, now, 5000, 460));
}

void test_inrush_is_ignored() {
  MonoMs now = 0;
  PumpMonitor m = learned(now);
  m.start(now);
  TEST_ASSERT_EQUAL(PumpVerdict::Ok, m.sample(now + 100, 900, 4200));  // start-up surge
  TEST_ASSERT_EQUAL(PumpVerdict::Ok, m.sample(now + 300, 1, 4200));
}

void test_disabled_monitor_never_judges() {
  MonoMs now = 0;
  PumpMonitor m;
  m.configure(false);
  TEST_ASSERT_EQUAL(PumpVerdict::Ok, runAt(m, now, 5000, 0));
  TEST_ASSERT_FALSE(m.ready());
}

void test_restored_learning() {
  PumpMonitor m;
  m.restore(PumpLearned{42, 5});
  TEST_ASSERT_TRUE(m.ready());
  MonoMs now = 0;
  TEST_ASSERT_EQUAL(PumpVerdict::Dry, runAt(m, now, 5000, 0.5f * 42 * 4.2f));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_learns_wet_current_from_healthy_runs);
  RUN_TEST(test_short_or_unconfirmed_runs_teach_nothing);
  RUN_TEST(test_dry_pump_detected);
  RUN_TEST(test_battery_sag_is_not_mistaken_for_dry);
  RUN_TEST(test_unplugged_pump_needs_no_learning);
  RUN_TEST(test_jammed_pump_detected);
  RUN_TEST(test_inrush_is_ignored);
  RUN_TEST(test_disabled_monitor_never_judges);
  RUN_TEST(test_restored_learning);
  return UNITY_END();
}
