import { transport } from './transport';
import type {
  Alert,
  ConfigBackup,
  DeviceEvent,
  EnvHistory,
  EventType,
  History,
  Info,
  PlantProfile,
  Queued,
  Settings,
  SettingsPatch,
  Status,
  WifiInfo,
  WifiNetwork,
} from './types';

const TOKEN_KEY = 'plantcare.token';

export class ApiError extends Error {
  readonly status: number;
  readonly code: string;
  constructor(status: number, code: string, message: string) {
    super(message);
    this.status = status;
    this.code = code;
  }
}

let memoryToken: string | null = null;

export function getToken(): string | null {
  try {
    return localStorage.getItem(TOKEN_KEY) ?? memoryToken;
  } catch {
    return memoryToken;
  }
}

export function setToken(token: string | null) {
  memoryToken = token;
  try {
    if (token) localStorage.setItem(TOKEN_KEY, token);
    else localStorage.removeItem(TOKEN_KEY);
  } catch {
    /* storage unavailable: keep it in memory */
  }
}

const unauthorizedListeners = new Set<() => void>();
export function onUnauthorized(fn: () => void) {
  unauthorizedListeners.add(fn);
  return () => {
    unauthorizedListeners.delete(fn);
  };
}

async function request<T>(method: string, path: string, body?: unknown): Promise<T> {
  let res;
  try {
    res = await transport().request(method, path, body, getToken());
  } catch {
    throw new ApiError(0, 'offline', 'Cannot reach the device. Check that you are on the same network.');
  }
  if (res.status >= 200 && res.status < 300) return res.body as T;
  const err = (res.body ?? {}) as { error?: string; message?: string };
  if (res.status === 401 && path !== '/api/login') unauthorizedListeners.forEach((fn) => fn());
  throw new ApiError(res.status, err.error ?? 'error', err.message ?? `Request failed (${res.status})`);
}

const q = (params: Record<string, string | number | undefined>) => {
  const parts = Object.entries(params)
    .filter(([, v]) => v !== undefined && v !== '')
    .map(([k, v]) => `${k}=${encodeURIComponent(String(v))}`);
  return parts.length ? `?${parts.join('&')}` : '';
};

const id = encodeURIComponent;

