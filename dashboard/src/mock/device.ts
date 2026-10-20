// A simulated PlantCare device implementing the same HTTP API as the firmware.
// History is synthesized from deterministic curves; the present is simulated live
// (plants dry out 120x faster than real life so a full cycle takes minutes).
import type { StreamHandler, Transport, TransportResponse } from '../api/transport';
import type {
  Alert,
  DeviceEvent,
  EnvHistory,
  History,
  PlannerState,
  PlantProfile,
  PlantStatus,
  SensorHealth,
  SensorIssue,
  Settings,
  Status,
} from '../api/types';
import { applySpecies, defaultProfile, findSpecies, limitsForPot } from '../data/species';

const SPEED = 120; // simulated seconds per real second for drying and soaking
const TOKEN = 'a3f1c0de5eed0000a3f1c0de5eed0000';
const nowS = () => Math.floor(Date.now() / 1000);

interface Live {
  moisture: number;
  mlToday: number;
  lastWateredAt: number | null;
  state: PlannerState;
  soakUntil: number;
  pulses: number;
  sessionMl: number;
  lockedOut: boolean;
  mlPerPct: number | null;
  sensor: SensorHealth;
  issue: SensorIssue;
  dryPerHour: number;
  periodH: number;
  phase: number;
}

interface Dose {
  plantId: string;
  ml: number;
  dispensed: number;
  kind: 'auto' | 'manual' | 'fallback';
}

const DEMO: { species: string; name: string; pot: number; periodH: number; issue?: SensorIssue }[] = [
  { species: 'monstera_deliciosa', name: 'Monstera', pot: 6000, periodH: 70 },
  { species: 'nephrolepis_exaltata', name: 'Boston fern', pot: 2500, periodH: 28 },
  { species: 'dracaena_trifasciata', name: 'Snake plant', pot: 3000, periodH: 260 },
  { species: 'ocimum_basilicum', name: 'Kitchen basil', pot: 1200, periodH: 20 },
  { species: 'curio_rowleyanus', name: 'String of pearls', pot: 800, periodH: 160, issue: 'stuck' },
];

function defaultSettings(): Settings {
  return {
    deviceName: 'PlantCare',
    timezone: 'UTC0',
    tzName: 'UTC',
    southernHemisphere: false,
    waterWindow: { startH: 7, endH: 21 },
    quietHours: { startH: 22, endH: 8 },
    sampleIntervalS: 60,
    logIntervalMin: 10,
    automationEnabled: true,
    buzzerEnabled: true,
    leakSensorEnabled: true,
    limits: {
      hardMaxPulseMl: 250,
      hardMaxDailyMlPerPlant: 1500,
      hardMaxDailyMlTotal: 5000,
      hardMaxOnTimeS: 120,
    },
    reservoir: {
      scaleEnabled: true,
      floatSwitchEnabled: true,
      capacityMl: 5000,
      fullHeightMm: 250,
      lowPct: 20,
      emptyMl: 150,
      lowDaysLeft: 2,
    },
    notify: { enabled: false, server: 'https://ntfy.sh', topic: '', tokenSet: false, minSeverity: 'warning' },
  };
}

const ok = (body: unknown = { ok: true }, status = 200): TransportResponse => ({ status, body });
const fail = (status: number, error: string, message = error): TransportResponse => ({
  status,
  body: { error, message },
});

function deepMerge<T>(base: T, patch: unknown): T {
  if (!patch || typeof patch !== 'object' || Array.isArray(patch)) return (patch ?? base) as T;
  const out: Record<string, unknown> = { ...(base as Record<string, unknown>) };
  for (const [k, v] of Object.entries(patch as Record<string, unknown>)) {
    const cur = out[k];
    out[k] =
      v && typeof v === 'object' && !Array.isArray(v) && cur && typeof cur === 'object'
        ? deepMerge(cur, v)
        : v;
  }
  return out as T;
}

