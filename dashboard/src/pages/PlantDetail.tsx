import { useMemo, useState } from 'react';
import { Link } from 'wouter';

import { api } from '../api/client';
import type { PlantProfile, PlantStatus, QueuedCommand, Status } from '../api/types';
import { BottleGauge } from '../components/BottleGauge';
import { ConditionChip } from '../components/ConditionChip';
import { NumberField } from '../components/Fields';
import { Icon } from '../components/Icon';
import { SoilGauge } from '../components/SoilGauge';
import { TimeSeriesChart } from '../components/TimeSeriesChart';
import { WaterDialog } from '../components/WaterDialog';
import { STYLE_LABEL, findSpecies } from '../data/species';
import { EMPTY_REASON_TEXT, ISSUE_TEXT, PUMP_TEXT, plantCondition } from '../lib/condition';
import {
  formatAgo,
  formatDateTime,
  formatDuration,
  formatEta,
  formatLux,
  formatMl,
  formatMonths,
  formatPct,
} from '../lib/format';
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
  timed: 'Flow test',
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
  const bottleValues = useMemo(() => (history.data?.points ?? []).map((p) => p[2]), [history.data]);
  const waterings = (events.data?.events ?? []).filter((e) => e.type === 'watering' && e.ml > 0);
  const markers = useMemo(() => waterings.map((e) => e.ts), [waterings]);

  const recs = recommendations({
    profile,
    status: ps,
    env: env.data ? envStats(env.data) : null,
    watering: events.data ? wateringStats(events.data.events, profile.id, now - 7 * 86400, now) : null,
    month: new Date(now * 1000).getMonth() + 1,
    southernHemisphere: settings?.southernHemisphere ?? false,
    bottleDaysLeft: ps.bottle?.daysLeft ?? null,
  });
  const dosing = ps.dosing;
  const canWater = !!profile.node && !status.automation.emergencyStop;

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
            {species ? species.latin : 'Custom plant'}
            {profile.node ? ` · node ${profile.node}` : ' · no node'}
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
            {ps.bottle && (
              <span>
                bottle <strong>{ps.bottle.empty ? 'empty' : formatMl(ps.bottle.ml)}</strong>
              </span>
            )}
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
          <button className="btn btn-water" onClick={() => setWatering(true)} disabled={!canWater}>
            <Icon name="drop" size={16} /> Water now
          </button>
          <Link href={`/plants/${encodeURIComponent(profile.id)}/edit`} className="btn">
            <Icon name="edit" size={16} /> Edit
          </Link>
        </div>
      </section>

      <QueuedNotice queued={ps.queued} etaS={ps.nextCheckInS} />

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

      {profile.node && (
        <BottleAndPump
          profile={profile}
          ps={ps}
          times={times}
          bottle={bottleValues}
          loading={history.loading}
        />
      )}
      <SensorAndNode profile={profile} ps={ps} now={now} />
      <WaterDialog plant={profile} open={watering} onClose={() => setWatering(false)} />
    </div>
  );
}

const COMMAND_TEXT: Record<string, (arg: number) => string> = {
  water: (ml) => `Water ${Math.round(ml)} ml`,
  timed_run: (s) => `Run the pump ${s} s`,
  refilled: () => 'Bottle refilled',
  clear_faults: () => 'Clear faults',
  identify: () => 'Blink the LED',
  forget: () => 'Unpair the node',
};

function QueuedNotice({ queued, etaS }: { queued: QueuedCommand[]; etaS: number | null }) {
  if (!queued.length) return null;
  return (
    <div className="notice" data-tone="info">
      <Icon name="clock" size={18} />
      <div className="grow">
        <p>
          <strong>Waiting for the node:</strong>{' '}
          {queued.map((c) => (COMMAND_TEXT[c.kind] ?? (() => c.kind))(c.arg)).join(', ')}
        </p>
        <p className="muted small">Carried out {formatEta(etaS)}.</p>
      </div>
    </div>
  );
}

function useRun() {
  const { refresh, reloadPlants } = useDevice();
  const toast = useToast();
  const [busy, setBusy] = useState(false);
  const run = async (fn: () => Promise<unknown>, done: string | ((r: unknown) => string)) => {
    setBusy(true);
    try {
      const r = await fn();
      toast.show(typeof done === 'string' ? done : done(r));
      await Promise.all([refresh(), reloadPlants()]);
    } catch (e) {
      toast.error(e);
    } finally {
      setBusy(false);
    }
  };
  return { busy, run };
}

