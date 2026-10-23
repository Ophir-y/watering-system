// A simulated PlantCare main station with a fleet of plant nodes, implementing the
// same HTTP API as the firmware. History is synthesized from deterministic curves;
// the present is simulated live (plants dry out 120x faster than real life, and the
// nodes check in on that same fast clock, so a full cycle takes minutes).
import type { StreamHandler, Transport, TransportResponse } from '../api/transport';
import type {
  Alert,
  DeviceEvent,
  EmptyReason,
  EnvHistory,
  History,
  NewNode,
  PlannerState,
  PlantProfile,
  PlantStatus,
  PumpVerdict,
  QueuedCommand,
  SensorHealth,
  SensorIssue,
  Settings,
  Status,
} from '../api/types';
import { applySpecies, defaultProfile, findSpecies, limitsForPot } from '../data/species';

const SPEED = 120; // simulated seconds per real second for drying, soaking and check-ins
const POUR_SPEED = 4; // doses run this much faster than real time
const TOKEN = 'a3f1c0de5eed0000a3f1c0de5eed0000';
const MAX_PLANTS = 16;
const nowS = () => Math.floor(Date.now() / 1000);

interface Dose {
  ml: number;
  dispensed: number;
  kind: 'auto' | 'manual' | 'fallback' | 'timed';
}

// One plant node and the plant it waters.
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
  dose: Dose | null;
  bottleMl: number;
  bottleEmpty: boolean;
  emptyReason: EmptyReason;
  refillDays: number;
  pumpLearned: boolean;
  lastRun: PumpVerdict;
  flowFactor: number;
  // The node.
  batteryMv: number;
  linkPct: number;
  online: boolean;
  checkInAt: number; // simulated ms of the next check-in
  lastSeenAt: number | null;
  queue: QueuedCommand[];
  nextCmdId: number;
  configPending: boolean;
  repair: boolean;
  buttonAt: number;
}

const DEMO: {
  species: string;
  name: string;
  pot: number;
  periodH: number;
  bottle: number;
  bottleFrac: number;
  batteryMv: number;
  issue?: SensorIssue;
}[] = [
  {
    species: 'monstera_deliciosa',
    name: 'Monstera',
    pot: 6000,
    periodH: 70,
    bottle: 2000,
    bottleFrac: 0.62,
    batteryMv: 4380,
  },
  {
    species: 'nephrolepis_exaltata',
    name: 'Boston fern',
    pot: 2500,
    periodH: 28,
    bottle: 1500,
    bottleFrac: 0.48,
    batteryMv: 4210,
  },
  {
    species: 'dracaena_trifasciata',
    name: 'Snake plant',
    pot: 3000,
    periodH: 260,
    bottle: 1000,
    bottleFrac: 0.9,
    batteryMv: 3540,
  },
  {
    species: 'ocimum_basilicum',
    name: 'Kitchen basil',
    pot: 1200,
    periodH: 20,
    bottle: 1000,
    bottleFrac: 0.18,
    batteryMv: 4450,
  },
  {
    species: 'curio_rowleyanus',
    name: 'String of pearls',
    pot: 800,
    periodH: 160,
    bottle: 500,
    bottleFrac: 0.7,
    batteryMv: 4100,
    issue: 'stuck',
  },
];

