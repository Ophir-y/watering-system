// Multi-day simulations of the whole engine against simulated plants and a reservoir.
#include <unity.h>

#include <map>

#include "../sim.h"

using namespace plantcore;
using namespace sim;

void setUp() {}
void tearDown() {}

static const MonoMs kMin = 60000;
static const MonoMs kHour = 60 * kMin;
static const MonoMs kDay = 24 * kHour;

struct Tracker {
  std::vector<float> maxOver;   // worst (true moisture - band.max)
  std::vector<float> minUnder;  // worst (true moisture - band.critical), after warm-up
  explicit Tracker(size_t n) : maxOver(n, -1000), minUnder(n, 1000) {}
};

// Runs the simulation, refilling the reservoir like a person would when it gets low.
static void simulate(World& w, Engine& e, const std::vector<PlantProfile>& plants, MonoMs duration, Tracker& t,
                     MonoMs warmup = kDay) {
  const MonoMs end = w.now + duration;
  while (w.now < end) {
    e.tick(w.inputs());
    for (size_t i = 0; i < plants.size(); ++i) {
      t.maxOver[i] = std::max(t.maxOver[i], w.plants[i].moisture - plants[i].moisture.max);
      if (w.now > warmup) t.minUnder[i] = std::min(t.minUnder[i], w.plants[i].moisture - plants[i].moisture.critical);
    }
    if (w.reservoirMl < 800 && !e.doseActive()) w.reservoirMl = 5000;
    bool busy = e.doseActive() || w.openChannel >= 0;
    w.advance(busy ? 250 : 10000);
  }
}

static std::map<std::pair<int, int64_t>, float> dailyTotals(const RecordingSink& sink) {
  std::map<std::pair<int, int64_t>, float> out;
  for (const auto& ev : sink.events) {
    if (ev.type == EventType::Watering) out[{ev.channel, ev.ts / 86400}] += ev.ml;
  }
  return out;
}

void test_month_with_mixed_plants_stays_in_band() {
  World w(4);
  RecordingSink sink;
  Engine e(w, sink);
  Settings s;

  std::vector<PlantProfile> plants;
  PlantProfile fern = makePlant("fern", 0);
  plants.push_back(fern);
  w.plants[0].dryPctPerHour = 0.6f;
  w.plants[0].mlPerPct = 12;

  PlantProfile pothos = makePlant("pothos", 1);
  pothos.style = WateringStyle::DryBetween;
  pothos.moisture = {15, 25, 50, 70};
  plants.push_back(pothos);
  w.plants[1].dryPctPerHour = 0.3f;

  PlantProfile succulent = makePlant("succulent", 2);
  succulent.style = WateringStyle::SoakAndDry;
  succulent.moisture = {5, 12, 40, 62};
  succulent.potVolumeMl = 1000;
  succulent.minIntervalH = 48;
  plants.push_back(succulent);
  w.plants[2].dryPctPerHour = 0.15f;
  w.plants[2].mlPerPct = 6;
  w.plants[2].moisture = 30;

  PlantProfile basil = makePlant("basil", 3);
  basil.moisture = {25, 40, 60, 80};
  basil.maxDailyMl = 800;
  plants.push_back(basil);
  w.plants[3].dryPctPerHour = 1.2f;
  w.plants[3].mlPerPct = 8;

  e.configure(s, plants);
  e.restore(PersistedState{}, w.inputs());
  Tracker t(plants.size());
  simulate(w, e, plants, 30 * kDay, t);

  for (size_t i = 0; i < plants.size(); ++i) {
    char msg[96];
    snprintf(msg, sizeof msg, "%s: max over band %.1f, min above critical %.1f", plants[i].id.c_str(), t.maxOver[i],
             t.minUnder[i]);
    TEST_ASSERT_TRUE_MESSAGE(t.maxOver[i] <= 3.0f, msg);   // never meaningfully overwatered
    TEST_ASSERT_TRUE_MESSAGE(t.minUnder[i] >= 0.0f, msg);  // never left below critical
  }
  for (const auto& kv : dailyTotals(sink)) {
    const PlantProfile& p = plants[kv.first.first];
    TEST_ASSERT_TRUE(kv.second <= p.maxDailyMl + 1);
  }
  TEST_ASSERT_TRUE(sink.count(EventType::Refill) > 0);
  TEST_ASSERT_EQUAL(0, e.alerts().countActive(Severity::Critical));
  // The thirsty basil needs far more water than the succulent.
  TEST_ASSERT_TRUE(sink.wateredMl(3) > 4 * sink.wateredMl(2));
}