export const api = {
  info: () => request<Info>('GET', '/api/info'),
  setup: (password: string, settings: SettingsPatch, now: number) =>
    request<{ token: string }>('POST', '/api/setup', { password, settings, now }),
  login: (password: string) => request<{ token: string }>('POST', '/api/login', { password }),
  logout: () => request<{ ok: true }>('POST', '/api/logout'),
  changePassword: (current: string, next: string) =>
    request<{ token: string }>('POST', '/api/account/password', { current, next }),

  status: () => request<Status>('GET', '/api/status'),

  plants: () => request<PlantProfile[]>('GET', '/api/plants'),
  plant: (plantId: string) => request<PlantProfile>('GET', `/api/plants/${id(plantId)}`),
  createPlant: (p: Partial<PlantProfile>) => request<PlantProfile>('POST', '/api/plants', p),
  updatePlant: (plantId: string, p: Partial<PlantProfile>) =>
    request<PlantProfile>('PUT', `/api/plants/${id(plantId)}`, p),
  deletePlant: (plantId: string) => request<{ ok: true }>('DELETE', `/api/plants/${id(plantId)}`),
  // Plant commands are queued and carried out when the plant's node next checks in.
  water: (plantId: string, ml: number) => request<Queued>('POST', `/api/plants/${id(plantId)}/water`, { ml }),
  timedRun: (plantId: string, seconds: number) =>
    request<Queued>('POST', `/api/plants/${id(plantId)}/timed-run`, { seconds }),
  refilled: (plantId: string, ml = 0) =>
    request<Queued>('POST', `/api/plants/${id(plantId)}/refilled`, { ml }),
  clearFaults: (plantId: string) => request<Queued>('POST', `/api/plants/${id(plantId)}/clear-faults`),
  identify: (plantId: string) => request<Queued>('POST', `/api/plants/${id(plantId)}/identify`),
  // Uses the node's latest reading (ageS seconds old).
  calibrate: (plantId: string, point: 'dry' | 'wet') =>
    request<{ raw: number; ageS: number; calibration: { rawDry: number; rawWet: number } }>(
      'POST',
      `/api/plants/${id(plantId)}/calibrate`,
      { point },
    ),
  repairNode: (nodeId: string) => request<{ ok: true }>('POST', `/api/nodes/${id(nodeId)}/repair`),

  history: (plantId: string, from: number, to: number, points = 200) =>
    request<History>('GET', `/api/history${q({ plant: plantId, from, to, points })}`),
  envHistory: (from: number, to: number, points = 200) =>
    request<EnvHistory>('GET', `/api/env/history${q({ from, to, points })}`),
  events: (
    opts: { from?: number; to?: number; type?: EventType | ''; plant?: string; limit?: number } = {},
  ) => request<{ events: DeviceEvent[] }>('GET', `/api/events${q(opts)}`),

  alerts: () => request<{ alerts: Alert[] }>('GET', '/api/alerts'),
  ackAlert: (alertId: number) => request<{ ok: true }>('POST', `/api/alerts/${alertId}/ack`),
  ackAllAlerts: () => request<{ ok: true }>('POST', '/api/alerts/ack-all'),

  settings: () => request<Settings>('GET', '/api/settings'),
  saveSettings: (patch: SettingsPatch) => request<Settings>('PUT', '/api/settings', patch),

  stopAll: (reason?: string) => request<{ ok: true }>('POST', '/api/system/stop', { reason }),
  resume: () => request<{ ok: true }>('POST', '/api/system/resume'),
  setAutomation: (enabled: boolean) => request<{ ok: true }>('POST', '/api/system/automation', { enabled }),
  reboot: () => request<{ ok: true }>('POST', '/api/system/reboot'),
  setTime: (epoch: number) => request<{ ok: true }>('POST', '/api/system/time', { epoch }),

  backup: () => request<ConfigBackup>('GET', '/api/config/backup'),
  restore: (backup: Pick<ConfigBackup, 'settings' | 'plants'>) =>
    request<{ ok: true }>('POST', '/api/config/restore', backup),

  wifi: () => request<WifiInfo>('GET', '/api/wifi'),
  startWifiScan: () => request<{ ok: true }>('POST', '/api/wifi/scan'),
  wifiScan: () => request<{ scanning: boolean; networks: WifiNetwork[] }>('GET', '/api/wifi/scan'),
  setWifi: (ssid: string, password: string, apPassword?: string) =>
    request<{ ok: true }>('POST', '/api/wifi', { ssid, password, apPassword }),

  testNotification: () => request<{ ok: true }>('POST', '/api/notify/test'),
  notificationResult: () => request<{ result: string; at: number }>('GET', '/api/notify/test'),

  exportUrl: (kind: 'readings' | 'env' | 'events', from: number, to: number) =>
    `/api/export.csv${q({ kind, from, to, token: getToken() ?? '' })}`,
};

// Firmware upload with progress (fetch cannot report upload progress).
export function uploadFirmware(file: File, onProgress: (fraction: number) => void): Promise<void> {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/system/ota');
    const token = getToken();
    if (token) xhr.setRequestHeader('Authorization', `Bearer ${token}`);
    xhr.upload.onprogress = (e) => e.lengthComputable && onProgress(e.loaded / e.total);
    xhr.onload = () => {
      if (xhr.status >= 200 && xhr.status < 300) return resolve();
      let message = `Upload failed (${xhr.status})`;
      try {
        message = JSON.parse(xhr.responseText).message ?? message;
      } catch {
        /* keep the generic message */
      }
      reject(new ApiError(xhr.status, 'update_failed', message));
    };
    xhr.onerror = () => reject(new ApiError(0, 'offline', 'The upload was interrupted.'));
    const form = new FormData();
    form.append('firmware', file, file.name);
    xhr.send(form);
  });
}