function defaultSettings(): Settings {
  return {
    deviceName: 'PlantCare',
    timezone: 'UTC0',
    tzName: 'UTC',
    southernHemisphere: false,
    waterWindow: { startH: 7, endH: 21 },
    checkInMin: 5,
    logIntervalMin: 10,
    automationEnabled: true,
    limits: { hardMaxPulseMl: 250, hardMaxDailyMlPerPlant: 1500, hardMaxOnTimeS: 120 },
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

const nodeId = (i: number) => `a0b1c2d3e4${(0xf0 + i).toString(16)}`;

export class MockDevice implements Transport {
  settings = defaultSettings();
  plants: PlantProfile[] = [];
  live = new Map<string, Live>();
  waiting: NewNode[] = [];
  alerts: Alert[] = [];
  events: DeviceEvent[] = [];
  emergencyStop = false;
  setupRequired: boolean;
  password = 'plantcare';
  private nextAlertId = 1;
  private timer: ReturnType<typeof setInterval> | null = null;
  private listeners = new Set<StreamHandler>();
  private lastTick = Date.now();
  private simMs = 0; // simulated clock, advanced by tick()
  private bootS = nowS() - 3 * 86400 - 4200;

  constructor(opts: { setupRequired?: boolean; autoTick?: boolean } = {}) {
    this.setupRequired = opts.setupRequired ?? false;
    DEMO.forEach((d, i) => {
      const s = findSpecies(d.species)!;
      const p = applySpecies({ ...defaultProfile(nodeId(i)), ...limitsForPot(d.pot), potVolumeMl: d.pot }, s);
      p.id = d.name.toLowerCase().replace(/[^a-z0-9]+/g, '-');
      p.name = d.name;
      p.pump.flowMlPerS = 1.6;
      p.bottle.capacityMl = d.bottle;
      this.plants.push(p);
      const lv = this.newLive(p, {
        mlToday: Math.round(40 + i * 25),
        mlPerPct: Math.round((p.potVolumeMl / 120) * 10) / 10,
        sensor: d.issue ? 'suspect' : 'ok',
        issue: d.issue ?? 'ok',
        dryPerHour: (p.moisture.target - p.moisture.min) / d.periodH,
        periodH: d.periodH,
        phase: (i * 0.37) % 1,
        bottleMl: Math.round(d.bottle * d.bottleFrac),
        refillDays: Math.max(
          3,
          Math.round(
            (d.bottle / ((p.moisture.target - p.moisture.min) * (p.potVolumeMl / 120))) * (d.periodH / 24),
          ),
        ),
        batteryMv: d.batteryMv,
        pumpLearned: i !== 4,
        flowFactor: [1.04, 0.97, 1, 1.08, 1][i],
        checkInAt: ((i + 1) * this.checkInMs()) / (DEMO.length + 1),
        lastSeenAt:
          nowS() - Math.round(((DEMO.length - i) * this.settings.checkInMin * 60) / (DEMO.length + 1)),
        linkPct: [100, 97, 100, 88, 100][i],
      });
      lv.moisture = this.synthetic(p, lv, nowS());
      lv.lastWateredAt = this.lastSyntheticWatering(lv, nowS());
      this.live.set(p.id, lv);
    });
    this.waiting.push({
      id: '34b7da5c11f0',
      board: 'xiao-esp32c3',
      firmware: '0.2.0',
      lastSeenAt: nowS() - 40,
    });
    this.seedEvents();
    this.raise(
      'warning',
      'sensor_fault',
      'string-of-pearls',
      'String of pearls: moisture sensor reading has not changed for 12 hours.',
      nowS() - 5400,
    );
    this.raise(
      'info',
      'humidity_out_of_range',
      'boston-fern',
      'Boston fern prefers 60-90 % humidity; it has been 47 % for over 2 hours.',
      nowS() - 9000,
    );
    this.raise(
      'warning',
      'bottle_low',
      'kitchen-basil',
      "Kitchen basil's bottle is low: about 180 ml (18%), about 0.9 days left.",
      nowS() - 3600,
    );
    this.raise(
      'warning',
      'battery_low',
      'snake-plant',
      'Snake plant: batteries are low (3.54 V). Replace them soon.',
      nowS() - 20000,
    );
    if (opts.autoTick !== false) this.timer = setInterval(() => this.tick(), 1000);
  }

  dispose() {
    if (this.timer) clearInterval(this.timer);
  }

  private checkInMs() {
    return (this.settings.checkInMin * 60 * 1000) / SPEED;
  }

  private newLive(p: PlantProfile, over: Partial<Live> = {}): Live {
    return {
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
      dose: null,
      bottleMl: p.bottle.capacityMl,
      bottleEmpty: false,
      emptyReason: 'none',
      refillDays: 7,
      pumpLearned: false,
      lastRun: 'ok',
      flowFactor: 1,
      batteryMv: 4600,
      linkPct: 100,
      online: true,
      checkInAt: this.simMs + this.checkInMs(),
      lastSeenAt: nowS(),
      queue: [],
      nextCmdId: 1,
      configPending: false,
      repair: false,
      buttonAt: 0,
      ...over,
    };
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

  // A sawtooth: full at each refill, down to ~15 % before the next.
  private syntheticBottle(p: PlantProfile, lv: Live, t: number) {
    const periodS = lv.refillDays * 86400;
    const nowFrac = 1 - lv.bottleMl / p.bottle.capacityMl;
    const frac = ((((t - nowS()) / periodS + nowFrac) % 1) + 1) % 1;
    return p.bottle.capacityMl * (1 - 0.85 * frac);
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
      const periodS = lv.refillDays * 86400;
      const sinceRefill = ((1 - lv.bottleMl / p.bottle.capacityMl) / 0.85) * periodS;
      for (let t = now - sinceRefill; t > from; t -= periodS) {
        this.events.push({
          ts: Math.round(t),
          type: 'refill',
          plantId: p.id,
          code: 'refill',
          ml: p.bottle.capacityMl,
          detail: 'Bottle refilled',
        });
      }
    }
    this.events.sort((a, b) => a.ts - b.ts);
  }

  private plantStatus(p: PlantProfile): PlantStatus {
    const lv = this.live.get(p.id);
    const targetPct =
      p.style === 'soak_and_dry' ? Math.max(p.moisture.target, p.moisture.max - 3) : p.moisture.target;
    if (!lv || !p.node) {
      return {
        id: p.id,
        name: p.name,
        node: p.node,
        online: false,
        lastSeenAt: null,
        nextCheckInS: null,
        firmware: null,
        battery: null,
        linkPct: null,
        moisturePct: null,
        raw: null,
        sensor: 'unknown',
        issue: 'unknown',
        state: 'unknown',
        triggerPct: p.moisture.min,
        targetPct,
        mlToday: 0,
        lastWateredAt: null,
        lockedOut: false,
        mlPerPct: null,
        soakRemainingS: 0,
        bottle: null,
        pump: { learned: false, lastRun: 'ok' },
        dosing: null,
        queued: [],
        configPending: false,
        repairRequested: false,
        buttonPressed: false,
      };
    }
    const pct = (lv.bottleMl / p.bottle.capacityMl) * 100;
    const usage = (p.moisture.target - p.moisture.min) * (lv.mlPerPct ?? 10) * (24 / lv.periodH);
    const daysLeft = usage > 1 ? lv.bottleMl / usage : null;
    const battPct = Math.round(Math.min(100, Math.max(0, ((lv.batteryMv - 3300) / (4650 - 3300)) * 100)));
    return {
      id: p.id,
      name: p.name,
      node: p.node,
      online: lv.online,
      lastSeenAt: lv.lastSeenAt,
      nextCheckInS: lv.online ? Math.max(0, Math.round(((lv.checkInAt - this.simMs) / 1000) * SPEED)) : null,
      firmware: '0.2.0',
      battery: { mv: lv.batteryMv, pct: battPct, low: lv.batteryMv < 3600 },
      linkPct: lv.linkPct,
      moisturePct:
        lv.sensor === 'failed' && lv.issue !== 'no_response' ? null : Math.round(lv.moisture * 10) / 10,
      raw: Math.round(
        p.calibration.rawDry + ((p.calibration.rawWet - p.calibration.rawDry) * lv.moisture) / 100,
      ),
      sensor: lv.sensor,
      issue: lv.issue,
      state: lv.dose ? 'watering' : lv.state,
      triggerPct: p.moisture.min,
      targetPct,
      mlToday: Math.round(lv.mlToday),
      lastWateredAt: lv.lastWateredAt,
      lockedOut: lv.lockedOut,
      mlPerPct: lv.mlPerPct,
      soakRemainingS:
        lv.state === 'soaking' ? Math.max(0, Math.round((lv.soakUntil - this.simMs) / 1000) * SPEED) : 0,
      bottle: {
        ml: Math.round(lv.bottleMl),
        pct: Math.round(pct * 10) / 10,
        capacityMl: p.bottle.capacityMl,
        daysLeft: daysLeft === null ? null : Math.round(daysLeft * 10) / 10,
        low: !lv.bottleEmpty && (pct < p.bottle.lowPct || (daysLeft !== null && daysLeft < 2 && pct < 50)),
        empty: lv.bottleEmpty,
        emptyReason: lv.emptyReason,
        flowFactor: lv.flowFactor,
      },
      pump: { learned: lv.pumpLearned && p.pump.currentSense, lastRun: lv.lastRun },
      dosing: lv.dose ? { ml: lv.dose.ml, dispensedMl: Math.round(lv.dose.dispensed * 10) / 10 } : null,
      queued: lv.queue.slice(),
      configPending: lv.configPending,
      repairRequested: lv.repair,
      buttonPressed: lv.buttonAt > 0 && Date.now() - lv.buttonAt < 60000,
    };
  }

  status(): Status {
    const now = nowS();
    const env = this.env(now);
    const counts = { critical: 0, warning: 0, info: 0 };
    for (const a of this.alerts) if (a.active) counts[a.severity]++;
    const withNode = this.plants.filter((p) => p.node && this.live.has(p.id));
    return {
      device: {
        name: this.settings.deviceName,
        version: '0.2.0-mock',
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
      plants: this.plants.map((p) => this.plantStatus(p)),
      newNodes: this.waiting.slice(),
      radio: {
        channel: 6,
        checkInMin: this.settings.checkInMin,
        online: withNode.filter((p) => this.live.get(p.id)!.online).length,
        total: withNode.length,
      },
      mlTodayTotal: Math.round([...this.live.values()].reduce((a, l) => a + l.mlToday, 0)),
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
    plantId: string | null,
    message: string,
    at = nowS(),
  ) {
    if (this.alerts.some((a) => a.active && a.code === code && a.plantId === plantId)) return;
    this.alerts.push({
      id: this.nextAlertId++,
      code,
      severity,
      plantId,
      active: true,
      acked: false,
      raisedAt: at,
      clearedAt: null,
      message,
    });
  }

  private clear(code: string, plantId: string | null) {
    for (const a of this.alerts) {
      if (a.active && a.code === code && a.plantId === plantId) {
        a.active = false;
        a.clearedAt = nowS();
      }
    }
  }

  tick(realMs = Date.now() - this.lastTick) {
    this.lastTick = Date.now();
    this.simMs += realMs;
    const simH = (realMs / 3_600_000) * SPEED;
    for (const p of this.plants) {
      const lv = this.live.get(p.id);
      if (!lv || !p.node) continue;
      lv.moisture = Math.max(0, lv.moisture - lv.dryPerHour * simH);
      if (lv.dose) this.pour(p, lv, realMs);
      if (lv.online && this.simMs >= lv.checkInAt) this.checkIn(p, lv);

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
      const canPump = !lv.bottleEmpty && !lv.lockedOut;
      if (auto && canPump && lv.state === 'idle' && lv.sensor !== 'failed' && lv.moisture < p.moisture.min) {
        lv.state = 'waiting';
        lv.pulses = 0;
        lv.sessionMl = 0;
      }
      if (lv.state === 'waiting' && !lv.dose && auto && canPump) {
        const ml = Math.min(
          p.maxPulseMl,
          Math.max(20, (this.plantStatus(p).targetPct - lv.moisture) * (lv.mlPerPct ?? 10) * 0.7),
        );
        lv.dose = { ml: Math.round(ml), dispensed: 0, kind: 'auto' };
        lv.state = 'watering';
      }
    }
    this.emit('status', this.status());
  }

  // Each node in turn: the station's mailbox is delivered when it checks in.
  private checkIn(p: PlantProfile, lv: Live) {
    lv.checkInAt = this.simMs + this.checkInMs();
    lv.lastSeenAt = nowS();
    lv.configPending = false;
    const queue = lv.queue;
    lv.queue = [];
    for (const c of queue) {
      if (c.kind === 'water' || c.kind === 'timed_run') {
        if (this.emergencyStop || lv.lockedOut || lv.bottleEmpty || lv.dose) continue;
        const timed = c.kind === 'timed_run';
        const ml = timed ? c.arg * p.pump.flowMlPerS * lv.flowFactor : c.arg;
        lv.dose = {
          ml: Math.min(ml, this.settings.limits.hardMaxPulseMl),
          dispensed: 0,
          kind: timed ? 'timed' : 'manual',
        };
        lv.state = 'idle';
      } else if (c.kind === 'refilled') {
        this.refill(p, lv);
      } else if (c.kind === 'clear_faults') {
        lv.lockedOut = false;
        if (lv.issue === 'no_response') {
          lv.issue = 'ok';
          lv.sensor = 'ok';
        }
        this.event('system', p.id, 'lockout_cleared', 0, `${p.name}: faults cleared by user`);
      }
    }
  }

  private refill(p: PlantProfile, lv: Live) {
    lv.bottleMl = p.bottle.capacityMl;
    lv.bottleEmpty = false;
    lv.emptyReason = 'none';
    this.clear('bottle_empty', p.id);
    this.clear('bottle_low', p.id);
    this.event('refill', p.id, 'refill', p.bottle.capacityMl, 'Bottle refilled');
  }

  private pour(p: PlantProfile, lv: Live, realMs: number) {
    const d = lv.dose!;
    const flow = p.pump.flowMlPerS * lv.flowFactor;
    const step = Math.min(d.ml - d.dispensed, (realMs / 1000) * flow * POUR_SPEED);
    const usable = Math.max(0, lv.bottleMl - p.bottle.reserveMl);
    if (step > usable) {
      // The pump starts sucking air: it feels it, stops, and calls the bottle empty.
      d.dispensed += usable;
      lv.bottleMl -= usable;
      lv.bottleEmpty = true;
      lv.emptyReason = p.pump.currentSense ? 'pump_dry' : 'gauge';
      lv.lastRun = p.pump.currentSense ? 'dry' : 'ok';
      this.raise(
        'critical',
        'bottle_empty',
        p.id,
        `${p.name}'s bottle is empty: the pump ran dry. Refill it, then press the node's button.`,
      );
      this.finishDose(p, lv);
      return;
    }
    d.dispensed += step;
    lv.bottleMl -= step;
    if (d.dispensed >= d.ml - 0.01) this.finishDose(p, lv);
  }

  private finishDose(p: PlantProfile, lv: Live) {
    const d = lv.dose!;
    lv.dose = null;
    if (d.kind === 'timed') {
      this.event(
        'watering',
        p.id,
        'timed',
        d.dispensed,
        `Timed pump run (${(d.dispensed / (p.pump.flowMlPerS * lv.flowFactor)).toFixed(1)} s)`,
      );
      return;
    }
    lv.mlToday += d.dispensed;
    lv.moisture = Math.min(100, lv.moisture + d.dispensed / (lv.mlPerPct ?? 10));
    if (lv.bottleMl < (p.bottle.lowPct / 100) * p.bottle.capacityMl && !lv.bottleEmpty) {
      this.raise(
        'warning',
        'bottle_low',
        p.id,
        `${p.name}'s bottle is low: about ${Math.round(lv.bottleMl)} ml.`,
      );
    }
    if (d.kind === 'manual') {
      lv.state = 'idle';
      lv.lastWateredAt = nowS();
      this.event('watering', p.id, 'manual', d.dispensed, 'Manual watering');
      return;
    }
    if (lv.bottleEmpty) {
      lv.state = 'idle';
      return;
    }
    lv.pulses++;
    lv.sessionMl += d.dispensed;
    lv.state = 'soaking';
    lv.soakUntil = this.simMs + (p.soakMin * 60 * 1000) / SPEED;
  }

  private queue(p: PlantProfile, kind: QueuedCommand['kind'], arg: number): TransportResponse {
    const lv = this.live.get(p.id);
    if (!lv || !p.node) return fail(409, 'no_node', 'This plant has no node.');
    if (lv.queue.length >= 8) return fail(409, 'busy');
    const c = { id: lv.nextCmdId++, kind, arg };
    lv.queue.push(c);
    const etaS = lv.online ? Math.max(0, Math.round(((lv.checkInAt - this.simMs) / 1000) * SPEED)) : null;
    return ok({ queued: true, id: c.id, etaS }, 202);
  }

  // Simulates someone pressing the node's button (refilled + highlight).
  pressButton(plantId: string) {
    const p = this.plants.find((x) => x.id === plantId);
    const lv = p && this.live.get(p.id);
    if (!p || !lv) return;
    lv.buttonAt = Date.now();
    this.refill(p, lv);
    this.checkIn(p, lv);
  }

  // ---------------------------------------------------------------- history

  history(plantId: string, from: number, to: number, points: number): History {
    const p = this.plants.find((x) => x.id === plantId);
    const lv = p && this.live.get(p.id);
    if (!p || !lv) return { plantId, from, to, points: [] };
    const n = Math.max(10, Math.min(300, points));
    const step = (to - from) / n;
    const out: History['points'] = [];
    for (let i = 0; i < n; i++) {
      const t = Math.round(from + step * (i + 0.5));
      if (t < this.bootS - 365 * 86400) continue;
      out.push([
        t,
        Math.round(this.synthetic(p, lv, t) * 10) / 10,
        Math.round(this.syntheticBottle(p, lv, t)),
      ]);
    }
    if (to >= nowS() - 60) out.push([nowS(), Math.round(lv.moisture * 10) / 10, Math.round(lv.bottleMl)]);
    return { plantId, from, to, points: out };
  }

  envHistory(from: number, to: number, points: number): EnvHistory {
    const n = Math.max(10, Math.min(300, points));
    const step = (to - from) / n;
    const out: EnvHistory['points'] = [];
    for (let i = 0; i < n; i++) {
      const t = Math.round(from + step * (i + 0.5));
      const e = this.env(t);
      out.push([t, Math.round(e.tempC * 10) / 10, Math.round(e.humidityPct), Math.round(e.lux)]);
    }
    return { from, to, points: out };
  }

  // ---------------------------------------------------------------- transport

  stream(_token: string, onEvent: StreamHandler): () => void {
    this.listeners.add(onEvent);
    return () => this.listeners.delete(onEvent);
  }

  private savePlant(p: PlantProfile, prevNode: string | null): TransportResponse | null {
    if (!p.name) return fail(400, 'invalid', 'name is required');
    if (p.node && this.plants.some((x) => x.id !== p.id && x.node === p.node)) {
      return fail(400, 'invalid', `node ${p.node} already waters another plant`);
    }
    if (p.node !== prevNode) {
      this.waiting = this.waiting.filter((n) => n.id !== p.node);
      const lv = this.live.get(p.id) ?? this.newLive(p);
      if (p.node) {
        lv.configPending = true;
        lv.online = true;
        lv.checkInAt = this.simMs + this.checkInMs() / 4;
        this.live.set(p.id, lv);
      }
    } else {
      const lv = this.live.get(p.id);
      if (lv) lv.configPending = true;
    }
    return null;
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
        version: '0.2.0-mock',
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
      if (this.plants.length >= MAX_PLANTS) return fail(400, 'invalid', `at most ${MAX_PLANTS} plants`);
      const p = deepMerge(defaultProfile(null), b) as PlantProfile;
      p.id =
        p.id ||
        p.name
          .toLowerCase()
          .replace(/[^a-z0-9]+/g, '-')
          .replace(/^-|-$/g, '') ||
        'plant';
      const err = this.savePlant(p, null);
      if (err) return err;
      this.plants.push(p);
      return ok(p, 201);
    }
    if (seg[0] === 'plants' && seg[1]) {
      const p = this.plants.find((x) => x.id === seg[1]);
      if (!p) return fail(404, 'not_found');
      if (is('GET', 'plants', '*')) return ok(p);
      if (is('PUT', 'plants', '*')) {
        const { id: _ignored, ...rest } = b;
        const next = deepMerge(p, rest);
        const err = this.savePlant(next, p.node);
        if (err) return err;
        Object.assign(p, next);
        return ok(p);
      }
      if (is('DELETE', 'plants', '*')) {
        this.plants = this.plants.filter((x) => x !== p);
        this.live.delete(p.id);
        return ok();
      }
      if (is('POST', 'plants', '*', 'water')) {
        if (this.emergencyStop) return fail(409, 'emergency_stop', 'Watering is stopped. Resume first.');
        const ml = Number(b.ml ?? 0);
        if (!(ml >= 5 && ml <= this.settings.limits.hardMaxPulseMl)) return fail(400, 'invalid_amount');
        return this.queue(p, 'water', ml);
      }
      if (is('POST', 'plants', '*', 'timed-run')) {
        const s = Number(b.seconds ?? 0);
        if (!(s >= 1 && s <= 60)) return fail(400, 'invalid_amount');
        return this.queue(p, 'timed_run', s);
      }
      if (is('POST', 'plants', '*', 'refilled')) return this.queue(p, 'refilled', Number(b.ml ?? 0));
      if (is('POST', 'plants', '*', 'clear-faults')) return this.queue(p, 'clear_faults', 0);
      if (is('POST', 'plants', '*', 'identify')) return this.queue(p, 'identify', 0);
      if (is('POST', 'plants', '*', 'calibrate')) {
        const raw = this.plantStatus(p).raw;
        if (raw === null) return fail(400, 'invalid', "no reading from this plant's node yet");
        if (b.point === 'dry') p.calibration.rawDry = raw;
        else if (b.point === 'wet') p.calibration.rawWet = raw;
        else return fail(400, 'invalid', 'point must be dry or wet');
        const lv = this.live.get(p.id);
        if (lv) lv.configPending = true;
        return ok({ raw, ageS: 40, calibration: p.calibration });
      }
    }
    if (is('POST', 'nodes', '*', 'repair')) {
      const p = this.plants.find((x) => x.node === seg[1]);
      const lv = p && this.live.get(p.id);
      if (!lv) return fail(404, 'not_found');
      lv.repair = false;
      lv.configPending = true;
      return ok();
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
      for (const lv of this.live.values()) lv.configPending = true;
      return ok(this.settings);
    }

    if (is('POST', 'system', 'stop')) {
      this.emergencyStop = true;
      for (const lv of this.live.values()) {
        lv.dose = null;
        lv.state = 'idle';
      }
      this.raise(
        'info',
        'emergency_stop',
        null,
        `Emergency stop (${String(b.reason ?? 'stopped from the dashboard')}). Every node stops watering at its next check-in, until you resume.`,
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
      return ok({ version: '0.2.0-mock', exportedAt: nowS(), settings: this.settings, plants: this.plants });
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
