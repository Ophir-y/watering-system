// Multi-day simulations of whole plant nodes: plant, bottle, pump and engine.
#include <unity.h>

#include <map>
#include <memory>

#include "../sim.h"

using namespace plantcore;
using namespace sim;

void setUp() {}
void tearDown() {}

static const MonoMs kMin = 60000;
static const MonoMs kHour = 60 * kMin;
static const MonoMs kDay = 24 * kHour;

// One node: its world, its engine and what it logged.
struct Node {
  World world;
  RecordingSink sink;
  Engine engine{world, sink};
  PlantProfile plant;
  float maxOver = -1000;   // worst (true moisture - band.max)
  float minUnder = 1000;   // worst (true moisture - band.critical), after warm-up
  int refills = 0;

  explicit Node(PlantProfile p) : plant(std::move(p)) {}
  void start(const Settings& s) {
    engine.configure(s, &plant);
    engine.restore(PersistedState{}, world.inputs());
  }
  // A person who refills the bottle when the node says it is low, and presses the button.
  void careTaker() {
    const bool asked = engine.alerts().isActive(AlertCode::BottleLow, 0) ||
                       engine.alerts().isActive(AlertCode::BottleEmpty, 0);
    if (asked && !engine.doseActive()) {
      world.bottleMl = plant.bottle.capacityMl;
      engine.refilled(0);
      refills++;
    }
  }
  void step(MonoMs warmupUntil, bool refill) {
    engine.tick(world.inputs());
    maxOver = std::max(maxOver, world.plant.moisture - plant.moisture.max);
    if (world.now > warmupUntil) minUnder = std::min(minUnder, world.plant.moisture - plant.moisture.critical);
    if (refill) careTaker();
    const bool busy = engine.doseActive() || world.pumpOn;
    world.advance(busy ? 100 : 10000);
  }
  void simulate(MonoMs duration, bool refill = true, MonoMs warmup = kDay) {
    const MonoMs end = world.now + duration;
    while (world.now < end) step(warmup, refill);
  }
};

static std::map<int64_t, float> dailyTotals(const RecordingSink& sink) {
  std::map<int64_t, float> out;
  for (const auto& ev : sink.events) {
    if (ev.type == EventType::Watering) out[ev.ts / 86400] += ev.ml;
  }
  return out;
}

void test_month_with_mixed_plants_stays_in_band() {
  std::vector<std::unique_ptr<Node>> nodes;

  PlantProfile fern = makePlant("fern");
  nodes.push_back(std::make_unique<Node>(fern));
  nodes.back()->world.plant.dryPctPerHour = 0.6f;
  nodes.back()->world.plant.mlPerPct = 12;

  PlantProfile pothos = makePlant("pothos");
  pothos.style = WateringStyle::DryBetween;
  pothos.moisture = {15, 25, 50, 70};
  nodes.push_back(std::make_unique<Node>(pothos));
  nodes.back()->world.plant.dryPctPerHour = 0.3f;

  PlantProfile succulent = makePlant("succulent");
  succulent.style = WateringStyle::SoakAndDry;
  succulent.moisture = {5, 12, 40, 62};
  succulent.potVolumeMl = 1000;
  succulent.minIntervalH = 48;
  succulent.bottle.capacityMl = 500;
  nodes.push_back(std::make_unique<Node>(succulent));
  nodes.back()->world.plant.dryPctPerHour = 0.15f;
  nodes.back()->world.plant.mlPerPct = 6;
  nodes.back()->world.plant.moisture = 30;
  nodes.back()->world.bottleMl = 500;

  PlantProfile basil = makePlant("basil");
  basil.moisture = {25, 40, 60, 80};
  basil.maxDailyMl = 800;
  basil.bottle.capacityMl = 2000;
  nodes.push_back(std::make_unique<Node>(basil));
  nodes.back()->world.plant.dryPctPerHour = 1.2f;
  nodes.back()->world.plant.mlPerPct = 8;
  nodes.back()->world.bottleMl = 2000;
  nodes.back()->world.flowMlPerS = 2.3f;  // a stronger pump than its profile says

  for (auto& n : nodes) {
    n->start(Settings{});
    n->simulate(30 * kDay);
  }

  for (auto& n : nodes) {
    char msg[128];
    snprintf(msg, sizeof msg, "%s: max over band %.1f, min above critical %.1f", n->plant.id.c_str(), n->maxOver,
             n->minUnder);
    TEST_ASSERT_TRUE_MESSAGE(n->maxOver <= 3.0f, msg);   // never meaningfully overwatered
    TEST_ASSERT_TRUE_MESSAGE(n->minUnder >= 0.0f, msg);  // never left below critical
    for (const auto& kv : dailyTotals(n->sink)) TEST_ASSERT_TRUE(kv.second <= n->plant.maxDailyMl + 1);
    TEST_ASSERT_EQUAL(0, n->engine.alerts().countActive(Severity::Critical));
  }
  // The thirsty basil needs far more water (and refills) than the succulent.
  TEST_ASSERT_TRUE(nodes[3]->sink.wateredMl() > 4 * nodes[2]->sink.wateredMl());
  TEST_ASSERT_TRUE(nodes[3]->refills > nodes[2]->refills);
  TEST_ASSERT_TRUE(nodes[3]->refills >= 3);
}