void test_gravity_valves_as_reservoir_drains() {
  World w(2);
  w.gravity = true;
  w.reservoirMl = 5000;
  w.flowMlPerS = {3.0f, 3.0f};  // at full head
  RecordingSink sink;
  Engine e(w, sink);
  Settings s;
  std::vector<PlantProfile> plants{makePlant("a", 0), makePlant("b", 1)};
  for (auto& p : plants) {
    p.outlet.type = OutletType::Valve;
    p.outlet.flowMlPerS = 3.0f;
    p.outlet.calFillPct = 100;
    p.outlet.dropMm = 300;
  }
  w.plants[0].dryPctPerHour = w.plants[1].dryPctPerHour = 1.5f;
  e.configure(s, plants);
  e.restore(PersistedState{}, w.inputs());

  // No refills: let the reservoir run down to the empty threshold.
  const MonoMs end = 10 * kDay;
  float minMoisture = 100;
  while (w.now < end) {
    e.tick(w.inputs());
    if (w.now > kDay) minMoisture = std::min({minMoisture, w.plants[0].moisture, w.plants[1].moisture});
    bool busy = e.doseActive() || w.openChannel >= 0;
    w.advance(busy ? 250 : 10000);
    // Stop between sessions: watering events are logged when a session ends.
    if (w.reservoirMl < 600 && !busy) {
      EngineStatus st = e.status();
      bool idle = std::all_of(st.plants.begin(), st.plants.end(),
                              [](const PlantStatus& p) { return p.planner == PlannerState::Idle; });
      if (idle) break;
    }
  }
  // Low head never produced a false "no flow" lockout, and doses matched what the scale saw.
  TEST_ASSERT_FALSE(e.alerts().isActive(AlertCode::OutletNoFlow, 0));
  TEST_ASSERT_FALSE(e.alerts().isActive(AlertCode::OutletNoFlow, 1));
  TEST_ASSERT_FLOAT_WITHIN(0.02f * w.dispensedTotal + 5, w.dispensedTotal, sink.wateredMl());
  TEST_ASSERT_TRUE(minMoisture >= 20.0f);
}

void test_sensor_dislodged_midway_limits_damage() {
  World w(1);
  RecordingSink sink;
  Engine e(w, sink);
  std::vector<PlantProfile> plants{makePlant("fern", 0)};
  w.plants[0].dryPctPerHour = 0.6f;
  e.configure(Settings{}, plants);
  e.restore(PersistedState{}, w.inputs());
  Tracker t(1);
  simulate(w, e, plants, 5 * kDay, t);
  const float before = sink.wateredMl(0);

  w.plants[0].sensorInSoil = false;  // knocked out of the pot: reads bone dry
  simulate(w, e, plants, 5 * kDay, t);
  const float after = sink.wateredMl(0) - before;
  TEST_ASSERT_TRUE(after <= 2 * plants[0].maxPulseMl + 1);  // at most one failed session
  TEST_ASSERT_TRUE(e.alerts().isActive(AlertCode::SensorNoResponse, 0));
  TEST_ASSERT_TRUE(t.maxOver[0] <= 3.0f);

  // Put it back. The pot has dried out meanwhile, so the reading does not rise by
  // itself: the user clears the fault from the dashboard and watering resumes.
  w.plants[0].sensorInSoil = true;
  simulate(w, e, plants, 1 * kHour, t);
  TEST_ASSERT_TRUE(e.alerts().isActive(AlertCode::SensorNoResponse, 0));
  TEST_ASSERT_EQUAL_STRING("", e.clearLockout("fern").c_str());
  simulate(w, e, plants, 3 * kDay, t);
  TEST_ASSERT_FALSE(e.alerts().isActive(AlertCode::SensorNoResponse, 0));
  TEST_ASSERT_TRUE(sink.wateredMl(0) - before > after);
}

void test_power_cut_mid_dose_does_not_double_water() {
  World w(1);
  RecordingSink sink;
  std::vector<PlantProfile> plants{makePlant("fern", 0)};
  w.plants[0].moisture = 30;
  {
    Engine e(w, sink);
    e.configure(Settings{}, plants);
    e.restore(PersistedState{}, w.inputs());
    w.lastSampleMs = -1000000;
    run(w, e, 20000);
    TEST_ASSERT_TRUE(e.doseActive());
    TEST_ASSERT_TRUE(sink.last.dispenseInProgress);
  }
  // Power cut: outlets lose power, the controller reboots.
  w.closeAll();
  const PersistedState saved = sink.last;
  w.now += 5000;
  RecordingSink sink2;
  Engine e2(w, sink2);
  e2.configure(Settings{}, plants);
  e2.restore(saved, w.inputs());
  TEST_ASSERT_TRUE(e2.alerts().isActive(AlertCode::InterruptedDispense, 0));
  run(w, e2, 5 * kHour);
  TEST_ASSERT_EQUAL(0, sink2.count(EventType::Watering, "auto"));  // min interval honoured
  run(w, e2, 3 * kHour);
  TEST_ASSERT_TRUE(sink2.count(EventType::Watering, "auto") >= 1);  // and then it catches up
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_month_with_mixed_plants_stays_in_band);
  RUN_TEST(test_gravity_valves_as_reservoir_drains);
  RUN_TEST(test_sensor_dislodged_midway_limits_damage);
  RUN_TEST(test_power_cut_mid_dose_does_not_double_water);
  return UNITY_END();
}
