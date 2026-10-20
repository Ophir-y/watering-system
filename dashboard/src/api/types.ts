// Mirrors the firmware's JSON (firmware/lib/plantcore/src/Json.cpp). The shapes are
// checked against shared/api-fixtures by src/api/contract.test.ts.

export type Severity = 'info' | 'warning' | 'critical';
export type OutletType = 'pump' | 'valve';
export type WateringStyle = 'evenly_moist' | 'dry_between' | 'soak_and_dry';
export type SensorFaultPolicy = 'pause' | 'fallback_schedule';
export type SensorHealth = 'ok' | 'suspect' | 'failed';
export type SensorIssue =
  'ok' | 'uncalibrated' | 'bus_error' | 'disconnected' | 'out_of_range' | 'no_response' | 'stuck' | 'erratic';
export type PlannerState = 'idle' | 'waiting' | 'watering' | 'soaking';
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
  channel: number;
  outlet: { type: OutletType; flowMlPerS: number; calFillPct: number; dropMm: number };
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
  quietHours: { startH: number; endH: number };
  sampleIntervalS: number;
  logIntervalMin: number;
  automationEnabled: boolean;
  buzzerEnabled: boolean;
  leakSensorEnabled: boolean;
  limits: {
    hardMaxPulseMl: number;
    hardMaxDailyMlPerPlant: number;
    hardMaxDailyMlTotal: number;
    hardMaxOnTimeS: number;
  };
  reservoir: {
    scaleEnabled: boolean;
    floatSwitchEnabled: boolean;
    capacityMl: number;
    fullHeightMm: number;
    lowPct: number;
    emptyMl: number;
    lowDaysLeft: number;
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

export interface PlantStatus {
  id: string;
  name: string;
  channel: number;
  moisturePct: number | null;
  raw: number;
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
  leak: boolean;
  reservoir: {
    scaleValid: boolean;
    ml: number | null;
    pct: number | null;
    capacityMl: number;
    daysLeft: number | null;
    low: boolean;
    empty: boolean;
    floatLow: boolean;
    scaleCalibrated: boolean;
    scaleRaw: number;
  };
  environment: {
    valid: boolean;
    tempC: number | null;
    humidityPct: number | null;
    vpdKPa: number | null;
    lux: number | null;
  };
  dose: { plantId: string; channel: number; kind: SessionKind; ml: number; dispensedMl: number } | null;
  lastDose: {
    plantId: string;
    channel: number;
    ml: number;
    onTimeMs: number;
    closedLoop: boolean;
    at: number;
  } | null;
  mlTodayTotal: number;
  plants: PlantStatus[];
  alerts: { critical: number; warning: number; info: number };
}

export interface Alert {
  id: number;
  code: string;
  severity: Severity;
  channel: number | null;
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

export interface History {
  plantId: string;
  from: number;
  to: number;
  points: [number, number | null][];
}

// [ts, tempC, humidityPct, lux, reservoirMl]
export interface EnvHistory {
  from: number;
  to: number;
  points: [number, number | null, number | null, number | null, number | null][];
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
