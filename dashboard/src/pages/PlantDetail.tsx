import { useMemo, useState } from 'react';
import { Link } from 'wouter';

import { api } from '../api/client';
import type { PlantProfile, PlantStatus, Status } from '../api/types';
import { ConditionChip } from '../components/ConditionChip';
import { NumberField } from '../components/Fields';
import { Icon } from '../components/Icon';
import { SoilGauge } from '../components/SoilGauge';
import { TimeSeriesChart } from '../components/TimeSeriesChart';
import { WaterDialog } from '../components/WaterDialog';
import { STYLE_LABEL, findSpecies } from '../data/species';
import { ISSUE_TEXT, plantCondition } from '../lib/condition';
import { formatAgo, formatDateTime, formatLux, formatMl, formatMonths, formatPct } from '../lib/format';
import { useAsync, useNow } from '../lib/hooks';
import { envStats, recommendations, wateringStats } from '../lib/recommendations';
import { useDevice, useStatus } from '../state/device';
import { useToast } from '../state/toast';

const RANGES = { '24 h': 86400, '7 days': 7 * 86400, '30 days': 30 * 86400, '1 year': 365 * 86400 } as const;
type RangeKey = keyof typeof RANGES;

const HOW: Record<string, string> = {
  auto: 'Automatic',
  manual: 'By hand',
  fallback: 'Fallback schedule',
  timed: 'Outlet test',
  detected: 'Watered outside the system',
  interrupted: 'Interrupted',
};

export function PlantDetail({ id }: { id: string }) {
  const status = useStatus();
  const { plants } = useDevice();
  const profile = plants.find((p) => p.id === id);
  const ps = status.plants.find((p) => p.id === id);
  if (!profile || !ps) {
    return (
      <div className="page">
        <div className="empty card">
          <h1>That plant is not here</h1>
          <p>It may have been removed.</p>
          <Link href="/" className="btn">
            Back to all plants
          </Link>
        </div>
      </div>
    );
  }
  return <PlantView profile={profile} ps={ps} status={status} />;
}

