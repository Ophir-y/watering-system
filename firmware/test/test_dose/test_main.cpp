#include <unity.h>

#include "Dose.h"

using namespace plantcore;

void setUp() {}
void tearDown() {}

struct Pump : IPump {
  bool running = false;
  int starts = 0;
  void on() override {
    running = true;
    starts++;
  }
  void off() override { running = false; }
};

struct Rig {
  Pump pump;
  DoseController dose{pump};
  MonoMs now = 0;

  void step(MonoMs dt) {
    now += dt;
    dose.tick(now);
  }
  void runUntilFinished(MonoMs limitMs = 300000) {
    while (!dose.finished() && now < limitMs) step(100);
  }
};

static DoseRequest req(float ml, float flow = 2) {
  DoseRequest r;
  r.ml = ml;
  r.flowMlPerS = flow;
  r.maxOnMs = 60000;
  return r;
}

void test_runs_for_calculated_time() {
  Rig rig;
  TEST_ASSERT_TRUE(rig.dose.start(req(30), 0));
  TEST_ASSERT_TRUE(rig.pump.running);
  TEST_ASSERT_TRUE(rig.dose.running());
  rig.runUntilFinished();
  TEST_ASSERT_EQUAL(DoseState::Done, rig.dose.state());
  TEST_ASSERT_EQUAL(DoseFault::None, rig.dose.fault());
  TEST_ASSERT_FALSE(rig.pump.running);
  TEST_ASSERT_INT_WITHIN(150, 15000, static_cast<int>(rig.dose.onTimeMs()));
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 30.0f, rig.dose.dispensedMl());
}

void test_cannot_start_twice() {
  Rig rig;
  TEST_ASSERT_TRUE(rig.dose.start(req(50), 0));
  TEST_ASSERT_FALSE(rig.dose.start(req(50), 0));
  TEST_ASSERT_EQUAL(1, rig.pump.starts);
}

void test_rejects_empty_request() {
  Rig rig;
  TEST_ASSERT_FALSE(rig.dose.start(req(0), 0));
  TEST_ASSERT_FALSE(rig.pump.running);
}

void test_capped_by_max_on_time() {
  Rig rig;
  DoseRequest r = req(1000, 1);
  r.maxOnMs = 10000;
  rig.dose.start(r, 0);
  rig.runUntilFinished();
  TEST_ASSERT_INT_WITHIN(150, 10000, static_cast<int>(rig.dose.onTimeMs()));
  TEST_ASSERT_FLOAT_WITHIN(0.2f, 10.0f, rig.dose.dispensedMl());
}

void test_dispensed_so_far_while_running() {
  Rig rig;
  rig.dose.start(req(40), 0);
  for (int i = 0; i < 50; ++i) rig.step(100);
  TEST_ASSERT_FLOAT_WITHIN(0.3f, 10.0f, rig.dose.dispensedSoFar(rig.now));
}

void test_fault_counts_water_until_it_stopped_flowing() {
  Rig rig;
  rig.dose.start(req(40), 0);
  for (int i = 0; i < 80; ++i) rig.step(100);  // 8 s
  rig.dose.fail(DoseFault::PumpDry, rig.now, 5000);  // the current dropped at 5 s
  TEST_ASSERT_FALSE(rig.pump.running);
  TEST_ASSERT_EQUAL(DoseFault::PumpDry, rig.dose.fault());
  TEST_ASSERT_EQUAL(DoseState::Failed, rig.dose.state());
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 10.0f, rig.dose.dispensedMl());
  TEST_ASSERT_INT_WITHIN(1, 8000, static_cast<int>(rig.dose.onTimeMs()));
}

void test_fault_with_no_flow_counts_nothing() {
  Rig rig;
  rig.dose.start(req(40), 1000);
  rig.now = 3000;
  rig.dose.fail(DoseFault::PumpDisconnected, rig.now, 0);
  TEST_ASSERT_EQUAL_FLOAT(0, rig.dose.dispensedMl());
}

void test_abort_stops_pump() {
  Rig rig;
  rig.dose.start(req(50), 0);
  rig.step(1000);
  rig.dose.abort(rig.now);
  TEST_ASSERT_FALSE(rig.pump.running);
  TEST_ASSERT_EQUAL(DoseFault::Aborted, rig.dose.fault());
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 2.0f, rig.dose.dispensedMl());
  rig.dose.acknowledge();
  TEST_ASSERT_TRUE(rig.dose.idle());
}

void test_abort_when_idle_still_switches_off() {
  Rig rig;
  rig.pump.running = true;  // e.g. left on by a crash before the engine started
  rig.dose.abort(0);
  TEST_ASSERT_FALSE(rig.pump.running);
  TEST_ASSERT_TRUE(rig.dose.idle());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_runs_for_calculated_time);
  RUN_TEST(test_cannot_start_twice);
  RUN_TEST(test_rejects_empty_request);
  RUN_TEST(test_capped_by_max_on_time);
  RUN_TEST(test_dispensed_so_far_while_running);
  RUN_TEST(test_fault_counts_water_until_it_stopped_flowing);
  RUN_TEST(test_fault_with_no_flow_counts_nothing);
  RUN_TEST(test_abort_stops_pump);
  RUN_TEST(test_abort_when_idle_still_switches_off);
  return UNITY_END();
}
