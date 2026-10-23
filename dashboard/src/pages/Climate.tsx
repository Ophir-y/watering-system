import { useMemo, useState } from 'react';

import { api } from '../api/client';
import { TimeSeriesChart } from '../components/TimeSeriesChart';
import { formatLux, formatPct, formatTemp } from '../lib/format';
import { useAsync, useNow } from '../lib/hooks';
import { useStatus } from '../state/device';

const RANGES = { '24 h': 86400, '7 days': 7 * 86400, '30 days': 30 * 86400 } as const;
type RangeKey = keyof typeof RANGES;

// Room climate from the main station's sensors: small multiples, one measure per chart.
// The nodes use it to water sooner when the air is hot and dry.
export function Climate() {
  const status = useStatus();
  const realNow = useNow(60000);
  const now = status.device.now ?? realNow;
  const [range, setRange] = useState<RangeKey>('24 h');
  const span = RANGES[range];
  const hist = useAsync(() => api.envHistory(now - span, now, 240), `${range}-${Math.floor(now / 300)}`);
  const env = status.environment;

  const cols = useMemo(() => {
    const pts = hist.data?.points ?? [];
    return {
      t: pts.map((p) => p[0]),
      temp: pts.map((p) => p[1]),
      rh: pts.map((p) => p[2]),
      lux: pts.map((p) => p[3]),
    };
  }, [hist.data]);

  return (
    <div className="page">
      <div className="page-head">
        <div>
          <h1>Room</h1>
          <p>
            What the main station's climate sensors recorded.{' '}
            {env.valid
              ? `Now ${formatTemp(env.tempC)}, ${formatPct(env.humidityPct)} humidity.`
              : 'No climate sensor is connected.'}
          </p>
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
        <section className="card">
          <h2>Why it matters</h2>
          <p className="ink-2">
            Warm, dry air pulls water out of soil and leaves faster. When the drying power (VPD) is high, each
            node starts watering its plant a little sooner; below a plant&apos;s minimum temperature its doses
            are halved. The sensors measure one spot, so keep the main station near the plants.
          </p>
        </section>
      </div>
    </div>
  );
}
