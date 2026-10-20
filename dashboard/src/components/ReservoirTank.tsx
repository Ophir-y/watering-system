interface Props {
  pct: number | null;
  lowPct: number;
  empty: boolean;
  width?: number;
  height?: number;
}

// Side view of the reservoir: water level in water-blue, the low mark as a hairline.
export function ReservoirTank({ pct, lowPct, empty, width = 64, height = 96 }: Props) {
  const level = pct === null ? null : Math.min(100, Math.max(0, pct));
  const inner = height - 8;
  const top = level === null ? inner : inner - (level / 100) * inner;
  const lowY = 4 + inner - (lowPct / 100) * inner;
  const label = level === null ? 'Reservoir level unknown' : `Reservoir ${Math.round(level)}% full`;
  return (
    <svg width={width} height={height} viewBox={`0 0 ${width} ${height}`} role="img" aria-label={label}>
      <rect
        x="1"
        y="1"
        width={width - 2}
        height={height - 2}
        rx="10"
        fill="var(--surface-2)"
        stroke="var(--hairline-strong)"
      />
      {level !== null && (
        <rect
          x="4"
          y={4 + top}
          width={width - 8}
          height={Math.max(0, inner - top)}
          rx="7"
          fill={empty ? 'var(--critical)' : 'var(--water)'}
          opacity={empty ? 0.8 : 0.9}
        />
      )}
      <line x1="4" x2={width - 4} y1={lowY} y2={lowY} stroke="var(--ink-2)" strokeWidth="1" opacity="0.5" />
    </svg>
  );
}
