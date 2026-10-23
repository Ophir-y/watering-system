import { Link } from 'wouter';

import type { PlantProfile, PlantStatus } from '../api/types';
import { findSpecies } from '../data/species';
import { plantCondition } from '../lib/condition';
import { formatAgo, formatMl, formatPct } from '../lib/format';
import { BottleGauge } from './BottleGauge';
import { ConditionChip } from './ConditionChip';
import { Icon } from './Icon';
import { SoilGauge } from './SoilGauge';

export function PlantCard({
  status,
  profile,
  now,
}: {
  status: PlantStatus;
  profile?: PlantProfile;
  now: number;
}) {
  const species = profile ? findSpecies(profile.species) : undefined;
  const band = profile?.moisture ?? {
    critical: 20,
    min: status.triggerPct,
    target: status.targetPct,
    max: 80,
  };
  const bottle = status.bottle;
  const battery = status.battery;

  return (
    <Link
      href={`/plants/${encodeURIComponent(status.id)}`}
      className="plant-card"
      data-highlight={status.buttonPressed || undefined}
    >
      <SoilGauge pct={status.moisturePct} band={band} trigger={status.triggerPct} state={status.state} />
      <div style={{ minWidth: 0 }}>
        <div className="name tag-name">{status.name}</div>
        {species && <div className="latin">{species.latin}</div>}
        <div className="reading">
          <strong>{formatPct(status.moisturePct)}</strong>
          <span className="muted small">target {formatPct(status.targetPct)}</span>
        </div>
        <ConditionChip condition={plantCondition(status, profile)} />
        <div className="meta">
          {status.dosing ? (
            <span>
              Pouring {Math.round(status.dosing.dispensedMl)} of {Math.round(status.dosing.ml)} ml
            </span>
          ) : (
            <span>Watered {formatAgo(status.lastWateredAt, now)}</span>
          )}
          <span>{formatMl(status.mlToday)} today</span>
        </div>
        {status.node && (
          <div className="node-line">
            {bottle && (
              <span title={`Bottle: about ${formatMl(bottle.ml)} of ${formatMl(bottle.capacityMl)}`}>
                <BottleGauge
                  pct={bottle.pct}
                  lowPct={profile?.bottle.lowPct ?? 25}
                  empty={bottle.empty}
                  width={12}
                  height={20}
                />
                {bottle.empty ? 'empty' : formatMl(bottle.ml)}
              </span>
            )}
            {battery && (
              <span
                data-tone={battery.low ? 'warning' : undefined}
                title={`${(battery.mv / 1000).toFixed(2)} V`}
              >
                <Icon name="battery" size={15} />
                {battery.pct}%
              </span>
            )}
            {!status.online && (
              <span data-tone="warning">
                <Icon name="signal" size={15} />
                offline
              </span>
            )}
          </div>
        )}
      </div>
    </Link>
  );
}
