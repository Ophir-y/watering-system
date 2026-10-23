import { describe, expect, it } from 'vitest';

import type { EnvHistory, PlantProfile, PlantStatus } from '../api/types';
import { SPECIES, applySpecies, defaultProfile, limitsForPot } from '../data/species';
import { TIMEZONES, zoneFor } from '../data/timezones';
import { plantCondition } from './condition';
import { formatAgo, formatLux, formatMl, formatMonths, formatPct } from './format';
import { envStats, recommendations, wateringStats } from './recommendations';
import { validatePlant } from './validate';

const profile = (over: Partial<PlantProfile> = {}): PlantProfile => ({
  ...defaultProfile('a0b1c2d3e4f5'),
  id: 'fern',
  name: 'Fern',
  ...over,
});

const status = (over: Partial<PlantStatus> = {}): PlantStatus => ({
  id: 'fern',
  name: 'Fern',
  node: 'a0b1c2d3e4f5',
  online: true,
  lastSeenAt: 1000,
  nextCheckInS: 120,
  firmware: '0.2.0',
  battery: { mv: 4300, pct: 74, low: false },
  linkPct: 100,
  moisturePct: 50,
  raw: 15000,
  sensor: 'ok',
  issue: 'ok',
  state: 'idle',
  triggerPct: 35,
  targetPct: 55,
  mlToday: 0,
  lastWateredAt: null,
  lockedOut: false,
  mlPerPct: null,
  soakRemainingS: 0,
  bottle: {
    ml: 900,
    pct: 60,
    capacityMl: 1500,
    daysLeft: 6,
    low: false,
    empty: false,
    emptyReason: 'none',
    flowFactor: 1,
  },
  pump: { learned: true, lastRun: 'ok' },
  dosing: null,
  queued: [],
  configPending: false,
  repairRequested: false,
  buttonPressed: false,
  ...over,
});

describe('format', () => {
  it('formats volumes, percentages and light', () => {
    expect(formatMl(240)).toBe('240 ml');
    expect(formatMl(1250)).toBe('1.3 L');
    expect(formatMl(12500)).toBe('13 L');
    expect(formatPct(null)).toBe('–');
    expect(formatPct(42.4)).toBe('42%');
    expect(formatLux(850)).toBe('850 lx');
    expect(formatLux(5400)).toBe('5.4k lx');
    expect(formatLux(25000)).toBe('25k lx');
  });

  it('formats relative times', () => {
    expect(formatAgo(null, 1000)).toBe('never');
    expect(formatAgo(1000, 1030)).toBe('just now');
    expect(formatAgo(1000, 1000 + 3 * 3600)).toBe('3 h ago');
    expect(formatAgo(0, 5)).toBe('never');
  });

  it('lists months in calendar order', () => {
    expect(formatMonths([12, 1, 2])).toBe('Jan, Feb, Dec');
    expect(formatMonths([])).toBe('none');
  });
});

describe('plant condition', () => {
  it('starts with the node and the bottle', () => {
    expect(plantCondition(status({ node: null })).label).toBe('No node');
    expect(plantCondition(status({ online: false, lastSeenAt: null, bottle: null })).label).toBe(
      'Waiting for its node',
    );
    expect(plantCondition(status({ online: false })).label).toBe('Node offline');
    const empty = status().bottle!;
    expect(plantCondition(status({ bottle: { ...empty, empty: true, ml: 0 }, lockedOut: true })).label).toBe(
      'Bottle empty',
    );
    expect(plantCondition(status({ bottle: { ...empty, low: true } })).label).toBe('Refill soon');
    expect(plantCondition(status({ battery: { mv: 3500, pct: 15, low: true } })).label).toBe('Batteries low');
    expect(plantCondition(status({ queued: [{ id: 1, kind: 'water', arg: 50 }] })).label).toBe(
      'Water queued',
    );
    expect(plantCondition(status({ repairRequested: true })).tone).toBe('serious');
  });

  it('puts faults before moisture', () => {
    expect(plantCondition(status({ lockedOut: true, moisturePct: 10 })).label).toBe('Pump locked');
    expect(plantCondition(status({ sensor: 'failed', issue: 'disconnected' })).tone).toBe('serious');
    expect(plantCondition(status({ sensor: 'failed', issue: 'uncalibrated' })).label).toBe(
      'Needs calibration',
    );
  });

  it('describes watering and moisture states', () => {
    const p = profile();
    expect(plantCondition(status({ state: 'watering' }), p).tone).toBe('water');
    expect(plantCondition(status({ state: 'soaking', soakRemainingS: 300 }), p).label).toBe(
      'Soaking · 5 min',
    );
    expect(plantCondition(status({ moisturePct: 10 }), p).label).toBe('Very dry');
    expect(plantCondition(status({ moisturePct: 30 }), p).label).toBe('Due for water');
    expect(plantCondition(status({ moisturePct: 80 }), p).label).toBe('Too wet');
    expect(plantCondition(status({ moisturePct: 50 }), p).label).toBe('In range');
    expect(plantCondition(status({ moisturePct: null }), p).tone).toBe('neutral');
  });
});

