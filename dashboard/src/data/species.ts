import type { MoistureBand, PlantProfile, Range, WateringStyle } from '../api/types';
import raw from './species.json';

export interface Species {
  id: string;
  common: string;
  latin: string;
  group: string;
  light: string;
  lightLux: Range;
  tempC: Range;
  humidityPct: Range;
  soil: string;
  style: WateringStyle;
  moisture: MoistureBand;
  minIntervalH: number;
  dormantMonths: number[];
  dormancyFactor: number;
  petSafe: boolean | null;
  notes: string;
}

export const SPECIES: Species[] = (raw as Species[]).slice().sort((a, b) => a.common.localeCompare(b.common));

export function findSpecies(id: string): Species | undefined {
  return SPECIES.find((s) => s.id === id);
}

export const STYLE_LABEL: Record<WateringStyle, string> = {
  evenly_moist: 'Keep evenly moist',
  dry_between: 'Let the top dry out',
  soak_and_dry: 'Soak, then dry out fully',
};

const clamp = (v: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, v));
const round5 = (v: number) => Math.round(v / 5) * 5;

// Dose limits that scale with the pot: pulses of ~6 % of its volume, at most ~30 % a day.
export function limitsForPot(potVolumeMl: number) {
  return {
    maxPulseMl: round5(clamp(potVolumeMl * 0.06, 20, 250)),
    maxDailyMl: round5(clamp(potVolumeMl * 0.3, 60, 1500)),
    soakMin: potVolumeMl > 4000 ? 15 : 10,
    fallbackMl: round5(clamp(potVolumeMl * 0.08, 20, 400)),
  };
}

// A new plant, watered by `node` (a plant node waiting to be added).
export function defaultProfile(node: string | null = null): PlantProfile {
  return {
    id: '',
    name: '',
    species: '',
    enabled: true,
    autoWater: true,
    node,
    pump: { flowMlPerS: 1.5, currentSense: true },
    bottle: { capacityMl: 1500, lowPct: 25, reserveMl: 50 },
    potVolumeMl: 2000,
    soil: '',
    moisture: { critical: 20, min: 35, target: 55, max: 75 },
    lightLux: { min: 2000, max: 20000 },
    tempC: { min: 16, max: 29 },
    humidityPct: { min: 40, max: 70 },
    style: 'evenly_moist',
    minIntervalH: 8,
    dormantMonths: [],
    dormancyFactor: 0.7,
    onSensorFault: 'pause',
    fallbackEveryDays: 4,
    calibration: { rawDry: 19200, rawWet: 9600 },
    careNotes: '',
    ...limitsForPot(2000),
  };
}

// Copies a species' care values into a profile, keeping hardware and identity fields.
export function applySpecies(p: PlantProfile, s: Species): PlantProfile {
  return {
    ...p,
    species: s.id,
    name: p.name || s.common,
    soil: s.soil,
    moisture: { ...s.moisture },
    lightLux: { ...s.lightLux },
    tempC: { ...s.tempC },
    humidityPct: { ...s.humidityPct },
    style: s.style,
    minIntervalH: s.minIntervalH,
    dormantMonths: [...s.dormantMonths],
    dormancyFactor: s.dormancyFactor,
    careNotes: s.notes,
    autoWater: s.id === 'phalaenopsis' ? false : p.autoWater,
  };
}
