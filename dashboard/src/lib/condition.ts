import type { PlantProfile, PlantStatus } from '../api/types';
import { formatDuration } from './format';

export type Tone = 'good' | 'warning' | 'serious' | 'critical' | 'water' | 'neutral';

export interface Condition {
  tone: Tone;
  label: string;
}

// One plain-language state per plant, most urgent first.
export function plantCondition(s: PlantStatus, p?: PlantProfile): Condition {
  if (s.lockedOut) return { tone: 'critical', label: 'Outlet locked' };
  if (s.sensor === 'failed') {
    if (s.issue === 'uncalibrated') return { tone: 'neutral', label: 'Needs calibration' };
    if (s.issue === 'no_response') return { tone: 'serious', label: 'No moisture response' };
    return { tone: 'serious', label: 'Sensor problem' };
  }
  if (s.state === 'watering') return { tone: 'water', label: 'Watering' };
  if (s.state === 'soaking') return { tone: 'water', label: `Soaking · ${formatDuration(s.soakRemainingS)}` };
  if (s.state === 'waiting') return { tone: 'water', label: 'Next in line' };
  if (s.moisturePct === null) return { tone: 'neutral', label: 'Waiting for a reading' };
  if (p && s.moisturePct < p.moisture.critical) return { tone: 'critical', label: 'Very dry' };
  if (s.moisturePct < s.triggerPct) return { tone: 'warning', label: 'Due for water' };
  if (p && s.moisturePct > p.moisture.max) return { tone: 'warning', label: 'Too wet' };
  if (s.sensor === 'suspect') return { tone: 'serious', label: 'Check sensor' };
  return { tone: 'good', label: 'In range' };
}

export function needsAttention(c: Condition): boolean {
  return c.tone === 'warning' || c.tone === 'serious' || c.tone === 'critical';
}

export const ISSUE_TEXT: Record<string, string> = {
  ok: 'Working',
  uncalibrated: 'Not calibrated yet',
  bus_error: 'Not responding (ADC read error)',
  disconnected: 'Looks disconnected or shorted',
  out_of_range: 'Reads far outside its calibration',
  no_response: 'Moisture did not rise after watering',
  stuck: 'Reading has not changed for 12 hours',
  erratic: 'Readings jump erratically',
};
