const PATHS = {
  check: 'M5 12.5l4.5 4.5L19 7.5',
  warning: 'M12 3.5L2.5 20h19L12 3.5zM12 10v4.5M12 17.3v.2',
  critical: 'M8.2 3h7.6L21 8.2v7.6L15.8 21H8.2L3 15.8V8.2zM12 8v5M12 16.3v.2',
  drop: 'M12 3.2c3.5 4.3 6 7.6 6 10.8a6 6 0 0 1-12 0c0-3.2 2.5-6.5 6-10.8z',
  info: 'M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18zM12 11v5M12 7.8v.2',
  sprout: 'M12 21v-8M12 13c0-4 2.5-6.5 7-7 0 4.5-2.5 7-7 7zM12 15.5c0-3-2-5-6-5.5 0 3.5 2 5.5 6 5.5z',
  list: 'M9 6h11M9 12h11M9 18h11M4.5 6h.01M4.5 12h.01M4.5 18h.01',
  bell: 'M6 16v-5a6 6 0 0 1 12 0v5l1.5 2h-15zM10 20.5a2 2 0 0 0 4 0',
  sliders: 'M4 7h16M4 17h16M9 5v4M15 15v4',
  tank: 'M6 4h12v15a2 2 0 0 1-2 2H8a2 2 0 0 1-2-2zM6 13h12',
  thermometer: 'M10 4.5a2 2 0 0 1 4 0v9.8a4 4 0 1 1-4 0zM12 10v6',
  sun: 'M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8zM12 2.5v2M12 19.5v2M4.6 4.6L6 6M18 18l1.4 1.4M2.5 12h2M19.5 12h2M4.6 19.4L6 18M18 6l1.4-1.4',
  wifi: 'M2.5 9a14 14 0 0 1 19 0M6 12.5a9 9 0 0 1 12 0M9.5 16a4 4 0 0 1 5 0M12 19.5v.2',
  arrowLeft: 'M19 12H5M11 6l-6 6 6 6',
  plus: 'M12 5v14M5 12h14',
  clock: 'M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18zM12 7v5l3 2',
  edit: 'M4 20h4L19 9l-4-4L4 16zM13.5 6.5l4 4',
  x: 'M6 6l12 12M18 6L6 18',
  refill: 'M12 3v10M8 9l4 4 4-4M5 15v3a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2v-3',
  sd: 'M7 3h8l4 4v12a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2zM9 3v4M12 3v4',
  pause: 'M8 5v14M16 5v14',
  battery: 'M3 8h15a1 1 0 0 1 1 1v6a1 1 0 0 1-1 1H3a1 1 0 0 1-1-1V9a1 1 0 0 1 1-1zM22 11v2',
  signal: 'M4 19v-2M9 19v-6M14 19v-10M19 19V5',
  node: 'M8 4h8a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2H8a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2zM10 8h4M12 16v.2',
} as const;

export type IconName = keyof typeof PATHS | 'stop';

export function Icon({ name, size = 18, label }: { name: IconName; size?: number; label?: string }) {
  const a11y = label ? { role: 'img', 'aria-label': label } : { 'aria-hidden': true };
  if (name === 'stop') {
    return (
      <svg width={size} height={size} viewBox="0 0 24 24" {...a11y}>
        <rect x="6" y="6" width="12" height="12" rx="2" fill="currentColor" />
      </svg>
    );
  }
  return (
    <svg
      width={size}
      height={size}
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth={1.8}
      strokeLinecap="round"
      strokeLinejoin="round"
      {...a11y}
    >
      <path d={PATHS[name]} />
    </svg>
  );
}
