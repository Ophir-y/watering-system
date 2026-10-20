// Rule-based care recommendations: compares a plant's profile with what the
// sensors have seen recently. Runs in the browser so the firmware stays small.
import type { DeviceEvent, EnvHistory, PlantProfile, PlantStatus } from '../api/types';
import { formatLux, formatMl } from './format';

export interface EnvStats {
  days: number;
  tempMin: number | null;
  tempMax: number | null;
  humidityAvg: number | null;
  luxPeak: number | null; // median of daily maxima
}

export interface WateringStats {
  days: number;
  sessions: number;
  ml: number;
}

export interface Recommendation {
  id: string;
  level: 'attention' | 'tip';
  title: string;
  body: string;
}

export interface RecommendationInput {
  profile: PlantProfile;
  status?: PlantStatus;
  env?: EnvStats | null;
  watering?: WateringStats | null;
  month: number; // 1..12, local
  southernHemisphere: boolean;
  reservoirDaysLeft?: number | null;
}

const median = (xs: number[]) => {
  if (!xs.length) return null;
  const s = xs.slice().sort((a, b) => a - b);
  const m = Math.floor(s.length / 2);
  return s.length % 2 ? s[m] : (s[m - 1] + s[m]) / 2;
};

export function envStats(h: EnvHistory, tzOffsetMin = new Date().getTimezoneOffset()): EnvStats | null {
  if (!h.points.length) return null;
  const temps: number[] = [];
  const hums: number[] = [];
  const dailyMax = new Map<number, number>();
  for (const [ts, t, rh, lux] of h.points) {
    if (t !== null) temps.push(t);
    if (rh !== null) hums.push(rh);
    if (lux !== null) {
      const day = Math.floor((ts - tzOffsetMin * 60) / 86400);
      dailyMax.set(day, Math.max(dailyMax.get(day) ?? 0, lux));
    }
  }
  return {
    days: (h.to - h.from) / 86400,
    tempMin: temps.length ? Math.min(...temps) : null,
    tempMax: temps.length ? Math.max(...temps) : null,
    humidityAvg: hums.length ? hums.reduce((a, b) => a + b, 0) / hums.length : null,
    luxPeak: median([...dailyMax.values()]),
  };
}

export function wateringStats(
  events: DeviceEvent[],
  plantId: string,
  from: number,
  to: number,
): WateringStats {
  let sessions = 0;
  let ml = 0;
  for (const e of events) {
    if (e.type !== 'watering' || e.plantId !== plantId || e.ts < from || e.ts > to || e.ml <= 0) continue;
    sessions++;
    ml += e.ml;
  }
  return { days: (to - from) / 86400, sessions, ml };
}

function isDormant(p: PlantProfile, month: number, southern: boolean) {
  const m = southern ? ((month + 5) % 12) + 1 : month;
  return p.dormantMonths.includes(m);
}