export class MockDevice implements Transport {
  settings = defaultSettings();
  plants: PlantProfile[] = [];
  live = new Map<string, Live>();
  alerts: Alert[] = [];
  events: DeviceEvent[] = [];
  reservoirMl = 0;
  emergencyStop = false;
  setupRequired: boolean;
  password = 'plantcare';
  dose: Dose | null = null;
  lastDose: Status['lastDose'] = null;
  private nextAlertId = 1;
  private timer: ReturnType<typeof setInterval> | null = null;
  private listeners = new Set<StreamHandler>();
  private lastTick = Date.now();
  private simMs = 0; // simulated clock for soaking, advanced by tick()
  private bootS = nowS() - 3 * 86400 - 4200;

  constructor(opts: { setupRequired?: boolean; autoTick?: boolean } = {}) {
    this.setupRequired = opts.setupRequired ?? false;
    this.reservoirMl = this.syntheticReservoir(nowS());
    DEMO.forEach((d, i) => {
      const s = findSpecies(d.species)!;
      const p = applySpecies({ ...defaultProfile(i), ...limitsForPot(d.pot), potVolumeMl: d.pot }, s);
      p.id = d.name.toLowerCase().replace(/[^a-z0-9]+/g, '-');
      p.name = d.name;
      p.outlet.flowMlPerS = 2.2;
      this.plants.push(p);
      const phase = (i * 0.37) % 1;
      this.live.set(p.id, {
        moisture: 0,
        mlToday: Math.round(40 + i * 25),
        lastWateredAt: null,
        state: 'idle',
        soakUntil: 0,
        pulses: 0,
        sessionMl: 0,
        lockedOut: false,
        mlPerPct: Math.round((p.potVolumeMl / 120) * 10) / 10,
        sensor: d.issue ? 'suspect' : 'ok',
        issue: d.issue ?? 'ok',
        dryPerHour: (p.moisture.target - p.moisture.min) / d.periodH,
        periodH: d.periodH,
        phase,
      });
      const lv = this.live.get(p.id)!;
      lv.moisture = this.synthetic(p, lv, nowS());
      lv.lastWateredAt = this.lastSyntheticWatering(lv, nowS());
    });
    this.seedEvents();
    this.raise(
      'warning',
      'sensor_fault',
      4,
      'String of pearls: moisture sensor reading has not changed for 12 hours.',
      nowS() - 5400,
    );
    this.raise(
      'info',
      'humidity_out_of_range',
      1,
      'Boston fern prefers 60-90 % humidity; it has been 47 % for over 2 hours.',
      nowS() - 9000,
    );
    if (opts.autoTick !== false) this.timer = setInterval(() => this.tick(), 1000);
  }

  dispose() {
    if (this.timer) clearInterval(this.timer);
  }

  // ---------------------------------------------------------------- simulation

  private synthetic(p: PlantProfile, lv: Live, t: number): number {
    const frac = (t / 3600 / lv.periodH + lv.phase) % 1;
    const wobble = Math.sin(t / 5400) * 0.6;
    return p.moisture.target + 2 - (p.moisture.target + 2 - (p.moisture.min - 1)) * frac + wobble;
  }

  private lastSyntheticWatering(lv: Live, t: number) {
    const frac = (t / 3600 / lv.periodH + lv.phase) % 1;
    return Math.round(t - frac * lv.periodH * 3600);
  }

  private env(t: number) {
    const hour = (((t / 3600) % 24) + 24) % 24;
    const day = Math.sin(((hour - 9) / 24) * 2 * Math.PI);
    const light = hour > 6 && hour < 20 ? Math.sin(((hour - 6) / 14) * Math.PI) : 0;
    return {
      tempC: 21.5 + 2.8 * day,
      humidityPct: 51 - 7 * day,
      lux: Math.max(0, 9500 * light * (0.85 + 0.15 * Math.sin(t / 86400))),
    };
  }

  private syntheticReservoir(t: number) {
    const periodS = 9 * 86400;
    const frac = ((((t - 2 * 86400) / periodS) % 1) + 1) % 1;
    return 4800 - 3900 * frac;
  }

