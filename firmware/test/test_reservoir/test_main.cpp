#include <unity.h>

#include "Reservoir.h"

using namespace plantcore;

void setUp() {}
void tearDown() {}

struct Rig {
  ReservoirModel model;
  MonoMs now = 0;
  float grams = 3000;
  bool floatLow = false;
  bool dosing = false;
  bool scaleOk = true;
  ReservoirEvent last;
  int events = 0;

  Rig() {
    ReservoirConfig cfg;
    cfg.capacityMl = 5000;
    model.configure(cfg);
  }
  void run(MonoMs ms, MonoMs step = 1000) {
    for (MonoMs t = 0; t < ms; t += step) {
      now += step;
      model.update(now, scaleOk, grams, floatLow, dosing);
      ReservoirEvent e;
      if (model.takeEvent(e)) {
        last = e;
        events++;
      }
    }
  }
};

void test_status_levels() {
  Rig r;
  r.run(15000);
  ReservoirStatus s = r.model.status();
  TEST_ASSERT_TRUE(s.scaleValid);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 60.0f, s.pct);
  TEST_ASSERT_FALSE(s.low);
  TEST_ASSERT_FALSE(s.empty);
  r.grams = 900;
  r.run(1000);
  TEST_ASSERT_TRUE(r.model.status().low);
  r.grams = 100;
  r.run(1000);
  TEST_ASSERT_TRUE(r.model.status().empty);
}

void test_float_switch_overrides_scale() {
  Rig r;
  r.floatLow = true;
  r.run(15000);
  ReservoirStatus s = r.model.status();
  TEST_ASSERT_TRUE(s.empty);
  TEST_ASSERT_TRUE(s.disagree);  // scale says 60 %
}

void test_refill_detected() {
  Rig r;
  r.run(15000);
  r.grams = 4500;
  r.run(15000);
  TEST_ASSERT_EQUAL(1, r.events);
  TEST_ASSERT_EQUAL(ReservoirEventType::Refill, r.last.type);
  TEST_ASSERT_FLOAT_WITHIN(1, 1500, r.last.ml);
}

void test_lift_off_and_refill_is_not_a_leak() {
  Rig r;
  r.run(15000);
  r.grams = -400;  // container lifted: net weight goes negative
  r.run(30000);
  TEST_ASSERT_TRUE(r.model.status().empty);
  r.grams = 4800;
  r.run(15000);
  TEST_ASSERT_EQUAL(1, r.events);
  TEST_ASSERT_EQUAL(ReservoirEventType::Refill, r.last.type);
  TEST_ASSERT_FALSE(r.model.status().empty);
}

void test_dispensing_is_not_a_leak() {
  Rig r;
  r.run(15000);
  r.dosing = true;
  r.grams -= 150;
  r.run(20000);
  r.dosing = false;
  r.model.accountDispensed(150);
  r.run(60000);
  TEST_ASSERT_EQUAL(0, r.events);
}

void test_fast_unexplained_loss() {
  Rig r;
  r.run(15000);
  r.grams -= 100;  // e.g. a valve stuck open draining quickly
  r.run(40000);
  TEST_ASSERT_EQUAL(1, r.events);
  TEST_ASSERT_EQUAL(ReservoirEventType::UnexplainedLoss, r.last.type);
}

void test_slow_leak_accumulates() {
  Rig r;
  r.run(15000);
  // 1 ml every 6 s = 10 ml/min: never fast, but adds up within minutes
  for (int i = 0; i < 120 && r.events == 0; ++i) {
    r.grams -= 1;
    r.run(6000);
  }
  TEST_ASSERT_EQUAL(1, r.events);
  TEST_ASSERT_EQUAL(ReservoirEventType::UnexplainedLoss, r.last.type);
}

void test_evaporation_does_not_alarm() {
  Rig r;
  r.run(15000);
  // 2 ml/hour for 2 days, sampled every minute
  for (int i = 0; i < 48 * 60; ++i) {
    r.grams -= 2.0f / 60.0f;
    r.run(60000, 60000);
  }
  TEST_ASSERT_EQUAL(0, r.events);
}

void test_scale_failure() {
  Rig r;
  r.run(15000);
  r.scaleOk = false;
  r.run(1000);
  ReservoirStatus s = r.model.status();
  TEST_ASSERT_FALSE(s.scaleValid);
  TEST_ASSERT_FALSE(s.empty);  // unknown, the float switch still guards
}

void test_usage_forecast() {
  Rig r;
  r.run(15000);
  r.model.onDayRollover(300);
  r.model.onDayRollover(300);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 300, r.model.usagePerDay());
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 10, r.model.status().daysLeft);
  r.grams = 500;
  r.run(1000);
  TEST_ASSERT_TRUE(r.model.status().low);  // < 2 days left
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_status_levels);
  RUN_TEST(test_float_switch_overrides_scale);
  RUN_TEST(test_refill_detected);
  RUN_TEST(test_lift_off_and_refill_is_not_a_leak);
  RUN_TEST(test_dispensing_is_not_a_leak);
  RUN_TEST(test_fast_unexplained_loss);
  RUN_TEST(test_slow_leak_accumulates);
  RUN_TEST(test_evaporation_does_not_alarm);
  RUN_TEST(test_scale_failure);
  RUN_TEST(test_usage_forecast);
  return UNITY_END();
}
