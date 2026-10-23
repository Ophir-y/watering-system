import { describe, expect, it } from 'vitest';

import { MockDevice } from './device';

const TOKEN = 'a3f1c0de5eed0000a3f1c0de5eed0000';

describe('mock device', () => {
  it('requires a token for everything except info and login', async () => {
    const d = new MockDevice({ autoTick: false });
    expect((await d.request('GET', '/api/info', undefined, null)).status).toBe(200);
    expect((await d.request('GET', '/api/status', undefined, null)).status).toBe(401);
    expect((await d.request('POST', '/api/login', { password: 'nope' }, null)).status).toBe(401);
    const ok = await d.request('POST', '/api/login', { password: 'plantcare' }, null);
    expect(ok.body).toEqual({ token: TOKEN });
  });

  it('waters a dry plant in pulses and logs a session', () => {
    const d = new MockDevice({ autoTick: false });
    const p = d.plants[1];
    const lv = d.live.get(p.id)!;
    lv.moisture = p.moisture.min - 5;
    let sessions = 0;
    d.stream(TOKEN, (event, data) => {
      if (event === 'event' && (data as { code: string }).code === 'auto') sessions++;
    });
    for (let i = 0; i < 400 && sessions === 0; i++) d.tick(1000);
    expect(sessions).toBe(1);
    expect(lv.moisture).toBeGreaterThan(p.moisture.target - 3);
  });

  it('queues Water now for the next check-in and pours it then', async () => {
    const d = new MockDevice({ autoTick: false });
    const p = d.plants[0];
    const lv = d.live.get(p.id)!;
    const r = await d.request('POST', `/api/plants/${p.id}/water`, { ml: 80 }, TOKEN);
    expect(r.status).toBe(202);
    expect((r.body as { queued: boolean }).queued).toBe(true);
    expect(d.status().plants[0].queued).toHaveLength(1);
    const before = lv.bottleMl;
    for (let i = 0; i < 20 && d.status().plants[0].queued.length; i++) d.tick(1000);
    expect(d.status().plants[0].queued).toHaveLength(0);
    for (let i = 0; i < 60 && lv.dose; i++) d.tick(1000);
    expect(before - lv.bottleMl).toBeCloseTo(80, 0);
  });

  it('adds a waiting node as a plant', async () => {
    const d = new MockDevice({ autoTick: false });
    const node = d.status().newNodes[0].id;
    const r = await d.request('POST', '/api/plants', { name: 'Calathea', node }, TOKEN);
    expect(r.status).toBe(201);
    const s = d.status();
    expect(s.newNodes).toHaveLength(0);
    expect(s.plants.find((p) => p.node === node)?.configPending).toBe(true);
    const dup = await d.request('POST', '/api/plants', { name: 'Again', node }, TOKEN);
    expect(dup.status).toBe(400);
  });

  it('marks the bottle empty when the pump runs dry, until refilled', () => {
    const d = new MockDevice({ autoTick: false });
    const p = d.plants[1];
    const lv = d.live.get(p.id)!;
    lv.bottleMl = 80;
    lv.moisture = p.moisture.min - 5;
    for (let i = 0; i < 200 && !lv.bottleEmpty; i++) d.tick(1000);
    expect(d.status().plants[1].bottle?.empty).toBe(true);
    expect(d.status().plants[1].bottle?.emptyReason).toBe('pump_dry');
    d.pressButton(p.id);
    expect(d.status().plants[1].bottle?.empty).toBe(false);
    expect(d.status().plants[1].buttonPressed).toBe(true);
  });

  it('refuses manual watering while stopped', async () => {
    const d = new MockDevice({ autoTick: false });
    await d.request('POST', '/api/system/stop', {}, TOKEN);
    const r = await d.request('POST', `/api/plants/${d.plants[0].id}/water`, { ml: 50 }, TOKEN);
    expect(r.status).toBe(409);
    expect((r.body as { error: string }).error).toBe('emergency_stop');
  });

  it('serves history inside the requested window', async () => {
    const d = new MockDevice({ autoTick: false });
    const to = Math.floor(Date.now() / 1000);
    const r = await d.request(
      'GET',
      `/api/history?plant=${d.plants[0].id}&from=${to - 7 * 86400}&to=${to}&points=100`,
      undefined,
      TOKEN,
    );
    const pts = (r.body as { points: [number, number][] }).points;
    expect(pts.length).toBeGreaterThan(90);
    expect(pts.every(([t]) => t >= to - 7 * 86400 && t <= to)).toBe(true);
  });
});