void test_forgotten_bottle_runs_dry_safely() {
  PlantProfile p = makePlant("fern");
  p.bottle.capacityMl = 600;
  Node n(p);
  n.world.bottleMl = 600;
  n.world.plant.dryPctPerHour = 1.0f;
  n.start(Settings{});
  n.engine.refilled(0);
  n.simulate(20 * kDay, false);
  const NodeStatus st = n.engine.status();
  TEST_ASSERT_TRUE(st.bottle.empty);
  TEST_ASSERT_TRUE(n.engine.alerts().isActive(AlertCode::BottleEmpty, 0));
  // Everything usable went to the plant, and the pump never ran dry for long.
  TEST_ASSERT_TRUE(n.world.bottleMl <= n.world.intakeMl + 50);
  TEST_ASSERT_FLOAT_WITHIN(0.05f * n.world.dispensedTotal + 5, n.world.dispensedTotal, n.sink.wateredMl());
  TEST_ASSERT_FALSE(st.lockedOut);
}

void test_flow_self_calibration_over_refill_cycles() {
  PlantProfile p = makePlant("basil");
  p.bottle.capacityMl = 800;
  p.maxDailyMl = 1000;
  Node n(p);
  n.world.flowMlPerS = 2.6f;  // the profile says 2.0
  n.world.bottleMl = 800;
  n.world.plant.dryPctPerHour = 1.5f;
  n.start(Settings{});
  n.engine.refilled(0);
  // Refill only when the pump says empty, so each cycle can be learned from.
  const MonoMs end = 25 * kDay;
  while (n.world.now < end) {
    n.step(kDay, false);
    if (n.engine.status().bottle.empty && !n.engine.doseActive()) {
      n.world.bottleMl = 800;
      n.engine.refilled(0);
      n.refills++;
    }
  }
  TEST_ASSERT_TRUE(n.refills >= 3);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 1.3f, n.engine.status().bottle.flowFactor);
  // So the gauge now tracks the real bottle closely.
  TEST_ASSERT_FLOAT_WITHIN(80.0f, n.world.bottleMl, n.engine.status().bottle.ml);
}

