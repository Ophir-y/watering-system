// Environmental adjustments to watering: drying demand (VPD), cold and dormancy.
#pragma once

#include "Types.h"

namespace plantcore {

// Saturation vapour pressure over water (Tetens), kPa.
float saturationVaporPressureKPa(float tempC);

// Vapour-pressure deficit: how strongly the air pulls water out of soil and leaves.
float vpdKPa(float tempC, float humidityPct);

struct WateringAdjust {
  float triggerOffsetPct = 0;  // added to the profile's trigger (positive = water sooner)
  float doseFactor = 1;        // multiplies pulse size
  bool tooCold = false;
  bool dormant = false;
};

// month: 1..12 local. southern: flips the dormancy mask by six months.
bool isDormantMonth(const PlantProfile& p, int month, bool southern);

WateringAdjust computeAdjust(const PlantProfile& p, const EnvSnapshot& env, int month, bool southern);

}  // namespace plantcore