  private seedEvents() {
    const now = nowS();
    const from = now - 30 * 86400;
    for (const p of this.plants) {
      const lv = this.live.get(p.id)!;
      for (let t = this.lastSyntheticWatering(lv, now); t > from; t -= lv.periodH * 3600) {
        const ml = Math.round((p.moisture.target - p.moisture.min) * (lv.mlPerPct ?? 10));
        this.events.push({
          ts: t,
          type: 'watering',
          plantId: p.id,
          code: 'auto',
          ml,
          detail: `${Math.max(1, Math.round(ml / p.maxPulseMl))} pulses, ${p.moisture.min - 1}% -> ${p.moisture.target}%, target reached`,
        });
      }
    }
    for (let t = now - 2 * 86400; t > from; t -= 9 * 86400) {
      this.events.push({
        ts: t,
        type: 'refill',
        plantId: null,
        code: 'refill',
        ml: 3900,
        detail: 'Reservoir refilled (+3900 ml)',
      });
    }
    this.events.sort((a, b) => a.ts - b.ts);
  }

  private plantStatus(p: PlantProfile): PlantStatus {
    const lv = this.live.get(p.id)!;
    return {
      id: p.id,
      name: p.name,
      channel: p.channel,
      moisturePct:
        lv.sensor === 'failed' && lv.issue !== 'no_response' ? null : Math.round(lv.moisture * 10) / 10,
      raw: Math.round(
        p.calibration.rawDry + ((p.calibration.rawWet - p.calibration.rawDry) * lv.moisture) / 100,
      ),
      sensor: lv.sensor,
      issue: lv.issue,
      state: lv.state,
      triggerPct: p.moisture.min,
      targetPct:
        p.style === 'soak_and_dry' ? Math.max(p.moisture.target, p.moisture.max - 3) : p.moisture.target,
      mlToday: Math.round(lv.mlToday),
      lastWateredAt: lv.lastWateredAt,
      lockedOut: lv.lockedOut,
      mlPerPct: lv.mlPerPct,
      soakRemainingS:
        lv.state === 'soaking' ? Math.max(0, Math.round((lv.soakUntil - this.simMs) / 1000) * SPEED) : 0,
    };
  }

  status(): Status {
    const now = nowS();
    const env = this.env(now);
    const cap = this.settings.reservoir.capacityMl;
    const pct = (this.reservoirMl / cap) * 100;
    const usage = 420;
    const daysLeft = this.reservoirMl / usage;
    const counts = { critical: 0, warning: 0, info: 0 };
    for (const a of this.alerts) if (a.active) counts[a.severity]++;
    return {
      device: {
        name: this.settings.deviceName,
        version: '0.1.0-mock',
        uptimeS: now - this.bootS,
        timeValid: true,
        now,
        tzName: this.settings.tzName,
        heapFree: 148_212,
        resetReason: 'power_on',
      },
      network: {
        mode: 'sta',
        ssid: 'HomeWiFi',
        ip: '192.168.1.50',
        rssi: -58,
        apActive: false,
        apSsid: 'PlantCare-1A2B',
        internet: true,
      },
      storage: { sdOk: true, sdUsedMb: 18.4, sdTotalMb: 29819.4 },
      automation: { enabled: this.settings.automationEnabled, emergencyStop: this.emergencyStop },
      leak: false,
      reservoir: {
        scaleValid: true,
        ml: Math.round(this.reservoirMl),
        pct: Math.round(pct * 10) / 10,
        capacityMl: cap,
        daysLeft: Math.round(daysLeft * 10) / 10,
        low: pct < this.settings.reservoir.lowPct || daysLeft < this.settings.reservoir.lowDaysLeft,
        empty: this.reservoirMl < this.settings.reservoir.emptyMl,
        floatLow: false,
        scaleCalibrated: true,
        scaleRaw: 1_843_210,
      },
      environment: {
        valid: true,
        tempC: Math.round(env.tempC * 10) / 10,
        humidityPct: Math.round(env.humidityPct),
        vpdKPa:
          Math.round(
            0.6108 * Math.exp((17.27 * env.tempC) / (env.tempC + 237.3)) * (1 - env.humidityPct / 100) * 100,
          ) / 100,
        lux: Math.round(env.lux),
      },
      dose: this.dose
        ? {
            plantId: this.dose.plantId,
            channel: this.plants.find((p) => p.id === this.dose!.plantId)?.channel ?? 0,
            kind: this.dose.kind,
            ml: this.dose.ml,
            dispensedMl: Math.round(this.dose.dispensed * 10) / 10,
          }
        : null,
      lastDose: this.lastDose,
      mlTodayTotal: Math.round([...this.live.values()].reduce((a, l) => a + l.mlToday, 0)),
      plants: this.plants.map((p) => this.plantStatus(p)),
      alerts: counts,
    };
  }

