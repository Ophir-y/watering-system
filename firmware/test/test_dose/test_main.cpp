#include <unity.h>

#include "Dose.h"

using namespace plantcore;

void setUp() {}
void tearDown() {}

struct Outlets : IOutlets {
  int open_ = -1;
  int opens = 0;
  void open(uint8_t ch) override {
    open_ = ch;
    opens++;
  }
  void closeAll() override { open_ = -1; }
};

// Simple reservoir: drains at `flow` ml/s while the outlet is open (or while stuck).
struct Rig {
  Outlets outlets;
  DoseController dose{outlets};
  float grams = 3000;
  float flow = 2;
  bool stuck = false;
  bool scaleOk = true;
  MonoMs now = 0;

  void step(MonoMs dt) {
    if (outlets.open_ >= 0 || stuck) grams -= flow * dt / 1000.0f;
    now += dt;
    dose.tick(now, scaleOk, grams);
  }
  void runUntilFinished(MonoMs limitMs = 300000) {
    while (!dose.finished() && now < limitMs) step(100);
  }
};

static DoseRequest req(float ml, float flow = 2, bool closed = true) {
  DoseRequest r;
  r.channel = 3;
  r.ml = ml;
  r.flowMlPerS = flow;
  r.maxOnMs = 60000;
  r.closedLoop = closed;
  return r;
}

void test_closed_loop_dispenses_requested_volume() {
  Rig rig;
  TEST_ASSERT_TRUE(rig.dose.start(req(50), 0, true, rig.grams));
  TEST_ASSERT_EQUAL(3, rig.outlets.open_);
  TEST_ASSERT_TRUE(rig.dose.outletOpen());
  rig.runUntilFinished();
  TEST_ASSERT_EQUAL(DoseState::Done, rig.dose.state());
  TEST_ASSERT_EQUAL(DoseFault::None, rig.dose.fault());
  TEST_ASSERT_EQUAL(-1, rig.outlets.open_);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 48.0f, rig.dose.dispensedMl());  // stops 2 ml early for in-flight water
}

void test_cannot_start_twice() {
  Rig rig;
  TEST_ASSERT_TRUE(rig.dose.start(req(50), 0, true, rig.grams));
  TEST_ASSERT_FALSE(rig.dose.start(req(50), 0, true, rig.grams));
  TEST_ASSERT_EQUAL(1, rig.outlets.opens);
}

void test_no_flow_detected() {
  Rig rig;
  rig.flow = 0;
  rig.dose.start(req(50), 0, true, rig.grams);
  rig.runUntilFinished();
  TEST_ASSERT_EQUAL(DoseState::Failed, rig.dose.state());
  TEST_ASSERT_EQUAL(DoseFault::NoFlow, rig.dose.fault());
  TEST_ASSERT_EQUAL(-1, rig.outlets.open_);
  TEST_ASSERT_TRUE(rig.now <= 16000);  // detected at 60 % of the expected 25 s
}

void test_slow_flow_keeps_partial_dose() {
  Rig rig;
  rig.flow = 0.6f;  // under a third of the calibrated flow
  DoseRequest r = req(50);
  r.maxOnMs = 40000;
  rig.dose.start(r, 0, true, rig.grams);
  rig.runUntilFinished();
  TEST_ASSERT_EQUAL(DoseFault::None, rig.dose.fault());
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 24.0f, rig.dose.dispensedMl());
}

void test_stuck_open_detected_after_close() {
  Rig rig;
  rig.dose.start(req(50), 0, true, rig.grams);
  while (rig.dose.outletOpen()) rig.step(100);
  rig.stuck = true;  // water keeps flowing although the outlet was closed
  rig.runUntilFinished();
  TEST_ASSERT_EQUAL(DoseState::Failed, rig.dose.state());
  TEST_ASSERT_EQUAL(DoseFault::StuckOpen, rig.dose.fault());
}

void test_scale_lost_mid_dose() {
  Rig rig;
  rig.dose.start(req(50), 0, true, rig.grams);
  for (int i = 0; i < 50; ++i) rig.step(100);
  rig.scaleOk = false;
  rig.step(100);
  TEST_ASSERT_EQUAL(DoseFault::ScaleLost, rig.dose.fault());
  TEST_ASSERT_EQUAL(-1, rig.outlets.open_);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 10.2f, rig.dose.dispensedMl());
}

void test_open_loop_runs_for_calculated_time() {
  Rig rig;
  rig.dose.start(req(30, 2, false), 0, false, 0);
  rig.runUntilFinished();
  TEST_ASSERT_EQUAL(DoseState::Done, rig.dose.state());
  TEST_ASSERT_INT_WITHIN(150, 15000, static_cast<int>(rig.dose.onTimeMs()));
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 30.0f, rig.dose.dispensedMl());
}

void test_open_loop_capped_by_max_on_time() {
  Rig rig;
  DoseRequest r = req(1000, 1, false);
  r.maxOnMs = 10000;
  rig.dose.start(r, 0, false, 0);
  rig.runUntilFinished();
  TEST_ASSERT_INT_WITHIN(150, 10000, static_cast<int>(rig.dose.onTimeMs()));
}

void test_closed_loop_falls_back_to_open_loop_without_scale() {
  Rig rig;
  rig.dose.start(req(20, 2, true), 0, false, 0);
  TEST_ASSERT_FALSE(rig.dose.request().closedLoop);
}

void test_abort_closes_outlet() {
  Rig rig;
  rig.dose.start(req(50), 0, true, rig.grams);
  rig.step(1000);
  rig.dose.abort(rig.now);
  TEST_ASSERT_EQUAL(-1, rig.outlets.open_);
  TEST_ASSERT_EQUAL(DoseFault::Aborted, rig.dose.fault());
  rig.dose.acknowledge();
  TEST_ASSERT_TRUE(rig.dose.idle());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_closed_loop_dispenses_requested_volume);
  RUN_TEST(test_cannot_start_twice);
  RUN_TEST(test_no_flow_detected);
  RUN_TEST(test_slow_flow_keeps_partial_dose);
  RUN_TEST(test_stuck_open_detected_after_close);
  RUN_TEST(test_scale_lost_mid_dose);
  RUN_TEST(test_open_loop_runs_for_calculated_time);
  RUN_TEST(test_open_loop_capped_by_max_on_time);
  RUN_TEST(test_closed_loop_falls_back_to_open_loop_without_scale);
  RUN_TEST(test_abort_closes_outlet);
  return UNITY_END();
}
