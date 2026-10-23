// Mirrors the main station's JSON (firmware/lib/plantcore/src/Json.cpp and
// firmware/lib/plantlink/src/Station.cpp). The shapes are checked against
// shared/api-fixtures by src/api/contract.test.ts.

export type Severity = 'info' | 'warning' | 'critical';
export type WateringStyle = 'evenly_moist' | 'dry_between' | 'soak_and_dry';
export type SensorFaultPolicy = 'pause' | 'fallback_schedule';
// 'unknown': the plant's node has not reported since the main station started.
export type SensorHealth = 'ok' | 'suspect' | 'failed' | 'unknown';
export type SensorIssue =
  | 'ok'
  | 'uncalibrated'
  | 'bus_error'
  | 'disconnected'
  | 'out_of_range'
  | 'no_response'
  | 'stuck'
  | 'erratic'
  | 'unknown';
export type PlannerState = 'idle' | 'waiting' | 'watering' | 'soaking' | 'unknown';
export type EmptyReason = 'none' | 'gauge' | 'pump_dry' | 'no_response';
export type PumpVerdict = 'ok' | 'dry' | 'disconnected' | 'jammed';
export type CommandKind = 'water' | 'timed_run' | 'refilled' | 'clear_faults' | 'identify' | 'forget';
export type SessionKind = 'auto' | 'manual' | 'fallback';
export type EventType = 'watering' | 'alert' | 'alert_cleared' | 'refill' | 'system';

export interface Range {
  min: number;
  max: number;
}

export interface MoistureBand {
  critical: number;
  min: number;
  target: number;
  max: number;
}

export interface PlantProfile {
  id: string;
  name: string;
  species: string;
  enabled: boolean;
  autoWater: boolean;
  // The plant node watering this plant (12 hex digits of its radio address).
  node: string | null;
  pump: { flowMlPerS: number; currentSense: boolean };
  bottle: { capacityMl: number; lowPct: number; reserveMl: number };
  potVolumeMl: number;
  soil: string;
  moisture: MoistureBand;
  lightLux: Range;
  tempC: Range;
  humidityPct: Range;
  style: WateringStyle;
  minIntervalH: number;
  maxDailyMl: number;
  maxPulseMl: number;
  soakMin: number;
  dormantMonths: number[];
  dormancyFactor: number;
  onSensorFault: SensorFaultPolicy;
  fallbackEveryDays: number;
  fallbackMl: number;
  calibration: { rawDry: number; rawWet: number };
  careNotes: string;
}

export interface Settings {
  deviceName: string;
  timezone: string;
  tzName: string;
  southernHemisphere: boolean;
  waterWindow: { startH: number; endH: number };
  checkInMin: number;
  logIntervalMin: number;
  automationEnabled: boolean;
  limits: {
    hardMaxPulseMl: number;
    hardMaxDailyMlPerPlant: number;
    hardMaxOnTimeS: number;
  };
  notify: {
    enabled: boolean;
    server: string;
    topic: string;
    tokenSet: boolean;
    minSeverity: Severity;
  };
}

// Write-only fields accepted by PUT /api/settings.
export type SettingsPatch = DeepPartial<Omit<Settings, 'notify'>> & {
  notify?: Partial<Omit<Settings['notify'], 'tokenSet'>> & { token?: string };
};

export interface QueuedCommand {
  id: number;
  kind: CommandKind;
  arg: number;
}

export interface BottleStatus {
  ml: number;
  pct: number;
  capacityMl: number;
  daysLeft: number | null;
  low: boolean;
  empty: boolean;
  emptyReason: EmptyReason;
  flowFactor: number; // learned correction of the pump's flow (1 = as measured)
}

export interface PlantStatus {
  id: string;
  name: string;
  node: string | null;
  online: boolean;
  lastSeenAt: number | null;
  nextCheckInS: number | null;
  firmware: string | null;
  battery: { mv: number; pct: number; low: boolean } | null;
  linkPct: number | null;
  moisturePct: number | null;
  raw: number | null;
  sensor: SensorHealth;
  issue: SensorIssue;
  state: PlannerState;
  triggerPct: number;
  targetPct: number;
  mlToday: number;
  lastWateredAt: number | null;
  lockedOut: boolean;
  mlPerPct: number | null;
  soakRemainingS: number;
  bottle: BottleStatus | null;
  pump: { learned: boolean; lastRun: PumpVerdict };
  dosing: { ml: number; dispensedMl: number } | null;
  queued: QueuedCommand[];
  configPending: boolean;
  repairRequested: boolean;
  buttonPressed: boolean;
}

// A node that said hello and is waiting to be added as a plant.
export interface NewNode {
  id: string;
  board: string;
  firmware: string;
  lastSeenAt: number | null;
}

export interface Status {
  device: {
    name: string;
    version: string;
    uptimeS: number;
    timeValid: boolean;
    now: number | null;
    tzName: string;
    heapFree: number;
    resetReason: string;
  };
  network: {
    mode: 'sta' | 'ap' | 'sta+ap' | 'off';
    ssid: string;
    ip: string;
    rssi: number;
    apActive: boolean;
    apSsid: string;
    internet: boolean;
  };
  storage: { sdOk: boolean; sdUsedMb: number; sdTotalMb: number };
  automation: { enabled: boolean; emergencyStop: boolean };
  environment: {
    valid: boolean;
    tempC: number | null;
    humidityPct: number | null;
    vpdKPa: number | null;
    lux: number | null;
  };
  plants: PlantStatus[];
  newNodes: NewNode[];
  radio: { channel: number; checkInMin: number; online: number; total: number };
  mlTodayTotal: number;
  alerts: { critical: number; warning: number; info: number };
}

// What a plant command returns: it runs when the node next checks in.
export interface Queued {
  queued: true;
  id: number;
  etaS: number | null;
}

export interface Alert {
  id: number;
  code: string;
  severity: Severity;
  plantId: string | null;
  active: boolean;
  acked: boolean;
  raisedAt: number;
  clearedAt: number | null;
  message: string;
}

export interface DeviceEvent {
  ts: number;
  type: EventType;
  plantId: string | null;
  code: string;
  ml: number;
  detail: string;
}

// [ts, moisturePct, bottleMl]
export interface History {
  plantId: string;
  from: number;
  to: number;
  points: [number, number | null, number | null][];
}

// [ts, tempC, humidityPct, lux]
export interface EnvHistory {
  from: number;
  to: number;
  points: [number, number | null, number | null, number | null][];
}

export interface Info {
  name: string;
  version: string;
  setupRequired: boolean;
  timeValid: boolean;
  apActive: boolean;
  dashboardEmbedded: boolean;
}

export interface WifiInfo {
  configured: boolean;
  ssid: string;
  mode: string;
  ip: string;
  rssi: number;
  apActive: boolean;
  apSsid: string;
  internet: boolean;
}

export interface WifiNetwork {
  ssid: string;
  rssi: number;
  secure: boolean;
}

export interface ConfigBackup {
  version: string;
  exportedAt: number;
  settings: Settings;
  plants: PlantProfile[];
}

export type DeepPartial<T> = { [K in keyof T]?: T[K] extends object ? DeepPartial<T[K]> : T[K] };
