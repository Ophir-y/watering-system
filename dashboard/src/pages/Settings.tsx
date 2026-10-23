import { useState } from 'react';

import { api, uploadFirmware } from '../api/client';
import type { ConfigBackup, Settings as SettingsT, SettingsPatch, WifiNetwork } from '../api/types';
import { NumberField, SelectField, TextField, Toggle } from '../components/Fields';
import { TIMEZONES, browserTimezone, zoneFor } from '../data/timezones';
import { formatDateTime } from '../lib/format';
import { useNow } from '../lib/hooks';
import { useDevice, useStatus } from '../state/device';
import { useToast } from '../state/toast';
import { getThemeChoice, setThemeChoice, type ThemeChoice } from '../theme';

const HOURS = Array.from({ length: 24 }, (_, h) => ({ value: h, label: `${String(h).padStart(2, '0')}:00` }));

export function Settings() {
  const { settings } = useDevice();
  if (!settings) return <div className="page">Loading…</div>;
  return (
    <div className="page">
      <div className="page-head">
        <div>
          <h1>Settings</h1>
          <p>Saved on the device; they apply immediately.</p>
        </div>
      </div>
      <GeneralSettings initial={settings} />
      <div className="two-col">
        <WifiSettings />
        <AccountSettings />
      </div>
      <MaintenanceSettings />
    </div>
  );
}