const queuedText = (what: string) => (r: unknown) =>
  `${what} ${formatEta((r as { etaS: number | null }).etaS)}`;

function BottleAndPump({
  profile,
  ps,
  times,
  bottle,
  loading,
}: {
  profile: PlantProfile;
  ps: PlantStatus;
  times: number[];
  bottle: (number | null)[];
  loading: boolean;
}) {
  const { busy, run } = useRun();
  const [measured, setMeasured] = useState(NaN);
  const testSeconds = 10;
  const b = ps.bottle;
  const saveFlow = (flow: number) =>
    run(
      () =>
        api.updatePlant(profile.id, { pump: { ...profile.pump, flowMlPerS: Math.round(flow * 100) / 100 } }),
      `Flow set to ${flow.toFixed(2)} ml/s`,
    );

  return (
    <section className="card">
      <h2>Bottle and pump</h2>
      <div className="two-col">
        <div className="stack">
          <div className="tank-row">
            <BottleGauge pct={b?.pct ?? null} lowPct={profile.bottle.lowPct} empty={!!b?.empty} />
            <div className="tank-figure">
              <strong>{b ? (b.empty ? 'Empty' : formatMl(b.ml)) : '–'}</strong>
              <span className="ink-2">
                {b ? `about ${formatPct(b.pct)} of ${formatMl(b.capacityMl)}` : 'No report yet'}
              </span>
              <p className="small muted" style={{ marginTop: 4 }}>
                {b?.empty
                  ? `Empty: ${EMPTY_REASON_TEXT[b.emptyReason] ?? b.emptyReason}.`
                  : b?.daysLeft !== null && b?.daysLeft !== undefined
                    ? `About ${b.daysLeft.toFixed(1)} days left`
                    : 'Learning daily use'}
              </p>
            </div>
          </div>
          <p className="small ink-2">
            There is no level sensor: the node counts what it pumps, and corrects itself each time the bottle
            runs dry. After refilling, press the button on the node, or here:
          </p>
          <div className="row">
            <button
              className="btn btn-small btn-water"
              disabled={busy}
              onClick={() => run(() => api.refilled(profile.id), queuedText('Refill noted, sent'))}
            >
              <Icon name="refill" size={16} /> Refilled
            </button>
          </div>
          <TimeSeriesChart
            times={times}
            values={bottle}
            label="Bottle"
            format={(v) => formatMl(v)}
            yRange={[0, profile.bottle.capacityMl]}
            height={140}
            loading={loading}
            emptyText="No bottle readings yet"
          />
        </div>

        <div className="stack">
          <h3>Pump</h3>
          <dl className="profile-list">
            <dt>Flow</dt>
            <dd>
              {profile.pump.flowMlPerS.toFixed(2)} ml/s
              {b && Math.abs(b.flowFactor - 1) >= 0.03 && (
                <span className="muted">
                  {' '}
                  (really about {(profile.pump.flowMlPerS * b.flowFactor).toFixed(2)})
                </span>
              )}
            </dd>
            <dt>Last run</dt>
            <dd>{PUMP_TEXT[ps.pump.lastRun] ?? ps.pump.lastRun}</dd>
            <dt>Feels dry runs</dt>
            <dd>
              {!profile.pump.currentSense
                ? 'Off (no current sensing)'
                : ps.pump.learned
                  ? 'Yes: it knows its normal current'
                  : 'Learning: after a few waterings'}
            </dd>
            <dt>Limits</dt>
            <dd>
              {formatMl(profile.maxPulseMl)} per pulse · {formatMl(profile.maxDailyMl)} a day
            </dd>
          </dl>
          <p className="small ink-2">
            Measure the flow: hold the tube in a measuring cup, run the pump for {testSeconds} seconds, then
            enter how much came out. This water is not counted as watering.
          </p>
          <div className="row" style={{ alignItems: 'flex-end' }}>
            <button
              className="btn btn-small"
              disabled={busy}
              onClick={() =>
                run(
                  () => api.timedRun(profile.id, testSeconds),
                  queuedText(`${testSeconds} s run sent, runs`),
                )
              }
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

function SensorAndNode({ profile, ps, now }: { profile: PlantProfile; ps: PlantStatus; now: number }) {
  const { busy, run } = useRun();
  const calibrate = (point: 'dry' | 'wet') =>
    run(
      () => api.calibrate(profile.id, point),
      (r) => {
        const age = (r as { ageS: number }).ageS;
        return `${point === 'dry' ? 'Dry' : 'Wet'} point saved (reading from ${age < 60 ? 'just now' : `${formatDuration(age)} ago`})`;
      },
    );

  return (
    <section className="card">
      <h2>Sensor and node</h2>
      <div className="two-col">
        <div className="stack">
          <h3>Moisture sensor</h3>
          <dl className="profile-list">
            <dt>Status</dt>
            <dd>{ISSUE_TEXT[ps.issue] ?? ps.issue}</dd>
            <dt>Raw reading</dt>
            <dd className="num">{ps.raw ?? '–'}</dd>
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
            The node reads the soil each time it checks in. To calibrate, hold the sensor in the air and wait
            for a check-in (or press the node&apos;s button, which also counts as a refill), then choose{' '}
            <em>Set dry point</em>. Then stand it in a glass of water up to its line and do the same with{' '}
            <em>Set wet point</em>.
          </p>
          <div className="row">
            <button
              className="btn btn-small"
              disabled={busy || !profile.node}
              onClick={() => calibrate('dry')}
            >
              Set dry point
            </button>
            <button
              className="btn btn-small"
              disabled={busy || !profile.node}
              onClick={() => calibrate('wet')}
            >
              Set wet point
            </button>
            {(ps.lockedOut || ps.issue === 'no_response') && (
              <button
                className="btn btn-small btn-water"
                disabled={busy}
                onClick={() => run(() => api.clearFaults(profile.id), queuedText('Clearing faults'))}
              >
                Clear faults
              </button>
            )}
          </div>
        </div>

        <div className="stack">
          <h3>Plant node</h3>
          {!profile.node ? (
            <p className="muted">
              This plant has no node, so nothing waters it. Edit the plant to give it one.
            </p>
          ) : (
            <>
              <dl className="profile-list">
                <dt>Node</dt>
                <dd className="num">{profile.node}</dd>
                <dt>Status</dt>
                <dd>
                  {ps.online ? 'Online' : 'Offline'}
                  {ps.lastSeenAt && `, last check-in ${formatAgo(ps.lastSeenAt, now)}`}
                </dd>
                <dt>Next check-in</dt>
                <dd>
                  {ps.nextCheckInS === null
                    ? '–'
                    : ps.nextCheckInS < 20
                      ? 'any moment'
                      : `in ${formatDuration(ps.nextCheckInS)}`}
                </dd>
                <dt>Batteries</dt>
                <dd>
                  {ps.battery
                    ? `${ps.battery.pct}% (${(ps.battery.mv / 1000).toFixed(2)} V)${ps.battery.low ? ': replace soon' : ''}`
                    : '–'}
                </dd>
                <dt>Signal</dt>
                <dd>{ps.linkPct === null ? '–' : `${ps.linkPct}% of messages answered`}</dd>
                <dt>Firmware</dt>
                <dd>{ps.firmware ?? '–'}</dd>
              </dl>
              {ps.configPending && (
                <p className="small ink-2">
                  New settings are on their way to the node: they arrive at its next check-in.
                </p>
              )}
              {ps.repairRequested && (
                <div className="notice" data-tone="warning">
                  <div className="grow small">
                    This node lost its pairing and asks to pair again. Only confirm if you just reset it.
                  </div>
                  <button
                    className="btn btn-small"
                    disabled={busy}
                    onClick={() =>
                      run(() => api.repairNode(profile.node!), 'Pairing again at its next check-in')
                    }
                  >
                    Pair again
                  </button>
                </div>
              )}
              <div className="row">
                <button
                  className="btn btn-small"
                  disabled={busy}
                  onClick={() => run(() => api.identify(profile.id), queuedText('The LED blinks'))}
                >
                  Blink its LED
                </button>
              </div>
            </>
          )}
        </div>
      </div>
    </section>
  );
}
