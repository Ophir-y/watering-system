import { Link } from 'wouter';

import type { PlantProfile, PlantStatus, Status } from '../api/types';
import { findSpecies } from '../data/species';
import { plantCondition } from '../lib/condition';
import { formatAgo, formatMl, formatPct } from '../lib/format';
import { ConditionChip } from './ConditionChip';
import { SoilGauge } from './SoilGauge';

export function PlantCard({
  status,
  profile,
  dose,
  now,
}: {
  status: PlantStatus;
  profile?: PlantProfile;
  dose: Status['dose'];
  now: number;
}) {
  const species = profile ? findSpecies(profile.species) : undefined;
  const band = profile?.moisture ?? {
    critical: 20,
    min: status.triggerPct,
    target: status.targetPct,
    max: 80,
  };
  const watering = dose && dose.plantId === status.id ? dose : null;

  return (
    <Link href={`/plants/${encodeURIComponent(status.id)}`} className="plant-card">
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
          {watering ? (
            <span>
              Pouring {Math.round(watering.dispensedMl)} of {Math.round(watering.ml)} ml
            </span>
          ) : (
            <span>Watered {formatAgo(status.lastWateredAt, now)}</span>
          )}
          <span>{formatMl(status.mlToday)} today</span>
        </div>
      </div>
    </Link>
  );
}