function GeneralSettings({ initial }: { initial: SettingsT }) {
  const { reloadSettings, refresh } = useDevice();
  const toast = useToast();
  const [s, setS] = useState<SettingsT>(() => structuredClone(initial));
  const [token, setToken] = useState('');
  const [saving, setSaving] = useState(false);
  const [testResult, setTestResult] = useState<string | null>(null);
  const dirty = JSON.stringify(s) !== JSON.stringify(initial) || token !== '';

  const up = <K extends keyof SettingsT>(k: K, v: SettingsT[K]) => setS((x) => ({ ...x, [k]: v }));
  const zones = Object.keys(TIMEZONES).includes(s.tzName)
    ? Object.keys(TIMEZONES)
    : [s.tzName, ...Object.keys(TIMEZONES)];

  const save = async () => {
    setSaving(true);
    try {
      const { tokenSet: _t, ...notify } = s.notify;
      const patch: SettingsPatch = { ...s, notify: { ...notify, ...(token ? { token } : {}) } };
      await api.saveSettings(patch);
      setToken('');
      await Promise.all([reloadSettings(), refresh()]);
      toast.show('Settings saved');
    } catch (e) {
      toast.error(e);
    } finally {
      setSaving(false);
    }
  };

  const sendTest = async () => {
    setTestResult('Sending…');
    try {
      await api.testNotification();
      for (let i = 0; i < 10; i++) {
        await new Promise((r) => setTimeout(r, 1500));
        const r = await api.notificationResult();
        if (r.at && r.at >= Math.floor(Date.now() / 1000) - 30) {
          setTestResult(r.result === 'ok' ? 'Sent. Check your phone.' : `Not sent: ${r.result}`);
          return;
        }
      }
      setTestResult('No answer from the device yet.');
    } catch (e) {
      setTestResult(e instanceof Error ? e.message : String(e));
    }
  };

  const randomTopic = () => {
    const bytes = crypto.getRandomValues(new Uint8Array(6));
    up('notify', {
      ...s.notify,
      topic: `plantcare-${Array.from(bytes, (b) => b.toString(16).padStart(2, '0')).join('')}`,
    });
  };

  return (
    <div className="form">
      <section className="card form-section">
        <h2>Device and time</h2>
        <div className="fields">
          <TextField
            label="Device name"
            value={s.deviceName}
            onChange={(v) => up('deviceName', v)}
            maxLength={32}
          />
          <SelectField
            label="Time zone"
            value={s.tzName}
            options={zones.map((z) => ({ value: z, label: z.replace(/_/g, ' ') }))}
            onChange={(z) => {
              const choice = zoneFor(z);
              setS((x) => ({
                ...x,
                tzName: z,
                timezone: choice.timezone,
                southernHemisphere: choice.southernHemisphere,
              }));
            }}
            hint={
              <button
                type="button"
                className="btn btn-ghost btn-small"
                style={{ padding: 0, minHeight: 0 }}
                onClick={() => {
                  const choice = zoneFor(browserTimezone());
                  setS((x) => ({
                    ...x,
                    tzName: choice.tzName,
                    timezone: choice.timezone,
                    southernHemisphere: choice.southernHemisphere,
                  }));
                }}
              >
                Use this browser&apos;s ({browserTimezone()})
              </button>
            }
          />
        </div>
        <Toggle
          label="Southern hemisphere"
          checked={s.southernHemisphere}
          onChange={(v) => up('southernHemisphere', v)}
          hint="Shifts each plant's resting months by half a year."
        />
      </section>

      <section className="card form-section">
        <h2>Watering</h2>
        <Toggle
          label="Water automatically"
          checked={s.automationEnabled}
          onChange={(v) => up('automationEnabled', v)}
          hint="When off, plants are only watered when you press Water now."
        />
        <div className="fields">
          <SelectField
            label="Water from"
            value={s.waterWindow.startH}
            options={HOURS}
            onChange={(v) => up('waterWindow', { ...s.waterWindow, startH: v })}
          />
          <SelectField
            label="Until"
            value={s.waterWindow.endH}
            options={HOURS}
            onChange={(v) => up('waterWindow', { ...s.waterWindow, endH: v })}
            hint="A plant that gets very dry is watered at any hour."
          />
          <NumberField
            label="Nodes check in every"
            value={s.checkInMin}
            unit="min"
            min={1}
            max={60}
            onChange={(v) => up('checkInMin', v)}
            hint="How often each plant's node wakes, reads its soil and reports. Longer saves batteries; Water now waits for the next check-in."
          />
          <NumberField
            label="Save history every"
            value={s.logIntervalMin}
            unit="min"
            onChange={(v) => up('logIntervalMin', v)}
          />
        </div>
      </section>

      <section className="card form-section">
        <h2>Safety limits</h2>
        <p>
          Hard caps that apply to every plant, including manual watering. Per-plant limits can only be lower.
        </p>
        <div className="fields">
          <NumberField
            label="Largest single dose"
            value={s.limits.hardMaxPulseMl}
            unit="ml"
            step={10}
            onChange={(v) => up('limits', { ...s.limits, hardMaxPulseMl: v })}
          />
          <NumberField
            label="Most per plant per day"
            value={s.limits.hardMaxDailyMlPerPlant}
            unit="ml"
            step={50}
            onChange={(v) => up('limits', { ...s.limits, hardMaxDailyMlPerPlant: v })}
          />
          <NumberField
            label="Longest a pump runs"
            value={s.limits.hardMaxOnTimeS}
            unit="s"
            onChange={(v) => up('limits', { ...s.limits, hardMaxOnTimeS: v })}
            hint="A hardware timer on each node enforces this independently of its software."
          />
        </div>
        <p className="small muted">
          Each plant also has its own bottle: the most it can ever get is what is in it.
        </p>
      </section>

      <section className="card form-section">
        <h2>Phone notifications</h2>
        <p>
          Sent through{' '}
          <a href="https://ntfy.sh" target="_blank" rel="noreferrer">
            ntfy
          </a>
          : install the app and subscribe to the topic below. Anyone who knows the topic can read the
          messages, so keep it hard to guess.
        </p>
        <Toggle
          label="Send notifications"
          checked={s.notify.enabled}
          onChange={(v) => up('notify', { ...s.notify, enabled: v })}
        />
        <div className="fields">
          <TextField
            label="Server"
            value={s.notify.server}
            onChange={(v) => up('notify', { ...s.notify, server: v })}
          />
          <TextField
            label="Topic"
            value={s.notify.topic}
            onChange={(v) => up('notify', { ...s.notify, topic: v })}
            hint={
              <button
                type="button"
                className="btn btn-ghost btn-small"
                style={{ padding: 0, minHeight: 0 }}
                onClick={randomTopic}
              >
                Generate a random topic
              </button>
            }
          />
          <TextField
            label="Access token"
            type="password"
            value={token}
            onChange={setToken}
            placeholder={s.notify.tokenSet ? 'Saved (leave blank to keep)' : 'Optional'}
            autoComplete="off"
          />
          <SelectField
            label="Notify me about"
            value={s.notify.minSeverity}
            onChange={(v) => up('notify', { ...s.notify, minSeverity: v })}
            options={[
              { value: 'critical', label: 'Critical problems only' },
              { value: 'warning', label: 'Warnings and critical' },
              { value: 'info', label: 'Everything, including notes' },
            ]}
          />
        </div>
        <div className="row">
          <button className="btn btn-small" onClick={sendTest} disabled={dirty}>
            Send a test notification
          </button>
          <span className="small ink-2">{dirty ? 'Save your changes first.' : testResult}</span>
        </div>
      </section>

      <div className="form-actions">
        <button
          className="btn btn-ghost"
          disabled={!dirty}
          onClick={() => {
            setS(structuredClone(initial));
            setToken('');
          }}
        >
          Discard changes
        </button>
        <button className="btn btn-water" disabled={!dirty || saving} onClick={save}>
          Save changes
        </button>
      </div>
    </div>
  );
}

