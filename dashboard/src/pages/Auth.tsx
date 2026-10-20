import { useState, type FormEvent, type ReactNode } from 'react';

import { ApiError, api, setToken } from '../api/client';
import type { WifiNetwork } from '../api/types';
import { TextField } from '../components/Fields';
import { browserTimezone, zoneFor } from '../data/timezones';
import { useDevice } from '../state/device';
import { messageFor } from '../state/toast';

function Frame({ children }: { children: ReactNode }) {
  return (
    <div className="auth">
      <div className="card">
        <div className="brand">
          <img src="/favicon.svg" width="32" height="32" alt="" />
          <strong>PlantCare</strong>
        </div>
        {children}
      </div>
    </div>
  );
}

export function Login() {
  const { signIn, info } = useDevice();
  const [password, setPassword] = useState('');
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  const submit = async (e: FormEvent) => {
    e.preventDefault();
    setBusy(true);
    setError(null);
    try {
      const { token } = await api.login(password);
      signIn(token);
    } catch (err) {
      setError(
        err instanceof ApiError && err.status === 401 ? 'That password is not right.' : messageFor(err),
      );
    } finally {
      setBusy(false);
    }
  };

  return (
    <Frame>
      <div>
        <h1>Sign in to {info?.name ?? 'PlantCare'}</h1>
        <p className="ink-2" style={{ marginTop: 4 }}>
          Use the password you chose when you set the device up.
        </p>
      </div>
      <form className="stack" onSubmit={submit}>
        <TextField
          label="Password"
          type="password"
          value={password}
          onChange={setPassword}
          autoComplete="current-password"
          error={error}
        />
        <button className="btn btn-water" disabled={busy || !password}>
          Sign in
        </button>
      </form>
    </Frame>
  );
}

export function Offline() {
  const { retry } = useDevice();
  return (
    <Frame>
      <div className="stack">
        <h1>Can&apos;t reach the device</h1>
        <p className="ink-2">
          Make sure this phone or computer is on the same Wi-Fi as PlantCare. If the device can&apos;t find
          your network it opens its own hotspot called <strong>PlantCare-…</strong> after ten minutes: join it
          and open <code>http://192.168.4.1</code>.
        </p>
        <p className="ink-2">Watering carries on as normal while you are disconnected.</p>
        <button className="btn btn-water" onClick={retry}>
          Try again
        </button>
      </div>
    </Frame>
  );
}