void test_sensor_dislodged_midway_limits_damage() {
  Node n(makePlant("fern"));
  n.world.plant.dryPctPerHour = 0.6f;
  n.start(Settings{});
  n.simulate(5 * kDay);
  const float before = n.sink.wateredMl();

  n.world.plant.sensorInSoil = false;  // knocked out of the pot: reads bone dry
  n.simulate(5 * kDay);
  const float after = n.sink.wateredMl() - before;
  TEST_ASSERT_TRUE(after <= 2 * n.plant.maxPulseMl + 1);  // at most one failed session
  TEST_ASSERT_TRUE(n.engine.alerts().isActive(AlertCode::SensorNoResponse, 0));
  TEST_ASSERT_TRUE(n.maxOver <= 3.0f);

  // Put it back. The pot has dried out meanwhile, so the reading does not rise by
  // itself: the user clears the fault from the dashboard and watering resumes.
  n.world.plant.sensorInSoil = true;
  n.simulate(1 * kHour);
  TEST_ASSERT_TRUE(n.engine.alerts().isActive(AlertCode::SensorNoResponse, 0));
  TEST_ASSERT_EQUAL_STRING("", n.engine.clearFaults().c_str());
  n.simulate(3 * kDay);
  TEST_ASSERT_FALSE(n.engine.alerts().isActive(AlertCode::SensorNoResponse, 0));
  TEST_ASSERT_TRUE(n.sink.wateredMl() - before > after);
}

void test_power_cut_mid_dose_does_not_double_water() {
  World w;
  RecordingSink sink;
  PlantProfile p = makePlant("fern");
  w.plant.moisture = 30;
  {
    Engine e(w, sink);
    e.configure(w.settings(), &p);
    e.restore(PersistedState{}, w.inputs());
    w.lastSampleMs = -1000000;
    run(w, e, 15000);
    TEST_ASSERT_TRUE(e.doseActive());
    TEST_ASSERT_TRUE(sink.last.dispenseInProgress);
  }
  // Batteries pulled: the pump stops, the node reboots.
  w.off();
  const PersistedState saved = sink.last;
  w.now += 5000;
  RecordingSink sink2;
  Engine e2(w, sink2);
  e2.configure(w.settings(), &p);
  e2.restore(saved, w.inputs());
  TEST_ASSERT_TRUE(e2.alerts().isActive(AlertCode::InterruptedDispense, 0));
  run(w, e2, 5 * kHour);
  TEST_ASSERT_EQUAL(0, sink2.count(EventType::Watering, "auto"));  // min interval honoured
  run(w, e2, 3 * kHour);
  TEST_ASSERT_TRUE(sink2.count(EventType::Watering, "auto") >= 1);  // and then it catches up
}

void test_keeps_watering_without_the_main_station() {
  // Rebooted while the main station is off: no clock, no room climate.
  Node n(makePlant("fern"));
  n.world.clockValid = false;
  n.world.env.valid = false;
  n.world.env.luxValid = false;
  n.world.plant.dryPctPerHour = 0.8f;
  n.start(Settings{});
  n.simulate(6 * kDay);
  TEST_ASSERT_TRUE(n.maxOver <= 3.0f);
  TEST_ASSERT_TRUE(n.minUnder >= 0.0f);
  TEST_ASSERT_TRUE(n.sink.count(EventType::Watering, "auto") >= 3);
  // Daily limits still reset (every 24 h of uptime).
  TEST_ASSERT_TRUE(n.engine.status().mlToday <= n.plant.maxDailyMl);
}

void test_five_minute_check_ins_keep_the_plant_in_band() {
  // Between sessions a battery node only reads its soil at each check-in.
  Node n(makePlant("fern"));
  n.world.sampleIntervalS = 300;
  n.world.plant.dryPctPerHour = 0.7f;
  n.start(Settings{});
  n.simulate(10 * kDay);
  TEST_ASSERT_TRUE(n.maxOver <= 3.0f);
  TEST_ASSERT_TRUE(n.minUnder >= 0.0f);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_month_with_mixed_plants_stays_in_band);
  RUN_TEST(test_forgotten_bottle_runs_dry_safely);
  RUN_TEST(test_flow_self_calibration_over_refill_cycles);
  RUN_TEST(test_sensor_dislodged_midway_limits_damage);
  RUN_TEST(test_power_cut_mid_dose_does_not_double_water);
  RUN_TEST(test_keeps_watering_without_the_main_station);
  RUN_TEST(test_five_minute_check_ins_keep_the_plant_in_band);
  return UNITY_END();
}
