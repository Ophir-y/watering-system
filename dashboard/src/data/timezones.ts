// IANA zone -> POSIX TZ rule used by the ESP32's C library. Covers common zones;
// anything else falls back to a fixed offset without daylight saving.
export const TIMEZONES: Record<string, string> = {
  UTC: 'UTC0',
  'Europe/London': 'GMT0BST,M3.5.0/1,M10.5.0',
  'Europe/Dublin': 'IST-1GMT0,M10.5.0,M3.5.0/1',
  'Europe/Lisbon': 'WET0WEST,M3.5.0/1,M10.5.0',
  'Europe/Amsterdam': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Berlin': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Brussels': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Copenhagen': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Madrid': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Oslo': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Paris': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Prague': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Rome': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Stockholm': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Vienna': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Warsaw': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Zurich': 'CET-1CEST,M3.5.0,M10.5.0/3',
  'Europe/Athens': 'EET-2EEST,M3.5.0/3,M10.5.0/4',
  'Europe/Bucharest': 'EET-2EEST,M3.5.0/3,M10.5.0/4',
  'Europe/Helsinki': 'EET-2EEST,M3.5.0/3,M10.5.0/4',
  'Europe/Kyiv': 'EET-2EEST,M3.5.0/3,M10.5.0/4',
  'Europe/Sofia': 'EET-2EEST,M3.5.0/3,M10.5.0/4',
  'Europe/Istanbul': '<+03>-3',
  'Europe/Moscow': 'MSK-3',
  'Asia/Jerusalem': 'IST-2IDT,M3.4.4/26,M10.5.0',
  'Asia/Dubai': '<+04>-4',
  'Asia/Tehran': '<+0330>-3:30',
  'Asia/Karachi': 'PKT-5',
  'Asia/Kolkata': 'IST-5:30',
  'Asia/Dhaka': '<+06>-6',
  'Asia/Bangkok': '<+07>-7',
  'Asia/Jakarta': 'WIB-7',
  'Asia/Singapore': '<+08>-8',
  'Asia/Shanghai': 'CST-8',
  'Asia/Hong_Kong': 'HKT-8',
  'Asia/Taipei': 'CST-8',
  'Asia/Manila': 'PST-8',
  'Asia/Seoul': 'KST-9',
  'Asia/Tokyo': 'JST-9',
  'Australia/Perth': 'AWST-8',
  'Australia/Adelaide': 'ACST-9:30ACDT,M10.1.0,M4.1.0/3',
  'Australia/Brisbane': 'AEST-10',
  'Australia/Melbourne': 'AEST-10AEDT,M10.1.0,M4.1.0/3',
  'Australia/Sydney': 'AEST-10AEDT,M10.1.0,M4.1.0/3',
  'Pacific/Auckland': 'NZST-12NZDT,M9.5.0,M4.1.0/3',
  'Africa/Cairo': 'EET-2EEST,M4.5.5/0,M10.5.4/24',
  'Africa/Johannesburg': 'SAST-2',
  'Africa/Lagos': 'WAT-1',
  'Africa/Nairobi': 'EAT-3',
  'America/St_Johns': 'NST3:30NDT,M3.2.0,M11.1.0',
  'America/Halifax': 'AST4ADT,M3.2.0,M11.1.0',
  'America/New_York': 'EST5EDT,M3.2.0,M11.1.0',
  'America/Toronto': 'EST5EDT,M3.2.0,M11.1.0',
  'America/Chicago': 'CST6CDT,M3.2.0,M11.1.0',
  'America/Denver': 'MST7MDT,M3.2.0,M11.1.0',
  'America/Phoenix': 'MST7',
  'America/Los_Angeles': 'PST8PDT,M3.2.0,M11.1.0',
  'America/Vancouver': 'PST8PDT,M3.2.0,M11.1.0',
  'America/Anchorage': 'AKST9AKDT,M3.2.0,M11.1.0',
  'Pacific/Honolulu': 'HST10',
  'America/Mexico_City': 'CST6',
  'America/Bogota': '<-05>5',
  'America/Lima': '<-05>5',
  'America/Santiago': '<-04>4<-03>,M9.1.6/24,M4.1.6/24',
  'America/Sao_Paulo': '<-03>3',
  'America/Argentina/Buenos_Aires': '<-03>3',
};

// Southern-hemisphere zones flip the dormancy months.
const SOUTHERN =
  /^(Australia|Pacific\/Auckland|Africa\/Johannesburg|America\/(Santiago|Sao_Paulo|Argentina|Lima))/;

export function browserTimezone(): string {
  try {
    return Intl.DateTimeFormat().resolvedOptions().timeZone || 'UTC';
  } catch {
    return 'UTC';
  }
}

export interface ZoneChoice {
  tzName: string;
  timezone: string;
  southernHemisphere: boolean;
  exact: boolean;
}

export function zoneFor(tzName: string, now = new Date()): ZoneChoice {
  const known = TIMEZONES[tzName];
  if (known) return { tzName, timezone: known, southernHemisphere: SOUTHERN.test(tzName), exact: true };
  // Fixed offset from the browser's current offset (POSIX signs are inverted).
  const offsetMin = -now.getTimezoneOffset();
  const sign = offsetMin >= 0 ? '-' : '';
  const abs = Math.abs(offsetMin);
  const hh = Math.floor(abs / 60);
  const mm = abs % 60;
  const label = `${offsetMin >= 0 ? '+' : '-'}${String(hh).padStart(2, '0')}${mm ? String(mm).padStart(2, '0') : ''}`;
  return {
    tzName,
    timezone: `<${label}>${sign}${hh}${mm ? `:${String(mm).padStart(2, '0')}` : ''}`,
    southernHemisphere: false,
    exact: false,
  };
}
