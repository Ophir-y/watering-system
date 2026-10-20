import { useId } from 'react';

import type { MoistureBand, PlannerState } from '../api/types';

interface Props {
  pct: number | null;
  band: MoistureBand;
  trigger?: number;
  state?: PlannerState;
  height?: number;
}

// The signature element: a soil core. The column fills and darkens with moisture
// (wet soil is darker); the bracket beside it marks the range the plant is kept in,
// with a dot at the target. Water droplets fall while it is being watered.
export function SoilGauge({ pct, band, trigger, state = 'idle', height = 132 }: Props) {
  const uid = useId().replace(/:/g, '');
  const w = 40;
  const h = height;
  const r = w / 2;
  const y = (v: number) => h - (Math.min(100, Math.max(0, v)) / 100) * h;
  const level = pct === null ? null : Math.min(100, Math.max(0, pct));
  const fillTop = level === null ? h : y(level);
  const lo = trigger ?? band.min;
  const label =
    level === null
      ? 'Moisture unknown'
      : `Moisture ${Math.round(level)}%. Kept between ${Math.round(lo)}% and ${band.max}%, target ${band.target}%.`;

  return (
    <svg
      className="gauge"
      width={w + 18}
      height={h}
      viewBox={`0 0 ${w + 18} ${h}`}
      role="img"
      aria-label={label}
    >
      <defs>
        <clipPath id={`core-${uid}`}>
          <rect x="0" y="0" width={w} height={h} rx={r} />
        </clipPath>
        <linearGradient id={`soil-${uid}`} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0" stopColor="var(--soil-2)" />
          <stop offset="0.45" stopColor="var(--soil-3)" />
          <stop offset="1" stopColor="var(--soil-wet)" />
        </linearGradient>
      </defs>
      <g clipPath={`url(#core-${uid})`}>
        <rect x="0" y="0" width={w} height={h} fill="var(--soil-dry)" />
        {level !== null && (
          <>
            <rect
              className="fill"
              x="0"
              y={fillTop}
              width={w}
              height={h - fillTop}
              fill={`url(#soil-${uid})`}
            />
            {/* surface gap at the moisture line */}
            <rect
              className={state === 'soaking' ? 'seep' : undefined}
              x="0"
              y={fillTop}
              width={w}
              height="2"
              fill="var(--surface)"
              opacity="0.7"
            />
          </>
        )}
        {state === 'watering' &&
          [0, 1, 2].map((i) => (
            <ellipse
              key={i}
              className="drop"
              cx={r + (i - 1) * 8}
              cy={10}
              rx="2.6"
              ry="3.6"
              style={{ ['--fall' as string]: `${Math.max(12, fillTop - 14)}px` }}
            />
          ))}
      </g>
      {/* comfort band: trigger .. max, target dot */}
      <g stroke="var(--water)" strokeWidth="2" strokeLinecap="round">
        <line x1={w + 9} x2={w + 9} y1={y(band.max)} y2={y(lo)} />
        <line x1={w + 5} x2={w + 9} y1={y(band.max)} y2={y(band.max)} />
        <line x1={w + 5} x2={w + 9} y1={y(lo)} y2={y(lo)} />
      </g>
      <circle
        cx={w + 9}
        cy={y(band.target)}
        r="3.5"
        fill="var(--water)"
        stroke="var(--surface)"
        strokeWidth="2"
      />
    </svg>
  );
}