describe('recommendations', () => {
  const env = (lux: number, rh: number, tMin: number): EnvHistory => {
    const from = 0;
    const points: EnvHistory['points'] = [];
    for (let h = 0; h < 72; h++)
      points.push([h * 3600, tMin + (h % 24 > 12 ? 4 : 0), rh, h % 24 === 13 ? lux : 0]);
    return { from, to: 72 * 3600, points };
  };
  const base = { month: 6, southernHemisphere: false };

  it('flags low light, dry air and cold', () => {
    const p = profile({
      lightLux: { min: 5000, max: 20000 },
      humidityPct: { min: 60, max: 80 },
      tempC: { min: 18, max: 30 },
    });
    const ids = recommendations({ ...base, profile: p, env: envStats(env(1200, 35, 14), 0) }).map(
      (r) => r.id,
    );
    expect(ids).toEqual(expect.arrayContaining(['light-low', 'humidity-low', 'temp-low']));
  });

  it('stays quiet when conditions match', () => {
    const p = profile({
      lightLux: { min: 1000, max: 20000 },
      humidityPct: { min: 40, max: 70 },
      tempC: { min: 15, max: 30 },
    });
    expect(recommendations({ ...base, profile: p, env: envStats(env(8000, 55, 20), 0) })).toEqual([]);
  });

  it('notices frequent watering, wet soil, resting season and a low bottle', () => {
    const p = profile({ dormantMonths: [6] });
    const events = Array.from({ length: 14 }, (_, i) => ({
      ts: i * 43200,
      type: 'watering' as const,
      plantId: 'fern',
      code: 'auto',
      ml: 100,
      detail: '',
    }));
    const recs = recommendations({
      ...base,
      profile: p,
      status: status({ moisturePct: 82 }),
      watering: wateringStats(events, 'fern', 0, 7 * 86400),
      bottleDaysLeft: 1.5,
    });
    const ids = recs.map((r) => r.id);
    expect(ids).toEqual(expect.arrayContaining(['watering-frequent', 'too-wet', 'dormant', 'refill']));
    expect(recs[0].level).toBe('attention');
  });

  it('maps southern-hemisphere months for dormancy', () => {
    const p = profile({ dormantMonths: [1] });
    expect(recommendations({ profile: p, month: 7, southernHemisphere: true }).map((r) => r.id)).toContain(
      'dormant',
    );
    expect(
      recommendations({ profile: p, month: 1, southernHemisphere: true }).map((r) => r.id),
    ).not.toContain('dormant');
  });
});

describe('validation and species', () => {
  it('accepts the default profile once named', () => {
    expect(validatePlant(profile())).toEqual({});
    expect(validatePlant(profile({ name: ' ' })).name).toBeDefined();
  });

  it('rejects a disordered moisture band', () => {
    expect(
      validatePlant(profile({ moisture: { critical: 20, min: 50, target: 40, max: 70 } })).moisture,
    ).toBeDefined();
  });

  it('every species produces a valid profile for a range of pots', () => {
    for (const s of SPECIES) {
      for (const pot of [300, 2000, 12000]) {
        const p = applySpecies({ ...profile({ potVolumeMl: pot }), ...limitsForPot(pot) }, s);
        expect({ species: s.id, pot, errors: validatePlant(p) }).toEqual({ species: s.id, pot, errors: {} });
      }
    }
  });

  it('has unique species ids', () => {
    expect(new Set(SPECIES.map((s) => s.id)).size).toBe(SPECIES.length);
  });
});

describe('time zones', () => {
  it('maps known zones and falls back to a fixed offset', () => {
    expect(zoneFor('Europe/Berlin').timezone).toBe(TIMEZONES['Europe/Berlin']);
    expect(zoneFor('Australia/Sydney').southernHemisphere).toBe(true);
    const winter = new Date('2026-01-15T12:00:00Z');
    const z = zoneFor('Mars/Olympus', winter);
    expect(z.exact).toBe(false);
    expect(z.timezone).toMatch(/^<[+-]\d{2,4}>-?\d+(:\d{2})?$/);
  });
});
