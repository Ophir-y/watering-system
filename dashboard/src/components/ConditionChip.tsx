import type { Condition, Tone } from '../lib/condition';
import { Icon, type IconName } from './Icon';

const ICON: Record<Tone, IconName> = {
  good: 'check',
  warning: 'warning',
  serious: 'warning',
  critical: 'critical',
  water: 'drop',
  neutral: 'info',
};

export function ConditionChip({ condition }: { condition: Condition }) {
  return (
    <span className="chip" data-tone={condition.tone}>
      <Icon name={ICON[condition.tone]} size={14} />
      {condition.label}
    </span>
  );
}

export function SeverityIcon({
  severity,
  size = 18,
}: {
  severity: 'info' | 'warning' | 'critical';
  size?: number;
}) {
  const color =
    severity === 'critical' ? 'var(--critical)' : severity === 'warning' ? 'var(--warning)' : 'var(--water)';
  return (
    <span style={{ color, display: 'inline-flex' }}>
      <Icon
        name={severity === 'critical' ? 'critical' : severity === 'warning' ? 'warning' : 'info'}
        size={size}
        label={severity}
      />
    </span>
  );
}