  private emit(event: string, data: unknown) {
    this.listeners.forEach((fn) => fn(event, data));
  }

  private event(type: DeviceEvent['type'], plantId: string | null, code: string, ml: number, detail: string) {
    const e = { ts: nowS(), type, plantId, code, ml, detail };
    this.events.push(e);
    this.emit('event', e);
  }

  private raise(
    severity: Alert['severity'],
    code: string,
    channel: number | null,
    message: string,
    at = nowS(),
  ) {
    if (this.alerts.some((a) => a.active && a.code === code && a.channel === channel)) return;
    const plantId = channel === null ? null : (this.plants.find((p) => p.channel === channel)?.id ?? null);
    this.alerts.push({
      id: this.nextAlertId++,
      code,
      severity,
      channel,
      plantId,
      active: true,
      acked: false,
      raisedAt: at,
      clearedAt: null,
      message,
    });
  }

  private clear(code: string, channel: number | null) {
    for (const a of this.alerts) {
      if (a.active && a.code === code && a.channel === channel) {
        a.active = false;
        a.clearedAt = nowS();
      }
    }
  }

  tick(realMs = Date.now() - this.lastTick) {
    this.lastTick = Date.now();
    this.simMs += realMs;
    const simH = (realMs / 3_600_000) * SPEED;
    // Active dose: 2.2 ml/s, real time.
    if (this.dose) {
      const step = Math.min(this.dose.ml - this.dose.dispensed, (realMs / 1000) * 2.2 * 4);
      this.dose.dispensed += step;
      this.reservoirMl -= step;
      if (this.dose.dispensed >= this.dose.ml - 0.01) this.finishDose();
    }
    for (const p of this.plants) {
      const lv = this.live.get(p.id)!;
      lv.moisture = Math.max(0, lv.moisture - lv.dryPerHour * simH);
      if (lv.state === 'soaking' && this.simMs >= lv.soakUntil) {
        const target = this.plantStatus(p).targetPct;
        if (lv.moisture >= target - 0.5 || lv.pulses >= 6) {
          lv.state = 'idle';
          this.event(
            'watering',
            p.id,
            'auto',
            lv.sessionMl,
            `${lv.pulses} pulses, -> ${Math.round(lv.moisture)}%, target reached`,
          );
          lv.lastWateredAt = nowS();
        } else {
          lv.state = 'waiting';
        }
      }
      const auto = this.settings.automationEnabled && !this.emergencyStop && p.enabled && p.autoWater;
      if (auto && lv.state === 'idle' && lv.sensor !== 'failed' && lv.moisture < p.moisture.min) {
        lv.state = 'waiting';
        lv.pulses = 0;
        lv.sessionMl = 0;
      }
      if (lv.state === 'waiting' && !this.dose && auto) {
        const ml = Math.min(
          p.maxPulseMl,
          Math.max(20, (this.plantStatus(p).targetPct - lv.moisture) * (lv.mlPerPct ?? 10) * 0.7),
        );
        this.dose = { plantId: p.id, ml: Math.round(ml), dispensed: 0, kind: 'auto' };
        lv.state = 'watering';
      }
    }
    this.emit('status', this.status());
  }