function WifiSettings() {
  const status = useStatus();
  const toast = useToast();
  const [networks, setNetworks] = useState<WifiNetwork[]>([]);
  const [scanning, setScanning] = useState(false);
  const [ssid, setSsid] = useState(status.network.ssid);
  const [password, setPassword] = useState('');
  const [apPassword, setApPassword] = useState('');

  const scan = async () => {
    setScanning(true);
    try {
      await api.startWifiScan();
      for (let i = 0; i < 12; i++) {
        await new Promise((r) => setTimeout(r, 1000));
        const r = await api.wifiScan();
        setNetworks(r.networks);
        if (!r.scanning) break;
      }
    } catch (e) {
      toast.error(e);
    } finally {
      setScanning(false);
    }
  };

  const save = async () => {
    try {
      await api.setWifi(ssid, password, apPassword || undefined);
      toast.show(`Joining ${ssid}. If this page stops responding, reconnect your device to that network.`);
      setPassword('');
    } catch (e) {
      toast.error(e);
    }
  };

  const net = status.network;
  return (
    <section className="card form-section">
      <h2>Wi-Fi</h2>
      <p>
        {net.mode === 'ap' || net.mode === 'sta+ap' ? `Hotspot “${net.apSsid}” is on. ` : ''}
        {net.ssid && net.mode !== 'ap'
          ? `Connected to ${net.ssid} (${net.ip}, signal ${net.rssi} dBm).`
          : 'Not connected to a network.'}
      </p>
      <div className="row">
        <button className="btn btn-small" onClick={scan} disabled={scanning}>
          {scanning ? 'Scanning…' : 'Find networks'}
        </button>
      </div>
      {networks.length > 0 && (
        <div className="species-list" style={{ maxHeight: 180 }}>
          {networks.map((n) => (
            <button key={n.ssid} type="button" aria-pressed={ssid === n.ssid} onClick={() => setSsid(n.ssid)}>
              <span>{n.ssid}</span>
              <span className="light">{n.rssi} dBm</span>
              <span className="latin">{n.secure ? 'Secured' : 'Open'}</span>
            </button>
          ))}
        </div>
      )}
      <div className="fields">
        <TextField label="Network name" value={ssid} onChange={setSsid} maxLength={32} />
        <TextField
          label="Password"
          type="password"
          value={password}
          onChange={setPassword}
          autoComplete="off"
        />
        <TextField
          label="Hotspot password"
          type="password"
          value={apPassword}
          onChange={setApPassword}
          placeholder="Unchanged"
          hint="Used when the device runs its own hotspot. At least 8 characters."
        />
      </div>
      <button className="btn" style={{ width: 'max-content' }} disabled={!ssid} onClick={save}>
        Join network
      </button>
    </section>
  );
}

function AccountSettings() {
  const toast = useToast();
  const { signIn, signOut } = useDevice();
  const [current, setCurrent] = useState('');
  const [next, setNext] = useState('');
  const [theme, setTheme] = useState<ThemeChoice>(getThemeChoice());

  const change = async () => {
    try {
      const { token } = await api.changePassword(current, next);
      setCurrent('');
      setNext('');
      signIn(token);
      toast.show('Password changed. Other devices are signed out.');
    } catch (e) {
      toast.error(e);
    }
  };

  return (
    <section className="card form-section">
      <h2>Account and display</h2>
      <div className="fields">
        <TextField
          label="Current password"
          type="password"
          value={current}
          onChange={setCurrent}
          autoComplete="current-password"
        />
        <TextField
          label="New password"
          type="password"
          value={next}
          onChange={setNext}
          autoComplete="new-password"
          hint="At least 8 characters."
        />
      </div>
      <div className="row">
        <button className="btn" disabled={!current || next.length < 8} onClick={change}>
          Change password
        </button>
        <button className="btn btn-ghost" onClick={signOut}>
          Sign out
        </button>
      </div>
      <hr className="hairline" />
      <div className="segmented" role="group" aria-label="Appearance" style={{ width: 'max-content' }}>
        {(['auto', 'light', 'dark'] as ThemeChoice[]).map((t) => (
          <button
            key={t}
            aria-pressed={theme === t}
            onClick={() => {
              setThemeChoice(t);
              setTheme(t);
            }}
          >
            {t === 'auto' ? 'Match system' : t === 'light' ? 'Light' : 'Dark'}
          </button>
        ))}
      </div>
    </section>
  );
}