function PlantView({ profile, ps, status }: { profile: PlantProfile; ps: PlantStatus; status: Status }) {
  const { settings } = useDevice();
  const [range, setRange] = useState<RangeKey>('24 h');
  const [watering, setWatering] = useState(false);
  const realNow = useNow(60000);
  const now = status.device.now ?? realNow;
  const bucket = Math.floor(now / 300);
  const span = RANGES[range];

  const history = useAsync(
    () => api.history(profile.id, now - span, now, 240),
    `${profile.id}-${range}-${bucket}`,
  );
  const events = useAsync(
    () => api.events({ plant: profile.id, from: now - 30 * 86400, to: now, limit: 300 }),
    `${profile.id}-ev-${bucket}`,
  );
  const env = useAsync(() => api.envHistory(now - 7 * 86400, now, 168), `env-${Math.floor(now / 3600)}`);

  const species = findSpecies(profile.species);
  const condition = plantCondition(ps, profile);
  const times = useMemo(() => (history.data?.points ?? []).map((p) => p[0]), [history.data]);
  const values = useMemo(() => (history.data?.points ?? []).map((p) => p[1]), [history.data]);
  const waterings = (events.data?.events ?? []).filter((e) => e.type === 'watering' && e.ml > 0);
  const markers = useMemo(() => waterings.map((e) => e.ts), [waterings]);

  const recs = recommendations({
    profile,
    status: ps,
    env: env.data ? envStats(env.data) : null,
    watering: events.data ? wateringStats(events.data.events, profile.id, now - 7 * 86400, now) : null,
    month: new Date(now * 1000).getMonth() + 1,
    southernHemisphere: settings?.southernHemisphere ?? false,
    reservoirDaysLeft: status.reservoir.daysLeft,
  });
  const dosing = status.dose?.plantId === profile.id ? status.dose : null;

  return (
    <div className="page">
      <Link href="/" className="btn btn-ghost btn-small" style={{ width: 'max-content' }}>
        <Icon name="arrowLeft" size={16} /> All plants
      </Link>

      <section className="plant-hero">
        <SoilGauge
          pct={ps.moisturePct}
          band={profile.moisture}
          trigger={ps.triggerPct}
          state={ps.state}
          height={176}
        />
        <div style={{ minWidth: 0 }}>
          <h1 className="name tag-name">{profile.name}</h1>
          <p className="muted" style={{ fontStyle: 'italic', marginTop: 4 }}>
            {species ? species.latin : 'Custom plant'} · channel {profile.channel + 1} · {profile.outlet.type}
          </p>
          <div className="facts">
            <span>
              <strong>{formatPct(ps.moisturePct)}</strong> now
            </span>
            <span>
              target <strong>{formatPct(ps.targetPct)}</strong>
            </span>
            <span>
              waters below <strong>{formatPct(ps.triggerPct)}</strong>
            </span>
            <span>
              {formatMl(ps.mlToday)} of {formatMl(profile.maxDailyMl)} today
            </span>
            <span>watered {formatAgo(ps.lastWateredAt, now)}</span>
          </div>
          <div style={{ marginTop: 12 }}>
            <ConditionChip condition={condition} />
            {dosing && (
              <span className="small ink-2" style={{ marginLeft: 10 }}>
                {Math.round(dosing.dispensedMl)} of {Math.round(dosing.ml)} ml poured
              </span>
            )}
            {!profile.autoWater && (
              <span className="small muted" style={{ marginLeft: 10 }}>
                Manual watering only
              </span>
            )}
          </div>
        </div>
        <div className="actions row">
          <button className="btn btn-water" onClick={() => setWatering(true)} disabled={!!status.dose}>
            <Icon name="drop" size={16} /> Water now
          </button>
          <Link href={`/plants/${encodeURIComponent(profile.id)}/edit`} className="btn">
            <Icon name="edit" size={16} /> Edit
          </Link>
        </div>
      </section>

      <section className="card">
        <div className="card-head">
          <h2>Soil moisture</h2>
          <div className="segmented" role="group" aria-label="Time range">
            {(Object.keys(RANGES) as RangeKey[]).map((r) => (
              <button key={r} aria-pressed={range === r} onClick={() => setRange(r)}>
                {r}
              </button>
            ))}
          </div>
        </div>
        <TimeSeriesChart
          times={times}
          values={values}
          label="Moisture"
          format={(v) => `${Math.round(v)}%`}
          band={{ lo: ps.triggerPct, hi: profile.moisture.max }}
          bandLabel={`Kept between ${Math.round(ps.triggerPct)}% and ${profile.moisture.max}%`}
          markers={markers}
          markerLabel="Watering"
          yRange={[0, 100]}
          loading={history.loading}
        />
      </section>

      <div className="two-col">
        <section className="card">
          <h2>Care profile</h2>
          <dl className="profile-list">
            <dt>Watering</dt>
            <dd>{STYLE_LABEL[profile.style]}</dd>
            <dt>Light</dt>
            <dd>
              {species?.light ?? ''} ({formatLux(profile.lightLux.min)}–{formatLux(profile.lightLux.max)})
            </dd>
            <dt>Temperature</dt>
            <dd>
              {profile.tempC.min}–{profile.tempC.max} °C
            </dd>
            <dt>Humidity</dt>
            <dd>
              {profile.humidityPct.min}–{profile.humidityPct.max}%
            </dd>
            <dt>Soil</dt>
            <dd>{profile.soil || '–'}</dd>
            <dt>Resting months</dt>
            <dd>
              {formatMonths(profile.dormantMonths)}
              {profile.dormantMonths.length > 0 && ` (doses at ${Math.round(profile.dormancyFactor * 100)}%)`}
            </dd>
            <dt>Pot</dt>
            <dd>{formatMl(profile.potVolumeMl)}</dd>
            {species?.petSafe !== undefined && species.petSafe !== null && (
              <>
                <dt>Pets</dt>
                <dd>{species.petSafe ? 'Non-toxic to cats and dogs' : 'Toxic to cats and dogs'}</dd>
              </>
            )}
          </dl>
          {profile.careNotes && (
            <p className="ink-2" style={{ marginTop: 14, fontSize: 14 }}>
              {profile.careNotes}
            </p>
          )}
        </section>

        <section className="card">
          <h2>Recommendations</h2>
          {recs.length === 0 ? (
            <p className="muted">Nothing to change: conditions match what {profile.name} likes.</p>
          ) : (
            <div className="stack">
              {recs.map((r) => (
                <div key={r.id} className="reco" data-level={r.level}>
                  <strong>{r.title}</strong>
                  <p>{r.body}</p>
                </div>
              ))}
            </div>
          )}
          <p className="small muted" style={{ marginTop: 12 }}>
            Based on the last 7 days of readings. The light and climate sensors measure one spot in the room.
          </p>
        </section>
      </div>

      <section className="card">
        <h2>Watering log</h2>
        {waterings.length === 0 ? (
          <p className="muted">No waterings in the last 30 days.</p>
        ) : (
          <div className="table-scroll" style={{ maxHeight: 360 }}>
            <table className="data">
              <thead>
                <tr>
                  <th>When</th>
                  <th className="num">Amount</th>
                  <th>How</th>
                  <th>Details</th>
                </tr>
              </thead>
              <tbody>
                {waterings.map((e) => (
                  <tr key={`${e.ts}-${e.code}`}>
                    <td>{formatDateTime(e.ts)}</td>
                    <td className="num">{formatMl(e.ml)}</td>
                    <td>{HOW[e.code] ?? e.code}</td>
                    <td className="ink-2">{e.detail}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </section>

      <SensorAndOutlet profile={profile} ps={ps} status={status} />
      <WaterDialog plant={profile} open={watering} onClose={() => setWatering(false)} />
    </div>
  );
}

function SensorAndOutlet({
  profile,
  ps,
  status,
}: {
  profile: PlantProfile;
  ps: PlantStatus;
  status: Status;
}) {
  const { refresh, reloadPlants } = useDevice();
  const toast = useToast();
  const [measured, setMeasured] = useState(NaN);
  const [busy, setBusy] = useState(false);
  const testSeconds = 10;
  const last = status.lastDose?.plantId === profile.id ? status.lastDose : null;
  const measuredFlow = last?.closedLoop && last.onTimeMs > 1000 ? last.ml / (last.onTimeMs / 1000) : null;

  const run = async (fn: () => Promise<unknown>, done: string) => {
    setBusy(true);
    try {
      await fn();
      toast.show(done);
      await Promise.all([refresh(), reloadPlants()]);
    } catch (e) {
      toast.error(e);
    } finally {
      setBusy(false);
    }
  };
  const saveFlow = (flow: number) =>
    run(
      () =>
        api.updatePlant(profile.id, {
          outlet: { ...profile.outlet, flowMlPerS: Math.round(flow * 100) / 100 },
        }),
      `Flow set to ${flow.toFixed(2)} ml/s`,
    );

  return (
    <section className="card">
      <h2>Sensor and outlet</h2>
      <div className="two-col">
        <div className="stack">
          <h3>Moisture sensor</h3>
          <dl className="profile-list">
            <dt>Status</dt>
            <dd>{ISSUE_TEXT[ps.issue] ?? ps.issue}</dd>
            <dt>Raw reading</dt>
            <dd className="num">{ps.raw}</dd>
            <dt>Calibration</dt>
            <dd className="num">
              dry {profile.calibration.rawDry} · wet {profile.calibration.rawWet}
            </dd>
            <dt>Learned</dt>
            <dd>
              {ps.mlPerPct
                ? `${ps.mlPerPct.toFixed(1)} ml raises moisture by 1%`
                : 'Not yet: needs a few waterings'}
            </dd>
          </dl>
          <p className="small ink-2">
            To calibrate, hold the sensor in the air (or bone-dry soil) and choose <em>Set dry point</em>.
            Then stand it in a glass of water up to its line and choose <em>Set wet point</em>.
          </p>
          <div className="row">
            <button
              className="btn btn-small"
              disabled={busy}
              onClick={() => run(() => api.calibrate(profile.id, 'dry'), 'Dry point saved')}
            >
              Set dry point
            </button>
            <button
              className="btn btn-small"
              disabled={busy}
              onClick={() => run(() => api.calibrate(profile.id, 'wet'), 'Wet point saved')}
            >
              Set wet point
            </button>
            {(ps.lockedOut || ps.issue === 'no_response') && (
              <button
                className="btn btn-small btn-water"
                disabled={busy}
                onClick={() => run(() => api.clearFaults(profile.id), 'Faults cleared')}
              >
                Clear faults
              </button>
            )}
          </div>
        </div>

        <div className="stack">
          <h3>Outlet ({profile.outlet.type === 'pump' ? 'pump' : 'gravity valve'})</h3>
          <dl className="profile-list">
            <dt>Flow</dt>
            <dd>{profile.outlet.flowMlPerS.toFixed(2)} ml/s</dd>
            <dt>Limits</dt>
            <dd>
              {formatMl(profile.maxPulseMl)} per pulse · {formatMl(profile.maxDailyMl)} a day
            </dd>
          </dl>
          {measuredFlow !== null && (
            <div className="notice" data-tone="info">
              <div className="grow small">
                The scale measured {formatMl(last!.ml)} in {(last!.onTimeMs / 1000).toFixed(1)} s on the last
                watering: <strong>{measuredFlow.toFixed(2)} ml/s</strong>.
              </div>
              <button className="btn btn-small" disabled={busy} onClick={() => saveFlow(measuredFlow)}>
                Use it
              </button>
            </div>
          )}
          <p className="small ink-2">
            Without a scale: put the outlet in a measuring cup, run it for {testSeconds} seconds, then enter
            how much came out.
          </p>
          <div className="row" style={{ alignItems: 'flex-end' }}>
            <button
              className="btn btn-small"
              disabled={busy || !!status.dose}
              onClick={() => run(() => api.timedRun(profile.id, testSeconds), `Running for ${testSeconds} s`)}
            >
              Run {testSeconds} s
            </button>
            <div style={{ width: 150 }}>
              <NumberField label="Measured" value={measured} onChange={setMeasured} unit="ml" min={1} />
            </div>
            <button
              className="btn btn-small"
              disabled={busy || !(measured > 0)}
              onClick={() => saveFlow(measured / testSeconds)}
            >
              Save flow
            </button>
          </div>
        </div>
      </div>
    </section>
  );
}