  private finishDose() {
    const d = this.dose!;
    const p = this.plants.find((x) => x.id === d.plantId);
    this.dose = null;
    this.lastDose = {
      plantId: d.plantId,
      channel: p?.channel ?? 0,
      ml: Math.round(d.dispensed * 10) / 10,
      onTimeMs: Math.round((d.dispensed / 2.2) * 1000),
      closedLoop: true,
      at: nowS(),
    };
    if (!p) return;
    const lv = this.live.get(p.id)!;
    lv.mlToday += d.dispensed;
    lv.moisture = Math.min(100, lv.moisture + d.dispensed / (lv.mlPerPct ?? 10));
    if (d.kind === 'manual') {
      lv.state = 'idle';
      lv.lastWateredAt = nowS();
      this.event('watering', p.id, 'manual', d.dispensed, 'Manual watering');
      return;
    }
    lv.pulses++;
    lv.sessionMl += d.dispensed;
    lv.state = 'soaking';
    lv.soakUntil = this.simMs + (p.soakMin * 60 * 1000) / SPEED;
  }

  // ---------------------------------------------------------------- history

  history(plantId: string, from: number, to: number, points: number): History {
    const p = this.plants.find((x) => x.id === plantId);
    if (!p) return { plantId, from, to, points: [] };
    const lv = this.live.get(p.id)!;
    const n = Math.max(10, Math.min(300, points));
    const step = (to - from) / n;
    const out: [number, number | null][] = [];
    for (let i = 0; i < n; i++) {
      const t = Math.round(from + step * (i + 0.5));
      if (t < this.bootS - 365 * 86400) continue;
      out.push([t, Math.round(this.synthetic(p, lv, t) * 10) / 10]);
    }
    if (to >= nowS() - 60) out.push([nowS(), Math.round(lv.moisture * 10) / 10]);
    return { plantId, from, to, points: out };
  }

  envHistory(from: number, to: number, points: number): EnvHistory {
    const n = Math.max(10, Math.min(300, points));
    const step = (to - from) / n;
    const out: EnvHistory['points'] = [];
    for (let i = 0; i < n; i++) {
      const t = Math.round(from + step * (i + 0.5));
      const e = this.env(t);
      out.push([
        t,
        Math.round(e.tempC * 10) / 10,
        Math.round(e.humidityPct),
        Math.round(e.lux),
        Math.round(this.syntheticReservoir(t)),
      ]);
    }
    return { from, to, points: out };
  }

  // ---------------------------------------------------------------- transport

  stream(_token: string, onEvent: StreamHandler): () => void {
    this.listeners.add(onEvent);
    return () => this.listeners.delete(onEvent);
  }