export function Setup() {
  const { signIn, info } = useDevice();
  const zone = zoneFor(browserTimezone());
  const [step, setStep] = useState<1 | 2 | 3>(1);
  const [name, setName] = useState('PlantCare');
  const [password, setPassword] = useState('');
  const [confirm, setConfirm] = useState('');
  const [error, setError] = useState<string | null>(null);
  const [token, setLocalToken] = useState('');
  const [networks, setNetworks] = useState<WifiNetwork[]>([]);
  const [ssid, setSsid] = useState('');
  const [wifiPassword, setWifiPassword] = useState('');
  const [busy, setBusy] = useState(false);

  const createAccount = async (e: FormEvent) => {
    e.preventDefault();
    if (password.length < 8) return setError('Use at least 8 characters.');
    if (password !== confirm) return setError('The two passwords differ.');
    setBusy(true);
    setError(null);
    try {
      const res = await api.setup(
        password,
        {
          deviceName: name.trim() || 'PlantCare',
          tzName: zone.tzName,
          timezone: zone.timezone,
          southernHemisphere: zone.southernHemisphere,
        },
        Math.floor(Date.now() / 1000),
      );
      setToken(res.token);
      setLocalToken(res.token);
      setStep(info?.apActive ? 2 : 3);
    } catch (err) {
      setError(messageFor(err));
    } finally {
      setBusy(false);
    }
  };

  const scan = async () => {
    setBusy(true);
    try {
      await api.startWifiScan();
      for (let i = 0; i < 12; i++) {
        await new Promise((r) => setTimeout(r, 1000));
        const r = await api.wifiScan();
        setNetworks(r.networks);
        if (!r.scanning) break;
      }
    } catch (err) {
      setError(messageFor(err));
    } finally {
      setBusy(false);
    }
  };

  const joinWifi = async (e: FormEvent) => {
    e.preventDefault();
    setBusy(true);
    try {
      await api.setWifi(ssid, wifiPassword);
      setStep(3);
    } catch (err) {
      setError(messageFor(err));
    } finally {
      setBusy(false);
    }
  };

  return (
    <Frame>
      <div className="steps" aria-hidden>
        {[1, 2, 3].map((s) => (
          <span key={s} data-done={step >= s} />
        ))}
      </div>
      {step === 1 && (
        <form className="stack" onSubmit={createAccount}>
          <div>
            <h1>Set up PlantCare</h1>
            <p className="ink-2" style={{ marginTop: 4 }}>
              Choose a password for this device. Anyone on your network needs it to change anything.
            </p>
          </div>
          <TextField label="Device name" value={name} onChange={setName} maxLength={32} />
          <TextField
            label="Password"
            type="password"
            value={password}
            onChange={setPassword}
            autoComplete="new-password"
            hint="At least 8 characters."
          />
          <TextField
            label="Repeat password"
            type="password"
            value={confirm}
            onChange={setConfirm}
            autoComplete="new-password"
            error={error}
          />
          <p className="small muted">
            Time zone: {zone.tzName}
            {zone.exact ? '' : ' (fixed offset; pick your zone in Settings later for daylight saving)'}. The
            clock is set from this browser.
          </p>
          <button className="btn btn-water" disabled={busy}>
            Continue
          </button>
        </form>
      )}
      {step === 2 && (
        <form className="stack" onSubmit={joinWifi}>
          <div>
            <h1>Connect to your Wi-Fi</h1>
            <p className="ink-2" style={{ marginTop: 4 }}>
              You are on the device&apos;s own hotspot. Join your home network so you can reach it from
              anywhere in the house and get notifications.
            </p>
          </div>
          <button
            type="button"
            className="btn btn-small"
            style={{ width: 'max-content' }}
            onClick={scan}
            disabled={busy}
          >
            {busy ? 'Scanning…' : 'Find networks'}
          </button>
          {networks.length > 0 && (
            <div className="species-list" style={{ maxHeight: 180 }}>
              {networks.map((n) => (
                <button
                  key={n.ssid}
                  type="button"
                  aria-pressed={ssid === n.ssid}
                  onClick={() => setSsid(n.ssid)}
                >
                  <span>{n.ssid}</span>
                  <span className="light">{n.rssi} dBm</span>
                  <span className="latin">{n.secure ? 'Secured' : 'Open'}</span>
                </button>
              ))}
            </div>
          )}
          <TextField label="Network name" value={ssid} onChange={setSsid} maxLength={32} />
          <TextField
            label="Wi-Fi password"
            type="password"
            value={wifiPassword}
            onChange={setWifiPassword}
            error={error}
          />
          <div className="row">
            <button className="btn btn-water" disabled={busy || !ssid}>
              Join network
            </button>
            <button type="button" className="btn btn-ghost" onClick={() => setStep(3)}>
              Skip for now
            </button>
          </div>
        </form>
      )}
      {step === 3 && (
        <div className="stack">
          <h1>Ready</h1>
          <p className="ink-2">
            {ssid
              ? `The device is joining ${ssid}. Reconnect this phone to that network, then open http://plantcare.local.`
              : 'Next, add your first plant and calibrate its sensor.'}
          </p>
          <button
            className="btn btn-water"
            onClick={() => {
              window.location.hash = '#/plants/new';
              signIn(token);
            }}
          >
            Add your first plant
          </button>
        </div>
      )}
    </Frame>
  );
}
