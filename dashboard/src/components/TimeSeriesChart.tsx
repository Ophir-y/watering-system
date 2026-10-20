import { useEffect, useRef, useState } from 'react';
import uPlot from 'uplot';
import 'uplot/dist/uPlot.min.css';

import { formatDateTime } from '../lib/format';

interface Props {
  times: number[];
  values: (number | null)[];
  label: string;
  format: (v: number) => string;
  band?: { lo: number; hi: number } | null;
  bandLabel?: string;
  markers?: number[];
  markerLabel?: string;
  yRange?: [number, number];
  height?: number;
  loading?: boolean;
  emptyText?: string;
}

function cssVar(name: string): string {
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
}

// Re-render charts when the theme changes (they paint with resolved colours).
function useThemeVersion(): number {
  const [v, setV] = useState(0);
  useEffect(() => {
    const bump = () => setV((x) => x + 1);
    const mo = new MutationObserver(bump);
    mo.observe(document.documentElement, { attributes: true, attributeFilter: ['data-theme'] });
    const mq = window.matchMedia?.('(prefers-color-scheme: dark)');
    mq?.addEventListener?.('change', bump);
    return () => {
      mo.disconnect();
      mq?.removeEventListener?.('change', bump);
    };
  }, []);
  return v;
}

// One series, one axis. Hover shows a crosshair readout; the table view below
// carries every value for keyboard and screen-reader users.
export function TimeSeriesChart({
  times,
  values,
  label,
  format,
  band,
  bandLabel,
  markers = [],
  markerLabel,
  yRange,
  height = 220,
  loading,
  emptyText = 'No readings in this period yet.',
}: Props) {
  const wrap = useRef<HTMLDivElement>(null);
  const tip = useRef<HTMLDivElement>(null);
  const theme = useThemeVersion();
  const hasData = values.some((v) => v !== null);

  useEffect(() => {
    const el = wrap.current;
    if (!el || !hasData || typeof window === 'undefined' || !('ResizeObserver' in window)) return;
    const series = cssVar('--chart-series');
    const grid = cssVar('--hairline');
    const muted = cssVar('--muted');
    const surface = cssVar('--surface');
    const bandFill = cssVar('--water-wash-strong');
    const markerFill = cssVar('--soil-3');
    const font = `11px ${cssVar('--font-ui') || 'system-ui'}`;
    const px = uPlot.pxRatio;

    const opts: uPlot.Options = {
      width: el.clientWidth || 600,
      height,
      padding: [10, 6, 0, 0],
      legend: { show: false },
      cursor: {
        y: false,
        drag: { x: false, y: false },
        points: { size: 9, width: 2, stroke: surface, fill: series },
      },
      scales: {
        x: { time: true },
        y: yRange
          ? { range: () => yRange }
          : {
              range: (_u, lo, hi) => [Math.floor(lo - (hi - lo) * 0.1), Math.ceil(hi + (hi - lo) * 0.1 + 1)],
            },
      },
      axes: [
        { stroke: muted, font, grid: { stroke: grid, width: 1 }, ticks: { show: false }, space: 70 },
        {
          stroke: muted,
          font,
          size: 48,
          grid: { stroke: grid, width: 1 },
          ticks: { show: false },
          values: (_u, vals) => vals.map((v) => format(v)),
        },
      ],
      series: [{}, { label, stroke: series, width: 2, points: { show: false }, spanGaps: false }],
      hooks: {
        drawClear: [
          (u) => {
            if (!band) return;
            const top = u.valToPos(band.hi, 'y', true);
            const bottom = u.valToPos(band.lo, 'y', true);
            u.ctx.fillStyle = bandFill;
            u.ctx.fillRect(u.bbox.left, top, u.bbox.width, bottom - top);
          },
        ],
        draw: [
          (u) => {
            const [x0, x1] = [u.scales.x.min ?? 0, u.scales.x.max ?? 0];
            u.ctx.fillStyle = markerFill;
            for (const t of markers) {
              if (t < x0 || t > x1) continue;
              const x = u.valToPos(t, 'x', true);
              const h = 9 * px;
              u.ctx.beginPath();
              u.ctx.roundRect(x - px, u.bbox.top + u.bbox.height - h, 2 * px, h, px);
              u.ctx.fill();
            }
          },
        ],
        setCursor: [
          (u) => {
            const t = tip.current;
            const idx = u.cursor.idx;
            if (!t) return;
            const v = idx === null || idx === undefined ? null : (u.data[1][idx] as number | null);
            if (v === null || v === undefined || idx === null || idx === undefined) {
              t.style.display = 'none';
              return;
            }
            t.replaceChildren();
            const val = document.createElement('div');
            val.className = 'v';
            val.append(document.createElement('i'), document.createTextNode(format(v)));
            const when = document.createElement('div');
            when.className = 't';
            when.textContent = `${label} · ${formatDateTime(u.data[0][idx] as number)}`;
            t.append(val, when);
            t.style.display = 'block';
            const left = (u.cursor.left ?? 0) + u.bbox.left / px;
            const flip = left > u.width - 170;
            t.style.left = `${flip ? left - t.offsetWidth - 12 : left + 12}px`;
            t.style.top = `${Math.max(0, u.valToPos(v, 'y') + u.bbox.top / px - 46)}px`;
          },
        ],
      },
    };

    const u = new uPlot(opts, [times, values] as uPlot.AlignedData, el);
    const ro = new ResizeObserver(() => u.setSize({ width: el.clientWidth, height }));
    ro.observe(el);
    const hide = () => tip.current && (tip.current.style.display = 'none');
    el.addEventListener('mouseleave', hide);
    return () => {
      ro.disconnect();
      el.removeEventListener('mouseleave', hide);
      u.destroy();
    };
    // band/markers/format are derived from the same props as times/values
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [times, values, theme, height, hasData]);

  const rows = times
    .map((t, i) => [t, values[i]] as const)
    .filter(([, v]) => v !== null)
    .reverse();

  return (
    <div>
      <div className={`chart${loading ? ' loading' : ''}`}>
        {hasData ? <div ref={wrap} /> : <div className="chart-empty">{loading ? 'Loading…' : emptyText}</div>}
        <div ref={tip} className="chart-tip" />
      </div>
      {hasData && (band || markers.length > 0) && (
        <div className="chart-legend">
          {band && (
            <span>
              <i className="key-band" /> {bandLabel ?? 'Target range'}
            </span>
          )}
          {markers.length > 0 && (
            <span>
              <i className="key-tick" /> {markerLabel ?? 'Watering'}
            </span>
          )}
        </div>
      )}
      {hasData && (
        <details className="table-view">
          <summary>Show as table</summary>
          <div className="table-scroll">
            <table className="data">
              <thead>
                <tr>
                  <th>Time</th>
                  <th className="num">{label}</th>
                </tr>
              </thead>
              <tbody>
                {rows.map(([t, v]) => (
                  <tr key={t}>
                    <td>{formatDateTime(t)}</td>
                    <td className="num">{format(v as number)}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </details>
      )}
    </div>
  );
}
