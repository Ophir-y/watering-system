// The dashboard's view of the API must match what the firmware sends. The
// firmware's native tests check its JSON against the same fixtures, so between the
// two, firmware, fixtures, TypeScript types and the mock device cannot drift apart.
import { describe, expect, it } from 'vitest';

import alertsFixture from '../../../shared/api-fixtures/alerts.json';
import envFixture from '../../../shared/api-fixtures/env-history.json';
import eventsFixture from '../../../shared/api-fixtures/events.json';
import historyFixture from '../../../shared/api-fixtures/history.json';
import plantFixture from '../../../shared/api-fixtures/plant.json';
import settingsFixture from '../../../shared/api-fixtures/settings.json';
import statusFixture from '../../../shared/api-fixtures/status.json';
import { MockDevice } from '../mock/device';
import type { Alert, DeviceEvent, EnvHistory, History, PlantProfile, Settings, Status } from './types';

// Maps every JSON path to the set of value types seen there (array items share "[]").
function shape(v: unknown, path = '$', out = new Map<string, Set<string>>()) {
  const t = v === null ? 'null' : Array.isArray(v) ? 'array' : typeof v;
  if (!out.has(path)) out.set(path, new Set());
  out.get(path)!.add(t);
  if (Array.isArray(v)) v.forEach((x) => shape(x, `${path}[]`, out));
  else if (v && typeof v === 'object') for (const [k, x] of Object.entries(v)) shape(x, `${path}.${k}`, out);
  return out;
}

function expectSameShape(fixture: unknown, sample: unknown) {
  const a = shape(fixture);
  const b = shape(sample);
  expect([...b.keys()].sort()).toEqual([...a.keys()].sort());
  for (const [path, types] of a) {
    const ta = [...types].filter((t) => t !== 'null');
    const tb = [...(b.get(path) ?? [])].filter((t) => t !== 'null');
    if (ta.length && tb.length) expect({ path, types: tb.sort() }).toEqual({ path, types: ta.sort() });
  }
}

// A plant pouring, a command waiting for its node, a plant without a node and a
// node waiting to be added: every part of the status document is present.
function busyDevice() {
  const d = new MockDevice({ autoTick: false });
  const lv = d.live.get(d.plants[0].id)!;
  lv.dose = { ml: 60, dispensed: 22.5, kind: 'auto' };
  lv.queue.push({ id: 1, kind: 'water', arg: 80 });
  d.plants.push({ ...d.plants[1], id: 'pothos', name: 'Pothos', node: null });
  return d;
}

describe('API contract (shared/api-fixtures)', () => {
  it('status', () => {
    const sample: Status = busyDevice().status();
    expectSameShape(statusFixture, sample);
  });

  it('plant profile', () => {
    const sample: PlantProfile = new MockDevice({ autoTick: false }).plants[0];
    expectSameShape(plantFixture, sample);
  });

  it('settings', () => {
    const sample: Settings = new MockDevice({ autoTick: false }).settings;
    expectSameShape(settingsFixture, sample);
  });

  it('alerts', () => {
    const d = new MockDevice({ autoTick: false });
    const sample: { alerts: Alert[] } = { alerts: d.alerts };
    expectSameShape(alertsFixture, sample);
  });

  it('events', () => {
    const d = new MockDevice({ autoTick: false });
    const sample: { events: DeviceEvent[] } = {
      events: [
        d.events.find((e) => e.plantId)!,
        { ...d.events.find((e) => e.type === 'refill')! },
        { ...d.events.find((e) => e.plantId)!, plantId: null },
      ],
    };
    expectSameShape(eventsFixture, sample);
  });

  it('history and environment history', () => {
    const d = new MockDevice({ autoTick: false });
    const now = Math.floor(Date.now() / 1000);
    const h: History = d.history(d.plants[0].id, now - 86400, now, 50);
    expectSameShape(historyFixture, h);
    const e: EnvHistory = d.envHistory(now - 86400, now, 50);
    e.points[1] = [e.points[1][0], e.points[1][1], null, null];
    expectSameShape(envFixture, e);
  });

  it('every enum value in the fixtures is known to the types', () => {
    const s = statusFixture as unknown as Status;
    const sensors: Status['plants'][number]['sensor'][] = ['ok', 'suspect', 'failed', 'unknown'];
    const states: Status['plants'][number]['state'][] = ['idle', 'waiting', 'watering', 'soaking', 'unknown'];
    const reasons: string[] = ['none', 'gauge', 'pump_dry', 'no_response'];
    const runs: string[] = ['ok', 'dry', 'disconnected', 'jammed'];
    for (const p of s.plants) {
      expect(sensors).toContain(p.sensor);
      expect(states).toContain(p.state);
      if (p.bottle) expect(reasons).toContain(p.bottle.emptyReason);
      expect(runs).toContain(p.pump.lastRun);
      for (const c of p.queued)
        expect(['water', 'timed_run', 'refilled', 'clear_faults', 'identify', 'forget']).toContain(c.kind);
    }
    expect(['sta', 'ap', 'sta+ap', 'off']).toContain(s.network.mode);
    expect((plantFixture as PlantProfile).node).toMatch(/^[0-9a-f]{12}$/);
  });
});