  async request(
    method: string,
    path: string,
    body: unknown,
    token: string | null,
  ): Promise<TransportResponse> {
    await new Promise((r) => setTimeout(r, 40));
    const url = new URL(path, 'http://device');
    const seg = url.pathname
      .replace(/^\/api\//, '')
      .split('/')
      .filter(Boolean);
    const qp = (k: string, d?: number) => (url.searchParams.has(k) ? Number(url.searchParams.get(k)) : d);
    const b = (body ?? {}) as Record<string, unknown>;
    const is = (m: string, ...parts: string[]) =>
      m === method && seg.length === parts.length && parts.every((x, i) => x === '*' || x === seg[i]);

    if (is('GET', 'info')) {
      return ok({
        name: this.settings.deviceName,
        version: '0.1.0-mock',
        setupRequired: this.setupRequired,
        timeValid: true,
        apActive: false,
        dashboardEmbedded: true,
      });
    }
    if (is('POST', 'setup')) {
      if (!this.setupRequired) return fail(409, 'already_set_up');
      if (String(b.password ?? '').length < 8)
        return fail(400, 'weak_password', 'Use at least 8 characters.');
      this.password = String(b.password);
      this.settings = deepMerge(this.settings, b.settings);
      this.setupRequired = false;
      return ok({ token: TOKEN });
    }
    if (is('POST', 'login')) {
      return b.password === this.password
        ? ok({ token: TOKEN })
        : fail(401, 'wrong_password', 'Wrong password.');
    }
    if (token !== TOKEN) return fail(401, 'unauthorized', 'Log in first.');

    if (is('POST', 'logout')) return ok();
    if (is('POST', 'account', 'password')) {
      if (b.current !== this.password || String(b.next ?? '').length < 8) return fail(400, 'rejected');
      this.password = String(b.next);
      return ok({ token: TOKEN });
    }
    if (is('GET', 'status')) return ok(this.status());

    if (is('GET', 'plants')) return ok(this.plants);
    if (is('POST', 'plants')) {
      const p = deepMerge(defaultProfile(Number(b.channel ?? 0)), b) as PlantProfile;
      if (!p.name) return fail(400, 'invalid', 'name is required');
      if (this.plants.some((x) => x.channel === p.channel))
        return fail(400, 'invalid', `channel ${p.channel + 1} is already used`);
      p.id =
        p.id ||
        p.name
          .toLowerCase()
          .replace(/[^a-z0-9]+/g, '-')
          .replace(/^-|-$/g, '') ||
        'plant';
      this.plants.push(p);
      this.live.set(p.id, {
        moisture: 45,
        mlToday: 0,
        lastWateredAt: null,
        state: 'idle',
        soakUntil: 0,
        pulses: 0,
        sessionMl: 0,
        lockedOut: false,
        mlPerPct: null,
        sensor: 'ok',
        issue: 'ok',
        dryPerHour: 0.5,
        periodH: 40,
        phase: 0,
      });
      return ok(p, 201);
    }
    if (seg[0] === 'plants' && seg[1]) {
      const p = this.plants.find((x) => x.id === seg[1]);
      if (!p) return fail(404, 'not_found');
      const lv = this.live.get(p.id)!;
      if (is('GET', 'plants', '*')) return ok(p);
      if (is('PUT', 'plants', '*')) {
        const { id: _ignored, ...rest } = b;
        const next = deepMerge(p, rest);
        if (this.plants.some((x) => x.id !== p.id && x.channel === next.channel)) {
          return fail(400, 'invalid', `channel ${next.channel + 1} is already used`);
        }
        Object.assign(p, next);
        return ok(p);
      }
      if (is('DELETE', 'plants', '*')) {
        this.plants = this.plants.filter((x) => x !== p);
        this.live.delete(p.id);
        return ok();
      }
      if (is('POST', 'plants', '*', 'water') || is('POST', 'plants', '*', 'timed-run')) {
        if (this.emergencyStop) return fail(409, 'emergency_stop');
        if (this.dose) return fail(409, 'busy');
        if (lv.lockedOut) return fail(409, 'lockout');
        const ml = seg[2] === 'water' ? Number(b.ml ?? 0) : Number(b.seconds ?? 0) * p.outlet.flowMlPerS;
        if (!(ml >= 5)) return fail(409, 'invalid_amount');
        this.dose = {
          plantId: p.id,
          ml: Math.min(ml, this.settings.limits.hardMaxPulseMl),
          dispensed: 0,
          kind: 'manual',
        };
        lv.state = 'watering';
        return ok();
      }
      if (is('POST', 'plants', '*', 'clear-faults')) {
        lv.lockedOut = false;
        if (lv.issue === 'no_response') {
          lv.issue = 'ok';
          lv.sensor = 'ok';
        }
        return ok();
      }
      if (is('POST', 'plants', '*', 'calibrate')) {
        const raw = this.plantStatus(p).raw;
        if (b.point === 'dry') p.calibration.rawDry = raw;
        else if (b.point === 'wet') p.calibration.rawWet = raw;
        else return fail(400, 'invalid', 'point must be dry or wet');
        return ok({ raw, calibration: p.calibration });
      }
    }

    if (is('GET', 'history')) {
      const to = qp('to', nowS())!;
      return ok(
        this.history(url.searchParams.get('plant') ?? '', qp('from', to - 86400)!, to, qp('points', 200)!),
      );
    }
    if (is('GET', 'env', 'history')) {
      const to = qp('to', nowS())!;
      return ok(this.envHistory(qp('from', to - 86400)!, to, qp('points', 200)!));
    }
    if (is('GET', 'events')) {
      const to = qp('to', nowS())!;
      const from = qp('from', to - 30 * 86400)!;
      const type = url.searchParams.get('type') ?? '';
      const plant = url.searchParams.get('plant') ?? '';
      const limit = qp('limit', 100)!;
      const events = this.events
        .filter(
          (e) => e.ts >= from && e.ts <= to && (!type || e.type === type) && (!plant || e.plantId === plant),
        )
        .slice(-limit)
        .reverse();
      return ok({ events });
    }

    if (is('GET', 'alerts')) {
      const sorted = this.alerts
        .slice()
        .sort((a, b2) => Number(b2.active) - Number(a.active) || b2.raisedAt - a.raisedAt);
      return ok({ alerts: sorted });
    }
    if (is('POST', 'alerts', 'ack-all')) {
      this.alerts.forEach((a) => (a.acked = a.acked || a.active));
      return ok();
    }
    if (is('POST', 'alerts', '*', 'ack')) {
      const a = this.alerts.find((x) => x.id === Number(seg[1]));
      if (!a) return fail(404, 'not_found');
      a.acked = true;
      return ok();
    }

    if (is('GET', 'settings')) return ok(this.settings);
    if (is('PUT', 'settings')) {
      const { notify, ...rest } = b as { notify?: Record<string, unknown> };
      this.settings = deepMerge(this.settings, rest);
      if (notify) {
        const { token: t, ...n } = notify;
        this.settings.notify = { ...this.settings.notify, ...(n as object) };
        if (typeof t === 'string') this.settings.notify.tokenSet = t.length > 0;
      }
      return ok(this.settings);
    }

    if (is('POST', 'reservoir', 'tare') || is('POST', 'reservoir', 'calibrate')) return ok();
    if (is('POST', 'reservoir', 'refilled')) {
      this.reservoirMl = Math.min(this.settings.reservoir.capacityMl, this.reservoirMl + Number(b.ml ?? 0));
      this.event('refill', null, 'manual_refill', Number(b.ml ?? 0), 'Refill recorded from the dashboard');
      return ok();
    }

    if (is('POST', 'system', 'stop')) {
      this.emergencyStop = true;
      this.dose = null;
      this.raise(
        'info',
        'emergency_stop',
        null,
        `Emergency stop (${String(b.reason ?? 'stopped from the dashboard')}). Watering is disabled until you resume.`,
      );
      this.event('system', null, 'emergency_stop', 0, String(b.reason ?? 'stopped from the dashboard'));
      return ok();
    }
    if (is('POST', 'system', 'resume')) {
      this.emergencyStop = false;
      this.clear('emergency_stop', null);
      this.event('system', null, 'resume', 0, 'Watering resumed');
      return ok();
    }
    if (is('POST', 'system', 'automation')) {
      this.settings.automationEnabled = Boolean(b.enabled);
      return ok();
    }
    if (is('POST', 'system', 'reboot') || is('POST', 'system', 'time')) return ok();

    if (is('GET', 'config', 'backup')) {
      return ok({ version: '0.1.0-mock', exportedAt: nowS(), settings: this.settings, plants: this.plants });
    }
    if (is('POST', 'config', 'restore')) {
      this.settings = deepMerge(this.settings, b.settings);
      if (Array.isArray(b.plants)) this.plants = b.plants as PlantProfile[];
      return ok();
    }

    if (is('GET', 'wifi')) {
      return ok({
        configured: true,
        ssid: 'HomeWiFi',
        mode: 'sta',
        ip: '192.168.1.50',
        rssi: -58,
        apActive: false,
        apSsid: 'PlantCare-1A2B',
        internet: true,
      });
    }
    if (is('POST', 'wifi', 'scan') || is('POST', 'wifi')) return ok();
    if (is('GET', 'wifi', 'scan')) {
      return ok({
        scanning: false,
        networks: [
          { ssid: 'HomeWiFi', rssi: -58, secure: true },
          { ssid: 'Neighbour 5G', rssi: -77, secure: true },
          { ssid: 'Cafe guest', rssi: -84, secure: false },
        ],
      });
    }
    if (is('POST', 'notify', 'test')) return ok();
    if (is('GET', 'notify', 'test'))
      return ok({ result: this.settings.notify.topic ? 'ok' : 'no topic configured', at: nowS() });

    return fail(404, 'not_found', 'Unknown API endpoint');
  }
}
