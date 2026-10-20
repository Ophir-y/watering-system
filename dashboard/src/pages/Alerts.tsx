import { useState } from 'react';
import { Link } from 'wouter';

import { api } from '../api/client';
import type { Alert } from '../api/types';
import { SeverityIcon } from '../components/ConditionChip';
import { Icon } from '../components/Icon';
import { formatAgo, formatDateTime } from '../lib/format';
import { useAsync, useNow } from '../lib/hooks';
import { useDevice, useStatus } from '../state/device';
import { useToast } from '../state/toast';

const SEVERITY_LABEL = { critical: 'Critical', warning: 'Warning', info: 'Note' } as const;

export function Alerts() {
  const status = useStatus();
  const { plants, refresh } = useDevice();
  const toast = useToast();
  const realNow = useNow();
  const now = status.device.now ?? realNow;
  const [showResolved, setShowResolved] = useState(false);
  const key = `${status.alerts.critical}-${status.alerts.warning}-${status.alerts.info}`;
  const alerts = useAsync(() => api.alerts(), key);
  const names = new Map(plants.map((p) => [p.id, p.name]));
  const all = alerts.data?.alerts ?? [];
  const active = all.filter((a) => a.active);
  const resolved = all.filter((a) => !a.active);

  const act = async (fn: () => Promise<unknown>, msg: string) => {
    try {
      await fn();
      toast.show(msg);
      alerts.reload();
      await refresh();
    } catch (e) {
      toast.error(e);
    }
  };

  const row = (a: Alert) => (
    <li key={a.id} style={{ gridTemplateColumns: '28px 1fr auto' }}>
      <SeverityIcon severity={a.severity} />
      <div style={{ minWidth: 0 }}>
        <p>{a.message}</p>
        <p className="small muted">
          {SEVERITY_LABEL[a.severity]}
          {a.plantId && names.get(a.plantId) && (
            <>
              {' · '}
              <Link href={`/plants/${encodeURIComponent(a.plantId)}`}>{names.get(a.plantId)}</Link>
            </>
          )}
          {' · '}
          {a.active
            ? `since ${formatAgo(a.raisedAt, now)}`
            : `resolved ${formatDateTime(a.clearedAt ?? a.raisedAt)}`}
          {a.acked && a.active && ' · acknowledged'}
        </p>
      </div>
      {a.active && !a.acked ? (
        <button className="btn btn-small" onClick={() => act(() => api.ackAlert(a.id), 'Acknowledged')}>
          Acknowledge
        </button>
      ) : (
        <span />
      )}
    </li>
  );

  return (
    <div className="page">
      <div className="page-head">
        <div>
          <h1>Alerts</h1>
          <p>
            Acknowledging stops reminders and the buzzer. An alert clears by itself once the problem is gone.
          </p>
        </div>
        {active.some((a) => !a.acked) && (
          <button className="btn" onClick={() => act(api.ackAllAlerts, 'All alerts acknowledged')}>
            Acknowledge all
          </button>
        )}
      </div>

      <section className="card">
        {active.length === 0 ? (
          <div className="empty">
            <span style={{ color: 'var(--good)' }}>
              <Icon name="check" size={28} />
            </span>
            <p>{alerts.loading ? 'Loading…' : 'No active alerts. Everything is working.'}</p>
          </div>
        ) : (
          <ul className="event-list">{active.map(row)}</ul>
        )}
      </section>

      {resolved.length > 0 && (
        <section className="stack">
          <button
            className="btn btn-ghost"
            style={{ width: 'max-content' }}
            onClick={() => setShowResolved((v) => !v)}
            aria-expanded={showResolved}
          >
            {showResolved ? 'Hide' : 'Show'} {resolved.length} resolved
          </button>
          {showResolved && (
            <section className="card">
              <ul className="event-list">{resolved.map(row)}</ul>
            </section>
          )}
        </section>
      )}
    </div>
  );
}
