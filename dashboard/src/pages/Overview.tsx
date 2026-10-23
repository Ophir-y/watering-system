import { Link } from 'wouter';

import { api } from '../api/client';
import type { PlantProfile, Status } from '../api/types';
import { BottleGauge } from '../components/BottleGauge';
import { SeverityIcon } from '../components/ConditionChip';
import { Icon } from '../components/Icon';
import { PlantCard } from '../components/PlantCard';
import { needsAttention, plantCondition } from '../lib/condition';
import { formatAgo, formatLux, formatMl, formatPct, formatTemp } from '../lib/format';
import { useAsync, useNow } from '../lib/hooks';
import { useDevice, useStatus } from '../state/device';

const MAX_PLANTS = 16;

function headline(status: Status, attention: string[], count: number) {
  if (status.automation.emergencyStop) {
    return { title: 'Watering is stopped.', sub: 'No plant is watered until you resume.' };
  }
  if (count === 0) {
    return status.newNodes.length
      ? { title: 'A plant node is waiting.', sub: 'Add it as a plant to start watering.' }
      : { title: 'No plants yet.', sub: 'Put batteries in a plant node, then add it here.' };
  }
  const empty = status.plants.filter((p) => p.bottle?.empty);
  if (empty.length) {
    return {
      title:
        empty.length === 1 ? `${empty[0].name}'s bottle is empty.` : `${empty.length} bottles are empty.`,
      sub: 'Refill, then press the button on the node (or tap Refilled on the plant).',
    };
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
  const pouring = status.plants.find((p) => p.dosing);
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

function Bottles({ status, byId }: { status: Status; byId: Map<string, PlantProfile> }) {
  const withBottle = status.plants.filter((p) => p.bottle);
  const toRefill = withBottle.filter((p) => p.bottle!.empty || p.bottle!.low);
  return (
    <section className="card">
      <div className="card-head">
        <h2>Bottles</h2>
        {toRefill.length > 0 && (
          <span className="chip" data-tone="warning">
            {toRefill.length} to refill
          </span>
        )}
      </div>
      {withBottle.length === 0 ? (
        <p className="muted">Each plant's bottle level shows here once its node has checked in.</p>
      ) : (
        <div className="bottle-list">
          {withBottle
            .slice()
            .sort((a, b) => a.bottle!.pct - b.bottle!.pct)
            .map((p) => (
              <Link key={p.id} href={`/plants/${encodeURIComponent(p.id)}`} className="bottle-row">
                <BottleGauge
                  pct={p.bottle!.pct}
                  lowPct={byId.get(p.id)?.bottle.lowPct ?? 25}
                  empty={p.bottle!.empty}
                  width={16}
                  height={26}
                />
                <span className="grow">{p.name}</span>
                <span className="ink-2 small">
                  {p.bottle!.empty
                    ? 'Empty'
                    : p.bottle!.daysLeft !== null
                      ? `${formatMl(p.bottle!.ml)} · ${p.bottle!.daysLeft.toFixed(1)} days`
                      : formatMl(p.bottle!.ml)}
                </span>
              </Link>
            ))}
        </div>
      )}
      <p className="small muted" style={{ marginTop: 10 }}>
        After refilling, press the button on the plant's node.
      </p>
    </section>
  );
}

export function Overview() {
  const status = useStatus();
  const { plants } = useDevice();
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

      {status.newNodes.length > 0 && (
        <section className="stack" aria-label="New plant nodes">
          {status.newNodes.map((n) => (
            <div key={n.id} className="notice" data-tone="info">
              <Icon name="node" size={20} />
              <div className="grow">
                <p>
                  <strong>New plant node found</strong> ({n.board}, {n.id})
                </p>
                <p className="muted small">Said hello {formatAgo(n.lastSeenAt, clockNow)}</p>
              </div>
              {plants.length < MAX_PLANTS && (
                <Link href={`/plants/new/${n.id}`} className="btn btn-small btn-water">
                  Add as a plant
                </Link>
              )}
            </div>
          ))}
        </section>
      )}

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
            <PlantCard key={p.id} status={p} profile={byId.get(p.id)} now={clockNow} />
          ))}
          {status.plants.length < MAX_PLANTS && (
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
          <Bottles status={status} byId={byId} />

          <Link href="/climate" className="card" style={{ color: 'inherit', textDecoration: 'none' }}>
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
              <p className="muted">No climate sensor on the main station.</p>
            )}
          </Link>

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
                <span>Plant nodes</span>
                <span>
                  {status.radio.total === 0
                    ? 'None yet'
                    : `${status.radio.online} of ${status.radio.total} online`}
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
                <span>History</span>
                <span>{status.storage.sdOk ? 'On the SD card' : 'Last day only (no SD card)'}</span>
              </div>
            </div>
          </section>
        </aside>
      </div>
    </div>
  );
}
