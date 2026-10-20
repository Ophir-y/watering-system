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