function MaintenanceSettings() {
  const status = useStatus();
  const { reloadPlants, reloadSettings, refresh } = useDevice();
  const toast = useToast();
  const [progress, setProgress] = useState<number | null>(null);
  const browserNow = useNow(10000);
  const clockOffset = status.device.now ? status.device.now - browserNow : null;

  const downloadBackup = async () => {
    try {
      const b = await api.backup();
      const blob = new Blob([JSON.stringify(b, null, 2)], { type: 'application/json' });
      const a = document.createElement('a');
      a.href = URL.createObjectURL(blob);
      a.download = `plantcare-backup-${new Date().toISOString().slice(0, 10)}.json`;
      a.click();
      URL.revokeObjectURL(a.href);
    } catch (e) {
      toast.error(e);
    }
  };

  const restore = async (file: File) => {
    try {
      const b = JSON.parse(await file.text()) as ConfigBackup;
      if (!b.settings || !Array.isArray(b.plants)) throw new Error('That file is not a PlantCare backup.');
      await api.restore({ settings: b.settings, plants: b.plants });
      await Promise.all([reloadPlants(), reloadSettings(), refresh()]);
      toast.show(`Restored ${b.plants.length} plants and all settings`);
    } catch (e) {
      toast.error(e);
    }
  };

  const flash = async (file: File) => {
    setProgress(0);
    try {
      await uploadFirmware(file, setProgress);
      toast.show('Firmware installed. The device restarts and comes back in about 20 seconds.');
    } catch (e) {
      toast.error(e);
    } finally {
      setProgress(null);
    }
  };

  const act = async (fn: () => Promise<unknown>, msg: string) => {
    try {
      await fn();
      toast.show(msg);
      await refresh();
    } catch (e) {
      toast.error(e);
    }
  };

  return (
    <section className="card form-section">
      <h2>Maintenance</h2>
      <div className="two-col">
        <div className="stack">
          <h3>Firmware</h3>
          <p className="small ink-2">
            Version {status.device.version}. Last restart: {status.device.resetReason.replace(/_/g, ' ')}.
            Upload a <code>firmware.bin</code> built with <code>make all</code>. If the new version fails to
            start, the device rolls back by itself.
          </p>
          <label className="btn" style={{ width: 'max-content' }}>
            {progress === null ? 'Upload firmware…' : `Uploading ${Math.round(progress * 100)}%`}
            <input
              type="file"
              accept=".bin"
              hidden
              disabled={progress !== null}
              onChange={(e) => e.target.files?.[0] && flash(e.target.files[0])}
            />
          </label>
          <button
            className="btn btn-ghost"
            style={{ width: 'max-content' }}
            onClick={() => act(api.reboot, 'Restarting…')}
          >
            Restart device
          </button>
        </div>
        <div className="stack">
          <h3>Backup</h3>
          <p className="small ink-2">Plants and settings as a file. History stays on the SD card.</p>
          <div className="row">
            <button className="btn" onClick={downloadBackup}>
              Download backup
            </button>
            <label className="btn btn-ghost">
              Restore from file…
              <input
                type="file"
                accept="application/json,.json"
                hidden
                onChange={(e) => e.target.files?.[0] && restore(e.target.files[0])}
              />
            </label>
          </div>
          <h3 style={{ marginTop: 8 }}>Clock</h3>
          <p className="small ink-2">
            {status.device.now
              ? `Device time ${formatDateTime(status.device.now)}`
              : 'The device clock is not set.'}
            {clockOffset !== null && Math.abs(clockOffset) > 120
              ? ` · ${Math.round(Math.abs(clockOffset) / 60)} min off from this browser`
              : ''}
          </p>
          <button
            className="btn btn-ghost"
            style={{ width: 'max-content' }}
            onClick={() =>
              act(() => api.setTime(Math.floor(Date.now() / 1000)), 'Clock set from this browser')
            }
          >
            Set clock from this browser
          </button>
        </div>
      </div>
    </section>
  );
}
