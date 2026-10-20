import { useMemo, useState } from 'react';

import { api } from '../api/client';
import { NumberField } from '../components/Fields';
import { ReservoirTank } from '../components/ReservoirTank';
import { TimeSeriesChart } from '../components/TimeSeriesChart';
import { formatLux, formatMl, formatPct } from '../lib/format';
import { useAsync, useNow } from '../lib/hooks';
import { useDevice, useStatus } from '../state/device';
import { useToast } from '../state/toast';

const RANGES = { '24 h': 86400, '7 days': 7 * 86400, '30 days': 30 * 86400 } as const;
type RangeKey = keyof typeof RANGES;

// Room climate and the reservoir: small multiples, one measure per chart.
export function Climate() {
  const status = useStatus();
  const { settings, refresh } = useDevice();
  const toast = useToast();
  const realNow = useNow(60000);
  const now = status.device.now ?? realNow;
  const [range, setRange] = useState<RangeKey>('24 h');
  const span = RANGES[range];
  const hist = useAsync(() => api.envHistory(now - span, now, 240), `${range}-${Math.floor(now / 300)}`);
  const [refillMl, setRefillMl] = useState(NaN);
  const [knownMl, setKnownMl] = useState(1000);
  const [step, setStep] = useState<'idle' | 'tared'>('idle');
  const res = status.reservoir;

  const cols = useMemo(() => {
    const pts = hist.data?.points ?? [];
    return {
      t: pts.map((p) => p[0]),
      temp: pts.map((p) => p[1]),
      rh: pts.map((p) => p[2]),
      lux: pts.map((p) => p[3]),
      res: pts.map((p) => p[4]),
    };
  }, [hist.data]);

  const act = async (fn: () => Promise<unknown>, msg: string, after?: () => void) => {
    try {
      await fn();
      toast.show(msg);
      after?.();
      await refresh();
    } catch (e) {
      toast.error(e);
    }
  };

  return (
    <div className="page">
      <div className="page-head">
        <div>
          <h1>Room and reservoir</h1>
          <p>What the climate sensors and the reservoir scale recorded.</p>
        </div>
        <div className="segmented" role="group" aria-label="Time range">
          {(Object.keys(RANGES) as RangeKey[]).map((r) => (
            <button key={r} aria-pressed={range === r} onClick={() => setRange(r)}>
              {r}
            </button>
          ))}
        </div>
      </div>

      <div className="two-col">
        <section className="card">
          <div className="card-head">
            <h2>Reservoir</h2>
            <span className="ink-2">
              {res.ml === null ? 'Scale not available' : `${formatMl(res.ml)} · ${formatPct(res.pct)}`}
            </span>
          </div>
          <TimeSeriesChart
            times={cols.t}
            values={cols.res}
            label="Reservoir"
            format={(v) => formatMl(v)}
            yRange={[0, res.capacityMl]}
            height={180}
            loading={hist.loading}
            emptyText="No reservoir readings yet. Calibrate the scale below."
          />
        </section>
        <section className="card">
          <div className="card-head">
            <h2>Temperature</h2>
          </div>
          <TimeSeriesChart
            times={cols.t}
            values={cols.temp}
            label="Temperature"
            format={(v) => `${v.toFixed(1)}°`}
            height={180}
            loading={hist.loading}
          />
        </section>
        <section className="card">
          <div className="card-head">
            <h2>Humidity</h2>
          </div>
          <TimeSeriesChart
            times={cols.t}
            values={cols.rh}
            label="Humidity"
            format={(v) => `${Math.round(v)}%`}
            yRange={[0, 100]}
            height={180}
            loading={hist.loading}
          />
        </section>
        <section className="card">
          <div className="card-head">
            <h2>Light</h2>
          </div>
          <TimeSeriesChart
            times={cols.t}
            values={cols.lux}
            label="Light"
            format={(v) => formatLux(v)}
            height={180}
            loading={hist.loading}
          />
        </section>
      </div>

      <div className="two-col">
        <section className="card form-section">
          <h2>Record a refill</h2>
          <p>
            The scale notices refills by itself. Without a scale, note them here so the activity log stays
            complete.
          </p>
          <div className="row" style={{ alignItems: 'flex-end' }}>
            <div style={{ width: 180 }}>
              <NumberField label="Water added" value={refillMl} onChange={setRefillMl} unit="ml" step={100} />
            </div>
            <button
              className="btn"
              disabled={!(refillMl > 0)}
              onClick={() =>
                act(
                  () => api.refilled(refillMl),
                  'Refill recorded',
                  () => setRefillMl(NaN),
                )
              }
            >
              Record refill
            </button>
          </div>
        </section>

        <section className="card form-section">
          <h2>Calibrate the scale</h2>
          <div className="tank-row">
            <ReservoirTank
              pct={res.pct}
              lowPct={settings?.reservoir.lowPct ?? 20}
              empty={res.empty}
              width={48}
              height={72}
            />
            <p className="small ink-2">
              {res.scaleCalibrated ? 'The scale is calibrated.' : 'The scale is not calibrated yet.'} Raw
              reading <span className="num">{res.scaleRaw}</span>.
            </p>
          </div>
          {step === 'idle' ? (
            <>
              <p>1. Put the empty reservoir on the scale, then tare.</p>
              <button
                className="btn"
                style={{ width: 'max-content' }}
                onClick={() => act(api.tareScale, 'Empty weight saved', () => setStep('tared'))}
              >
                Tare empty reservoir
              </button>
            </>
          ) : (
            <>
              <p>2. Pour in a measured amount of water, wait a few seconds, then save.</p>
              <div className="row" style={{ alignItems: 'flex-end' }}>
                <div style={{ width: 180 }}>
                  <NumberField
                    label="Water poured in"
                    value={knownMl}
                    onChange={setKnownMl}
                    unit="ml"
                    step={100}
                  />
                </div>
                <button
                  className="btn btn-water"
                  disabled={!(knownMl >= 100)}
                  onClick={() =>
                    act(
                      () => api.calibrateScale(knownMl),
                      'Scale calibrated',
                      () => setStep('idle'),
                    )
                  }
                >
                  Save calibration
                </button>
                <button className="btn btn-ghost" onClick={() => setStep('idle')}>
                  Start over
                </button>
              </div>
            </>
          )}
        </section>
      </div>
    </div>
  );
}
