import { Link } from 'wouter';

import { api } from '../api/client';
import type { Status } from '../api/types';
import { SeverityIcon } from '../components/ConditionChip';
import { Icon } from '../components/Icon';
import { PlantCard } from '../components/PlantCard';
import { ReservoirTank } from '../components/ReservoirTank';
import { needsAttention, plantCondition } from '../lib/condition';
import { formatAgo, formatLux, formatMl, formatPct, formatTemp } from '../lib/format';
import { useAsync, useNow } from '../lib/hooks';
import { useDevice, useStatus } from '../state/device';

function headline(status: Status, attention: string[], count: number) {
  if (status.automation.emergencyStop) {
    return { title: 'Watering is stopped.', sub: 'Nothing will be watered until you resume.' };
  }
  if (status.leak)
    return { title: 'Water on the floor.', sub: 'The leak sensor is wet. All watering has stopped.' };
  if (count === 0) return { title: 'No plants yet.', sub: 'Add your first plant to start watering.' };
  if (status.reservoir.empty) {
    return { title: 'The reservoir is empty.', sub: 'Refill it so watering can continue.' };
  }
  if (attention.length) {
    const names = attention.slice(0, 3).join(', ');
    return {
      title:
        attention.length === 1
          ? `${attention[0]} needs attention.`
          : `${attention.length} of ${count} plants need attention.`,
      sub:
        attention.length === 1
          ? 'Open it to see what is going on.'
          : names + (attention.length > 3 ? ' and more.' : '.'),
    };
  }
  const pouring = status.dose && status.plants.find((p) => p.id === status.dose?.plantId);
  if (pouring) return { title: `Watering ${pouring.name}.`, sub: `Every other plant is within its range.` };
  const closest = status.plants
    .filter((p) => p.moisturePct !== null)
    .sort((a, b) => a.moisturePct! - a.triggerPct - (b.moisturePct! - b.triggerPct))[0];
  return {
    title: count === 1 ? 'Your plant is in its range.' : `All ${count} plants are in range.`,
    sub: closest
      ? `Closest to its next watering: ${closest.name}, ${Math.max(0, Math.round(closest.moisturePct! - closest.triggerPct))}% above where it starts.`
      : '',
  };
}

export function Overview() {
  const status = useStatus();
  const { plants, settings } = useDevice();
  const now = useNow();
  const alertKey = `${status.alerts.critical}-${status.alerts.warning}-${status.automation.emergencyStop}`;
  const alerts = useAsync(() => api.alerts(), alertKey);
  const byId = new Map(plants.map((p) => [p.id, p]));
  const attention = status.plants
    .filter((p) => needsAttention(plantCondition(p, byId.get(p.id))))
    .map((p) => p.name);
  const head = headline(status, attention, status.plants.length);
  const shown = (alerts.data?.alerts ?? [])
    .filter((a) => a.active && !a.acked && a.severity !== 'info')
    .slice(0, 3);
  const res = status.reservoir;
  const env = status.environment;
  const clockNow = status.device.now ?? now;

  return (
    <div className="page">
      <section className="hero">
        <div>
          <h1>{head.title}</h1>
          {head.sub && <p>{head.sub}</p>}
        </div>
      </section>

      {shown.length > 0 && (
        <section className="stack" aria-label="Alerts">
          {shown.map((a) => (
            <div key={a.id} className="notice" data-tone={a.severity}>
              <SeverityIcon severity={a.severity} />
              <div className="grow">
                <p>{a.message}</p>
                <p className="muted small">{formatAgo(a.raisedAt, clockNow)}</p>
              </div>
              <Link href="/alerts" className="btn btn-small btn-ghost">
                Details
              </Link>
            </div>
          ))}
        </section>
      )}

      <div className="split">
        <section aria-label="Plants" className="plant-grid">
          {status.plants.map((p) => (
            <PlantCard key={p.id} status={p} profile={byId.get(p.id)} dose={status.dose} now={clockNow} />
          ))}
          {status.plants.length < 8 && (
            <Link href="/plants/new" className="plant-card add">
              <span>
                <Icon name="plus" size={22} />
                <br />
                Add a plant
              </span>
            </Link>
          )}
        </section>

        <aside className="side">
          <Link href="/climate" className="card" style={{ color: 'inherit', textDecoration: 'none' }}>
            <div className="card-head">
              <h2>Reservoir</h2>
              {res.low && !res.empty && (
                <span className="chip" data-tone="warning">
                  Low
                </span>
              )}
              {res.empty && (
                <span className="chip" data-tone="critical">
                  Empty
                </span>
              )}
            </div>
            <div className="tank-row">
              <ReservoirTank pct={res.pct} lowPct={settings?.reservoir.lowPct ?? 20} empty={res.empty} />
              <div className="tank-figure">
                <strong>{res.ml === null ? '–' : formatMl(res.ml)}</strong>
                <span className="ink-2">
                  {res.pct === null
                    ? 'Scale not available'
                    : `${formatPct(res.pct)} of ${formatMl(res.capacityMl)}`}
                </span>
                <p className="small muted" style={{ marginTop: 4 }}>
                  {res.daysLeft === null
                    ? 'Learning daily use'
                    : `About ${res.daysLeft.toFixed(1)} days left`}
                </p>
              </div>
            </div>
          </Link>

          <section className="card">
            <h2>Room</h2>
            {env.valid || env.lux !== null ? (
              <div className="tiles">
                <div className="tile">
                  <div className="label">Temperature</div>
                  <div className="value">{formatTemp(env.tempC)}</div>
                </div>
                <div className="tile">
                  <div className="label">Humidity</div>
                  <div className="value">{formatPct(env.humidityPct)}</div>
                </div>
                <div className="tile">
                  <div className="label">Light</div>
                  <div className="value">{formatLux(env.lux)}</div>
                </div>
                <div className="tile">
                  <div className="label">Drying power</div>
                  <div className="value">{env.vpdKPa === null ? '–' : `${env.vpdKPa.toFixed(1)} kPa`}</div>
                </div>
              </div>
            ) : (
              <p className="muted">No climate sensor connected.</p>
            )}
          </section>

          <section className="card">
            <h2>System</h2>
            <div className="health">
              <div>
                <span>Automatic watering</span>
                <span>
                  {status.automation.emergencyStop ? 'Stopped' : status.automation.enabled ? 'On' : 'Paused'}
                </span>
              </div>
              <div>
                <span>Watered today</span>
                <span>{formatMl(status.mlTodayTotal)}</span>
              </div>
              <div>
                <span>Wi-Fi</span>
                <span>{status.network.mode === 'ap' ? 'Own hotspot' : status.network.ssid || 'Offline'}</span>
              </div>
              <div>
                <span>Clock</span>
                <span>{status.device.timeValid ? 'Set' : 'Not set'}</span>
              </div>
              <div>
                <span>SD card history</span>
                <span>{status.storage.sdOk ? 'Recording' : 'Missing'}</span>
              </div>
            </div>
          </section>
        </aside>
      </div>
    </div>
  );
}
