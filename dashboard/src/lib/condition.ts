import type { PlantProfile, PlantStatus } from '../api/types';
import { formatDuration } from './format';

export type Tone = 'good' | 'warning' | 'serious' | 'critical' | 'water' | 'neutral';

export interface Condition {
  tone: Tone;
  label: string;
}

// One plain-language state per plant, most urgent first.
export function plantCondition(s: PlantStatus, p?: PlantProfile): Condition {
  if (!s.node) return { tone: 'neutral', label: 'No node' };
  if (s.repairRequested) return { tone: 'serious', label: 'Asks to pair again' };
  if (!s.online) {
    return s.lastSeenAt === null && s.bottle === null
      ? { tone: 'neutral', label: 'Waiting for its node' }
      : { tone: 'serious', label: 'Node offline' };
  }
  if (s.bottle?.empty) return { tone: 'critical', label: 'Bottle empty' };
  if (s.lockedOut) return { tone: 'critical', label: 'Pump locked' };
  if (s.battery && s.battery.pct <= 5) return { tone: 'critical', label: 'Batteries flat' };
  if (s.sensor === 'failed') {
    if (s.issue === 'uncalibrated') return { tone: 'neutral', label: 'Needs calibration' };
    if (s.issue === 'no_response') return { tone: 'serious', label: 'No moisture response' };
    return { tone: 'serious', label: 'Sensor problem' };
  }
  if (s.dosing || s.state === 'watering') return { tone: 'water', label: 'Watering' };
  if (s.state === 'soaking') return { tone: 'water', label: `Soaking · ${formatDuration(s.soakRemainingS)}` };
  if (s.state === 'waiting') return { tone: 'water', label: 'About to water' };
  if (s.queued.some((c) => c.kind === 'water')) return { tone: 'water', label: 'Water queued' };
  if (s.moisturePct === null) return { tone: 'neutral', label: 'Waiting for a reading' };
  if (p && s.moisturePct < p.moisture.critical) return { tone: 'critical', label: 'Very dry' };
  if (s.moisturePct < s.triggerPct) return { tone: 'warning', label: 'Due for water' };
  if (p && s.moisturePct > p.moisture.max) return { tone: 'warning', label: 'Too wet' };
  if (s.sensor === 'suspect') return { tone: 'serious', label: 'Check sensor' };
  if (s.bottle?.low) return { tone: 'warning', label: 'Refill soon' };
  if (s.battery?.low) return { tone: 'warning', label: 'Batteries low' };
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
  unknown: 'No report from the node yet',
};

export const EMPTY_REASON_TEXT: Record<string, string> = {
  none: '',
  gauge: 'the gauge counted it empty',
  pump_dry: 'the pump ran dry',
  no_response: 'watering stopped making the soil wetter',
};

export const PUMP_TEXT: Record<string, string> = {
  ok: 'Pumping normally',
  dry: 'Ran dry (sucked air)',
  disconnected: 'Drew no current: unplugged?',
  jammed: 'Drew far too much current: jammed?',
};
