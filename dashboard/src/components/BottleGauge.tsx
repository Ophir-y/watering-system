interface Props {
  pct: number | null;
  lowPct: number;
  empty: boolean;
  width?: number;
  height?: number;
}

// Side view of a plant's bottle: water level in water-blue, the low mark as a hairline.
// The level is the node's fuel gauge (it counts what it pumps), not a measurement.
export function BottleGauge({ pct, lowPct, empty, width = 64, height = 96 }: Props) {
  const level = pct === null ? null : Math.min(100, Math.max(0, pct));
  const pad = width < 30 ? 2 : 4;
  const inner = height - 2 * pad;
  const top = level === null ? inner : inner - (level / 100) * inner;
  const lowY = pad + inner - (lowPct / 100) * inner;
  const label =
    level === null
      ? 'Bottle level unknown'
      : empty
        ? 'Bottle empty'
        : `Bottle about ${Math.round(level)}% full`;
  return (
    <svg width={width} height={height} viewBox={`0 0 ${width} ${height}`} role="img" aria-label={label}>
      <rect
        x="1"
        y="1"
        width={width - 2}
        height={height - 2}
        rx={width < 30 ? 4 : 10}
        fill="var(--surface-2)"
        stroke="var(--hairline-strong)"
      />
      {level !== null && (
        <rect
          x={pad}
          y={pad + top}
          width={width - 2 * pad}
          height={Math.max(empty ? 2 : 0, inner - top)}
          rx={width < 30 ? 2 : 7}
          fill={empty ? 'var(--critical)' : 'var(--water)'}
          opacity={empty ? 0.8 : 0.9}
        />
      )}
      <line
        x1={pad}
        x2={width - pad}
        y1={lowY}
        y2={lowY}
        stroke="var(--ink-2)"
        strokeWidth="1"
        opacity="0.5"
      />
    </svg>
  );
}
