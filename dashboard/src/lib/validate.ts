// Client-side checks mirroring validateProfile() in the firmware, so problems show
// next to the field before saving. The device validates again on save.
import type { PlantProfile } from '../api/types';

export type Errors = Partial<Record<string, string>>;

const between = (v: number, lo: number, hi: number) => Number.isFinite(v) && v >= lo && v <= hi;

export function validatePlant(p: PlantProfile): Errors {
  const e: Errors = {};
  if (!p.name.trim()) e.name = 'Give the plant a name.';
  else if (p.name.length > 40) e.name = 'Keep the name under 40 characters.';
  if (!between(p.potVolumeMl, 50, 100000)) e.potVolumeMl = 'Between 50 ml and 100 L.';
  if (!between(p.pump.flowMlPerS, 0.05, 100)) e.flow = 'Between 0.05 and 100 ml/s.';
  if (!between(p.bottle.capacityMl, 100, 20000)) e.capacityMl = 'Between 100 ml and 20 L.';
  if (!between(p.bottle.lowPct, 0, 90)) e.lowPct = 'Between 0 and 90%.';
  if (!(p.bottle.reserveMl >= 0 && p.bottle.reserveMl < p.bottle.capacityMl / 2))
    e.reserveMl = 'Less than half the bottle.';
  const m = p.moisture;
  if (![m.critical, m.min, m.target, m.max].every((v) => between(v, 0, 100))) {
    e.moisture = 'Moisture values are percentages from 0 to 100.';
  } else if (!(m.critical < m.min && m.min < m.target && m.target < m.max)) {
    e.moisture =
      'Each value must be higher than the one before: very dry < start watering < target < too wet.';
  }
  if (!(p.lightLux.min >= 0 && p.lightLux.min < p.lightLux.max && p.lightLux.max <= 200000))
    e.light = 'Minimum must be below maximum.';
  if (!(p.tempC.min >= -10 && p.tempC.min < p.tempC.max && p.tempC.max <= 50))
    e.temp = 'Minimum must be below maximum (−10 to 50 °C).';
  if (!(p.humidityPct.min >= 0 && p.humidityPct.min < p.humidityPct.max && p.humidityPct.max <= 100)) {
    e.humidity = 'Minimum must be below maximum (0–100%).';
  }
  if (!between(p.minIntervalH, 0.5, 336)) e.minIntervalH = 'Between half an hour and 14 days.';
  if (!between(p.maxPulseMl, 5, 2000)) e.maxPulseMl = 'Between 5 and 2000 ml.';
  if (!between(p.maxDailyMl, 5, 20000)) e.maxDailyMl = 'Between 5 ml and 20 L.';
  else if (p.maxDailyMl < p.maxPulseMl) e.maxDailyMl = 'Should be at least one pulse.';
  if (!between(p.soakMin, 2, 60)) e.soakMin = 'Between 2 and 60 minutes.';
  if (!between(p.dormancyFactor, 0.1, 1)) e.dormancyFactor = 'Between 10% and 100%.';
  if (!between(p.fallbackEveryDays, 0.5, 60)) e.fallbackEveryDays = 'Between half a day and 60 days.';
  if (!between(p.fallbackMl, 5, 2000)) e.fallbackMl = 'Between 5 and 2000 ml.';
  if (p.careNotes.length > 1000) e.careNotes = 'Keep notes under 1000 characters.';
  return e;
}
