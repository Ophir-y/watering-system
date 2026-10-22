// The bottle's fuel gauge: counting, refills, empty detection and flow self-calibration.
#include <unity.h>

#include "Bottle.h"

using namespace plantcore;

void setUp() {}
void tearDown() {}

static BottleModel bottle(float capacity = 1000) {
  BottleConfig cfg;
  cfg.capacityMl = capacity;
  cfg.lowPct = 25;
  cfg.reserveMl = 50;
  BottleModel b;
  b.configure(cfg);
  return b;
}

void test_new_bottle_assumed_full() {
  BottleModel b = bottle();
  BottleStatus s = b.status();
  TEST_ASSERT_EQUAL_FLOAT(1000, s.ml);
  TEST_ASSERT_EQUAL_FLOAT(100, s.pct);
  TEST_ASSERT_FALSE(s.low);
  TEST_ASSERT_FALSE(s.empty);
  TEST_ASSERT_FALSE(b.state().knownFull);  // not confirmed: flow is not learned from this cycle
}

void test_counts_what_was_pumped() {
  BottleModel b = bottle();
  b.dispensed(120, 120);
  b.dispensed(80, 80);
  TEST_ASSERT_EQUAL_FLOAT(800, b.status().ml);
  TEST_ASSERT_EQUAL_FLOAT(80, b.status().pct);
  b.dispensed(5000, 5000);
  TEST_ASSERT_EQUAL_FLOAT(0, b.status().ml);  // never negative
  TEST_ASSERT_TRUE(b.belowReserve());
}

void test_low_by_level_or_forecast() {
  BottleModel b = bottle();
  b.dispensed(760, 760);  // 24 %
  TEST_ASSERT_TRUE(b.status().low);

  BottleModel c = bottle();
  c.onDayRollover(600);  // a thirsty plant: 600 ml a day
  TEST_ASSERT_FALSE(c.status().low);  // a full bottle is never "low", even for 1.7 days
  c.dispensed(600, 600); // 40 % left, under 2 days
  BottleStatus s = c.status();
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.667f, s.daysLeft);
  TEST_ASSERT_TRUE(s.low);
}

void test_refill_resets_and_clears_empty() {
  BottleModel b = bottle();
  b.dispensed(990, 990);
  TEST_ASSERT_TRUE(b.markEmpty(EmptyReason::Gauge));
  TEST_ASSERT_FALSE(b.markEmpty(EmptyReason::Gauge));  // already empty
  TEST_ASSERT_TRUE(b.status().empty);
  TEST_ASSERT_EQUAL(EmptyReason::Gauge, b.status().reason);
  b.refilled(0);
  TEST_ASSERT_FALSE(b.status().empty);
  TEST_ASSERT_EQUAL_FLOAT(1000, b.status().ml);
  TEST_ASSERT_TRUE(b.state().knownFull);
}

void test_partial_top_up() {
  BottleModel b = bottle();
  b.refilled(0);
  b.dispensed(700, 700);
  b.refilled(300);
  TEST_ASSERT_EQUAL_FLOAT(600, b.status().ml);
  TEST_ASSERT_FALSE(b.state().knownFull);
}

void test_pump_dry_from_full_learns_a_faster_pump() {
  BottleModel b = bottle();
  b.refilled(0);
  // The pump really moves 1.25x its nominal flow: the gauge only counted 800 ml
  // when the whole litre had gone.
  for (int i = 0; i < 8; ++i) b.dispensed(100, 100);
  TEST_ASSERT_EQUAL_FLOAT(200, b.status().ml);
  b.markEmpty(EmptyReason::PumpDry);
  // Half way towards the observed 1.25 in one cycle.
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.125f, b.flowFactor());
  TEST_ASSERT_EQUAL_FLOAT(0, b.status().ml);
}

void test_pump_dry_from_full_learns_a_slower_pump() {
  BottleModel b = bottle();
  b.refilled(0);
  // The gauge counted 1250 ml (already "empty" for a while) before the pump ran dry.
  for (int i = 0; i < 10; ++i) b.dispensed(125, 125);
  b.markEmpty(EmptyReason::PumpDry);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.9f, b.flowFactor());
}

void test_no_learning_without_a_confirmed_full_cycle() {
  BottleModel b = bottle();  // never confirmed full
  for (int i = 0; i < 8; ++i) b.dispensed(100, 100);
  b.markEmpty(EmptyReason::PumpDry);
  TEST_ASSERT_EQUAL_FLOAT(1, b.flowFactor());

  b.refilled(0);
  b.dispensed(100, 100);  // too little pumped to judge
  b.markEmpty(EmptyReason::PumpDry);
  TEST_ASSERT_EQUAL_FLOAT(1, b.flowFactor());

  b.refilled(0);
  for (int i = 0; i < 8; ++i) b.dispensed(100, 100);
  b.markEmpty(EmptyReason::NoResponse);  // not certain it is empty: no learning
  TEST_ASSERT_EQUAL_FLOAT(1, b.flowFactor());
}

void test_flow_factor_is_bounded() {
  BottleModel b = bottle();
  for (int cycle = 0; cycle < 10; ++cycle) {
    b.refilled(0);
    b.dispensed(310, 310);  // claims the bottle held 3x more than it did
    b.markEmpty(EmptyReason::PumpDry);
  }
  TEST_ASSERT_TRUE(b.flowFactor() <= kFlowFactorMax);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, kFlowFactorMax, b.flowFactor());
}

void test_restore_keeps_state() {
  BottleModel b = bottle();
  b.refilled(0);
  b.dispensed(300, 250);
  BottleState saved = b.state();
  BottleModel c = bottle();
  c.restore(saved);
  TEST_ASSERT_EQUAL_FLOAT(700, c.status().ml);
  TEST_ASSERT_EQUAL_FLOAT(250, c.state().pumpedSinceRefillMl);
  TEST_ASSERT_TRUE(c.state().knownFull);

  BottleState unset;  // ml = -1: never set
  c.restore(unset);
  TEST_ASSERT_EQUAL_FLOAT(1000, c.status().ml);
}

void test_capacity_change_clamps_level() {
  BottleModel b = bottle(1000);
  BottleConfig smaller;
  smaller.capacityMl = 600;
  b.configure(smaller);
  TEST_ASSERT_EQUAL_FLOAT(600, b.status().ml);
}

void test_usage_forecast() {
  BottleModel b = bottle();
  b.onDayRollover(200);
  b.onDayRollover(100);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 170.0f, b.state().usagePerDayMl);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1000.0f / 170.0f, b.status().daysLeft);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_new_bottle_assumed_full);
  RUN_TEST(test_counts_what_was_pumped);
  RUN_TEST(test_low_by_level_or_forecast);
  RUN_TEST(test_refill_resets_and_clears_empty);
  RUN_TEST(test_partial_top_up);
  RUN_TEST(test_pump_dry_from_full_learns_a_faster_pump);
  RUN_TEST(test_pump_dry_from_full_learns_a_slower_pump);
  RUN_TEST(test_no_learning_without_a_confirmed_full_cycle);
  RUN_TEST(test_flow_factor_is_bounded);
  RUN_TEST(test_restore_keeps_state);
  RUN_TEST(test_capacity_change_clamps_level);
  RUN_TEST(test_usage_forecast);
  return UNITY_END();
}
