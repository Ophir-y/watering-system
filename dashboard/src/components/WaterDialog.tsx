import { useState } from 'react';

import { api } from '../api/client';
import type { PlantProfile } from '../api/types';
import { formatEta } from '../lib/format';
import { useDevice } from '../state/device';
import { useToast } from '../state/toast';
import { Dialog } from './Dialog';
import { NumberField } from './Fields';

export function WaterDialog({
  plant,
  open,
  onClose,
}: {
  plant: PlantProfile;
  open: boolean;
  onClose: () => void;
}) {
  const [ml, setMl] = useState(Math.min(plant.maxPulseMl, 100));
  const [busy, setBusy] = useState(false);
  const { refresh } = useDevice();
  const toast = useToast();
  const presets = [25, 50, 100, 200].filter((v) => v <= 250);

  const water = async () => {
    setBusy(true);
    try {
      const q = await api.water(plant.id, ml);
      toast.show(`${plant.name}: ${ml} ml queued, poured ${formatEta(q.etaS)}`);
      onClose();
      await refresh();
    } catch (e) {
      toast.error(e);
    } finally {
      setBusy(false);
    }
  };

  return (
    <Dialog open={open} onClose={onClose} labelledBy="water-title">
      <h2 id="water-title">
        Water <span className="tag-name">{plant.name}</span> now
      </h2>
      <p className="ink-2 small">
        Ignores the schedule but still respects the safety limits. Counts towards today&apos;s total. The
        plant&apos;s node pours it when it next checks in.
      </p>
      <div className="stack" style={{ marginTop: 14 }}>
        <div className="presets">
          {presets.map((p) => (
            <button key={p} className="btn btn-small" aria-pressed={ml === p} onClick={() => setMl(p)}>
              {p} ml
            </button>
          ))}
        </div>
        <NumberField label="Amount" value={ml} onChange={setMl} unit="ml" min={5} max={250} />
      </div>
      <div className="row">
        <button className="btn btn-ghost" onClick={onClose}>
          Cancel
        </button>
        <button className="btn btn-water" disabled={busy || !(ml >= 5)} onClick={water}>
          Water {Number.isFinite(ml) ? ml : ''} ml
        </button>
      </div>
    </Dialog>
  );
}
