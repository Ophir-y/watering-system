export function formatMl(ml: number): string {
  if (!Number.isFinite(ml)) return '–';
  if (Math.abs(ml) >= 1000) return `${(ml / 1000).toFixed(ml >= 10000 ? 0 : 1)} L`;
  return `${Math.round(ml)} ml`;
}

export function formatPct(pct: number | null | undefined, digits = 0): string {
  return pct === null || pct === undefined || !Number.isFinite(pct) ? '–' : `${pct.toFixed(digits)}%`;
}

export function formatLux(lux: number | null | undefined): string {
  if (lux === null || lux === undefined) return '–';
  if (lux >= 10000) return `${Math.round(lux / 1000)}k lx`;
  if (lux >= 1000) return `${(lux / 1000).toFixed(1)}k lx`;
  return `${Math.round(lux)} lx`;
}

export function formatTemp(c: number | null | undefined): string {
  return c === null || c === undefined ? '–' : `${c.toFixed(1)} °C`;
}

export function formatDuration(seconds: number): string {
  const s = Math.max(0, Math.round(seconds));
  if (s < 60) return `${s} s`;
  const m = Math.round(s / 60);
  if (m < 60) return `${m} min`;
  const h = Math.floor(m / 60);
  const rem = m % 60;
  if (h < 48) return rem ? `${h} h ${rem} min` : `${h} h`;
  return `${Math.round(h / 24)} days`;
}

// When a queued command runs: "at the next check-in (in about 3 min)".
export function formatEta(etaS: number | null | undefined): string {
  if (etaS === null || etaS === undefined) return 'when the node next checks in';
  if (etaS < 20) return 'within moments';
  return `at the next check-in, in about ${formatDuration(etaS)}`;
}

// "3 h ago", "just now", "in 2 days" relative to `now` (unix seconds).
export function formatAgo(ts: number | null | undefined, now: number): string {
  if (!ts) return 'never';
  const d = now - ts;
  if (Math.abs(d) < 60) return 'just now';
  return d > 0 ? `${formatDuration(d)} ago` : `in ${formatDuration(-d)}`;
}

export function formatDateTime(ts: number, opts: Intl.DateTimeFormatOptions = {}): string {
  return new Date(ts * 1000).toLocaleString(undefined, {
    month: 'short',
    day: 'numeric',
    hour: '2-digit',
    minute: '2-digit',
    ...opts,
  });
}

export function formatTime(ts: number): string {
  return new Date(ts * 1000).toLocaleTimeString(undefined, { hour: '2-digit', minute: '2-digit' });
}

export function formatRange(r: { min: number; max: number }, unit: string): string {
  return `${r.min}–${r.max}${unit}`;
}

const MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];

export function formatMonths(months: number[]): string {
  if (!months.length) return 'none';
  return months
    .slice()
    .sort((a, b) => a - b)
    .map((m) => MONTHS[m - 1])
    .join(', ');
}

export function monthName(m: number): string {
  return MONTHS[m - 1] ?? '';
}