export function recommendations(input: RecommendationInput): Recommendation[] {
  const { profile: p, status: s, env, watering } = input;
  const out: Recommendation[] = [];
  const name = p.name;

  if (s?.issue === 'uncalibrated') {
    out.push({
      id: 'calibrate',
      level: 'attention',
      title: 'Calibrate the moisture sensor',
      body: `Automatic watering for ${name} starts once the sensor knows its dry and wet points. Use the sensor section below.`,
    });
  }

  if (env?.luxPeak !== null && env?.luxPeak !== undefined && env.days >= 2) {
    if (env.luxPeak < p.lightLux.min * 0.8) {
      out.push({
        id: 'light-low',
        level: 'attention',
        title: 'Probably too dark',
        body: `Light at the sensor peaks around ${formatLux(env.luxPeak)} a day; ${name} prefers ${formatLux(p.lightLux.min)}–${formatLux(p.lightLux.max)}. If it stands near the sensor, move it closer to a window or add a grow light.`,
      });
    } else if (env.luxPeak > p.lightLux.max * 2) {
      out.push({
        id: 'light-high',
        level: 'tip',
        title: 'Strong light',
        body: `Light at the sensor reaches ${formatLux(env.luxPeak)}, well above the ${formatLux(p.lightLux.max)} ${name} is used to. Watch for bleached or scorched leaves.`,
      });
    }
  }

  if (
    env?.humidityAvg !== null &&
    env?.humidityAvg !== undefined &&
    env.humidityAvg < p.humidityPct.min - 5
  ) {
    out.push({
      id: 'humidity-low',
      level: p.humidityPct.min >= 60 ? 'attention' : 'tip',
      title: 'Air is drier than it likes',
      body: `Humidity averages ${Math.round(env.humidityAvg)}%, below the ${p.humidityPct.min}–${p.humidityPct.max}% ${name} prefers. Group plants together, use a pebble tray, or run a humidifier.`,
    });
  }

  if (env?.tempMin !== null && env?.tempMin !== undefined && env.tempMin < p.tempC.min) {
    out.push({
      id: 'temp-low',
      level: env.tempMin < p.tempC.min - 3 ? 'attention' : 'tip',
      title: 'Cold spells',
      body: `It dropped to ${env.tempMin.toFixed(1)} °C; ${name} prefers at least ${p.tempC.min} °C. Keep it away from cold windows and draughts. Watering is reduced automatically while it is cold.`,
    });
  }
  if (env?.tempMax !== null && env?.tempMax !== undefined && env.tempMax > p.tempC.max) {
    out.push({
      id: 'temp-high',
      level: 'tip',
      title: 'Hot spells',
      body: `It reached ${env.tempMax.toFixed(1)} °C, above the ${p.tempC.max} °C ${name} is comfortable with. Heat dries the soil faster; it will be watered a little sooner.`,
    });
  }

  if (watering && watering.days >= 3) {
    const perDay = watering.sessions / watering.days;
    const weeklyMl = (watering.ml / watering.days) * 7;
    if (perDay > 1.5) {
      out.push({
        id: 'watering-frequent',
        level: 'attention',
        title: 'Watered unusually often',
        body: `${watering.sessions} waterings in ${Math.round(watering.days)} days. The soil may drain too fast, the pot may be too small, or the sensor may sit where water runs straight past it.`,
      });
    } else if (weeklyMl > p.potVolumeMl) {
      out.push({
        id: 'watering-heavy',
        level: 'tip',
        title: 'Uses a lot of water',
        body: `About ${formatMl(weeklyMl)} a week, more than the pot holds. Check that the saucer is not overflowing; it may be time to repot.`,
      });
    }
  }

  if (s && s.moisturePct !== null && s.moisturePct > p.moisture.max) {
    out.push({
      id: 'too-wet',
      level: 'attention',
      title: 'Soil is wetter than it should be',
      body: `Moisture is ${Math.round(s.moisturePct)}%, above the ${p.moisture.max}% limit. Make sure the pot drains freely; automatic watering waits until it dries below ${Math.round(s.triggerPct)}%.`,
    });
  }

  if (isDormant(p, input.month, input.southernHemisphere)) {
    out.push({
      id: 'dormant',
      level: 'tip',
      title: 'Resting season',
      body: `${name} grows slowly this time of year, so doses are scaled to ${Math.round(p.dormancyFactor * 100)}% and it is watered a little later.`,
    });
  }

  if (
    input.reservoirDaysLeft !== null &&
    input.reservoirDaysLeft !== undefined &&
    input.reservoirDaysLeft < 3
  ) {
    out.push({
      id: 'refill',
      level: 'attention',
      title: 'Refill the reservoir soon',
      body: `At the current rate it runs out in about ${input.reservoirDaysLeft.toFixed(1)} days.`,
    });
  }

  return out.sort((a, b) => (a.level === b.level ? 0 : a.level === 'attention' ? -1 : 1));
}
