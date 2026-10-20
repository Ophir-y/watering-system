import { useState } from 'react';

import { api } from '../api/client';
import type { DeviceEvent, EventType } from '../api/types';
import { Icon, type IconName } from '../components/Icon';
import { formatDateTime, formatMl } from '../lib/format';
import { useAsync, useNow } from '../lib/hooks';
import { useDevice, useStatus } from '../state/device';

const TYPES: { value: EventType | ''; label: string }[] = [
  { value: '', label: 'Everything' },
  { value: 'watering', label: 'Watering' },
  { value: 'alert', label: 'Alerts' },
  { value: 'refill', label: 'Refills' },
  { value: 'system', label: 'System' },
];

const DAYS = [1, 7, 30, 90] as const;

const ICONS: Record<EventType, IconName> = {
  watering: 'drop',
  alert: 'warning',
  alert_cleared: 'check',
  refill: 'refill',
  system: 'info',
};

function summary(e: DeviceEvent, plantName: string | undefined): string {
  if (e.type === 'watering') {
    if (e.code === 'detected') return `${plantName ?? 'A plant'} was watered outside the system`;
    const how =
      e.code === 'manual'
        ? 'by hand'
        : e.code === 'fallback'
          ? 'on the fallback schedule'
          : e.code === 'timed'
            ? 'in an outlet test'
            : e.code === 'interrupted'
              ? 'before a restart'
              : 'automatically';
    return `${plantName ?? 'A plant'} watered ${how}: ${formatMl(e.ml)}`;
  }
  if (e.type === 'refill') return `Reservoir refilled${e.ml ? `: +${formatMl(e.ml)}` : ''}`;
  if (e.type === 'alert_cleared') return `Resolved: ${e.code.replace(/_/g, ' ')}`;
  return e.detail || e.code.replace(/_/g, ' ');
}

export function Activity() {
  const status = useStatus();
  const { plants } = useDevice();
  const realNow = useNow(60000);
  const now = status.device.now ?? realNow;
  const [type, setType] = useState<EventType | ''>('');
  const [days, setDays] = useState<(typeof DAYS)[number]>(7);
  const [plant, setPlant] = useState('');
  const from = now - days * 86400;
  const events = useAsync(
    () => api.events({ from, to: now, type, plant, limit: 300 }),
    `${type}-${days}-${plant}-${Math.floor(now / 120)}`,
  );
  const names = new Map(plants.map((p) => [p.id, p.name]));
  const list = events.data?.events ?? [];

  return (
    <div className="page">
      <div className="page-head">
        <div>
          <h1>Activity</h1>
          <p>Everything the system did, newest first.</p>
        </div>
        <div className="row">
          <a className="btn btn-small" href={api.exportUrl('readings', from, now)} download>
            Moisture CSV
          </a>
          <a className="btn btn-small" href={api.exportUrl('env', from, now)} download>
            Room CSV
          </a>
          <a className="btn btn-small" href={api.exportUrl('events', from, now)} download>
            Activity CSV
          </a>
        </div>
      </div>

      <div className="filters">
        <div className="segmented" role="group" aria-label="Period">
          {DAYS.map((d) => (
            <button key={d} aria-pressed={days === d} onClick={() => setDays(d)}>
              {d === 1 ? '24 h' : `${d} days`}
            </button>
          ))}
        </div>
        <div className="segmented" role="group" aria-label="Kind">
          {TYPES.map((t) => (
            <button key={t.value} aria-pressed={type === t.value} onClick={() => setType(t.value)}>
              {t.label}
            </button>
          ))}
        </div>
        <select
          className="input"
          style={{ width: 'auto' }}
          value={plant}
          onChange={(e) => setPlant(e.target.value)}
          aria-label="Plant"
        >
          <option value="">All plants</option>
          {plants.map((p) => (
            <option key={p.id} value={p.id}>
              {p.name}
            </option>
          ))}
        </select>
      </div>

      <section className="card" style={{ opacity: events.loading && list.length ? 0.6 : 1 }}>
        {list.length === 0 ? (
          <div className="empty">
            <Icon name="list" size={28} />
            <p>{events.loading ? 'Loading…' : 'Nothing happened in this period with these filters.'}</p>
          </div>
        ) : (
          <ul className="event-list">
            {list.map((e, i) => (
              <li key={`${e.ts}-${e.code}-${i}`}>
                <span className="event-icon" data-type={e.type}>
                  <Icon name={ICONS[e.type]} size={15} />
                </span>
                <div style={{ minWidth: 0 }}>
                  <p>{summary(e, e.plantId ? names.get(e.plantId) : undefined)}</p>
                  {e.type === 'watering' && e.detail && <p className="small muted">{e.detail}</p>}
                  {e.type === 'alert' && <p className="small muted">{e.detail}</p>}
                </div>
                <span className="when">{formatDateTime(e.ts)}</span>
              </li>
            ))}
          </ul>
        )}
      </section>
    </div>
  );
}
