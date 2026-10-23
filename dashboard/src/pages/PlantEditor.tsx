import { useMemo, useState } from 'react';
import { Link, useLocation } from 'wouter';

import { api } from '../api/client';
import type { PlantProfile, WateringStyle } from '../api/types';
import { Dialog } from '../components/Dialog';
import { Field, NumberField, SelectField, TextField, Toggle } from '../components/Fields';
import { Icon } from '../components/Icon';
import { SoilGauge } from '../components/SoilGauge';
import {
  SPECIES,
  STYLE_LABEL,
  applySpecies,
  defaultProfile,
  findSpecies,
  limitsForPot,
} from '../data/species';
import { monthName } from '../lib/format';
import { validatePlant } from '../lib/validate';
import { useDevice, useStatus } from '../state/device';
import { useToast } from '../state/toast';

// node: a plant node waiting to be added (from "New plant node found").
export function PlantEditor({ id, node }: { id?: string; node?: string }) {
  const { plants, reloadPlants, refresh } = useDevice();
  const status = useStatus();
  const existing = id ? plants.find((p) => p.id === id) : undefined;
  const waiting = status.newNodes.map((n) => n.id);

  const [draft, setDraft] = useState<PlantProfile>(() =>
    existing ? structuredClone(existing) : defaultProfile(node ?? waiting[0] ?? null),
  );
  const nodeOptions = [
    ...new Set([...(existing?.node ? [existing.node] : []), ...(draft.node ? [draft.node] : []), ...waiting]),
  ];
  const [limitsTouched, setLimitsTouched] = useState(Boolean(existing));
  const [query, setQuery] = useState('');
  const [saving, setSaving] = useState(false);
  const [confirmDelete, setConfirmDelete] = useState(false);
  const [showErrors, setShowErrors] = useState(false);
  const [, navigate] = useLocation();
  const toast = useToast();

  const errors = useMemo(() => validatePlant(draft), [draft]);
  const err = (k: string) => (showErrors ? errors[k] : undefined);
  const species = findSpecies(draft.species);
  const set = <K extends keyof PlantProfile>(k: K, v: PlantProfile[K]) => setDraft((d) => ({ ...d, [k]: v }));
  const setLimit = <K extends 'maxPulseMl' | 'maxDailyMl' | 'soakMin' | 'fallbackMl'>(k: K, v: number) => {
    setLimitsTouched(true);
    set(k, v);
  };

  const matches = useMemo(() => {
    const q = query.trim().toLowerCase();
    return q ? SPECIES.filter((s) => `${s.common} ${s.latin} ${s.group}`.toLowerCase().includes(q)) : SPECIES;
  }, [query]);

  if (id && !existing) {
    return (
      <div className="page">
        <p>That plant does not exist.</p>
        <Link href="/">Back to all plants</Link>
      </div>
    );
  }

  const save = async () => {
    setShowErrors(true);
    if (Object.keys(errors).length) {
      toast.show('Fix the highlighted fields first.');
      return;
    }
    setSaving(true);
    try {
      const saved = existing
        ? await api.updatePlant(existing.id, draft)
        : await api.createPlant({ ...draft, id: undefined });
      await Promise.all([reloadPlants(), refresh()]);
      toast.show(existing ? 'Changes saved' : `${saved.name} added`);
      navigate(`/plants/${encodeURIComponent(saved.id)}`);
    } catch (e) {
      toast.error(e);
    } finally {
      setSaving(false);
    }
  };

  const remove = async () => {
    if (!existing) return;
    try {
      await api.deletePlant(existing.id);
      await Promise.all([reloadPlants(), refresh()]);
      toast.show(`${existing.name} removed`);
      navigate('/');
    } catch (e) {
      toast.error(e);
    }
  };

  const m = draft.moisture;
  const setBand = (k: keyof PlantProfile['moisture'], v: number) => set('moisture', { ...m, [k]: v });

  return (
    <div className="page">
      <Link
        href={existing ? `/plants/${encodeURIComponent(existing.id)}` : '/'}
        className="btn btn-ghost btn-small"
        style={{ width: 'max-content' }}
      >
        <Icon name="arrowLeft" size={16} /> {existing ? existing.name : 'All plants'}
      </Link>
      <div className="page-head">
        <div>
          <h1>{existing ? `Edit ${existing.name}` : 'Add a plant'}</h1>
          <p>Pick a species to fill in sensible starting values, then adjust anything you like.</p>
        </div>
      </div>

      <div className="form">
        <section className="card form-section">
          <h2>Plant</h2>
          <div className="fields">
            <TextField
              label="Name"
              value={draft.name}
              onChange={(v) => set('name', v)}
              maxLength={40}
              error={err('name')}
              placeholder="e.g. Kitchen basil"
            />
            <SelectField
              label="Plant node"
              value={draft.node ?? ''}
              onChange={(v) => set('node', v || null)}
              options={[
                ...nodeOptions.map((n) => ({
                  value: n,
                  label: n === existing?.node ? `${n} (this plant's)` : `${n} (waiting to be added)`,
                })),
                { value: '', label: 'None: nothing waters this plant' },
              ]}
              hint={
                nodeOptions.length
                  ? "The node in this plant's pot. It pairs at its next check-in."
                  : 'Put batteries in a node: it shows up here within a minute.'
              }
            />
          </div>
          <Field
            label={species ? `Species: ${species.common}` : 'Species'}
            hint="Choosing one copies its care values below."
          >
            {(fid) => (
              <input
                id={fid}
                className="input"
                type="search"
                placeholder="Search 40+ plants: monstera, fern, basil…"
                value={query}
                onChange={(e) => setQuery(e.target.value)}
              />
            )}
          </Field>
          <div className="species-list" role="listbox" aria-label="Species">
            {matches.map((s) => (
              <button
                key={s.id}
                type="button"
                role="option"
                aria-selected={draft.species === s.id}
                aria-pressed={draft.species === s.id}
                onClick={() => setDraft((d) => applySpecies(d, s))}
              >
                <span>{s.common}</span>
                <span className="light">{s.light}</span>
                <span className="latin">{s.latin}</span>
              </button>
            ))}
            {matches.length === 0 && (
              <p className="muted small" style={{ padding: 12 }}>
                No match. You can still set everything by hand.
              </p>
            )}
          </div>
          <div className="row">
            <Toggle label="Enabled" checked={draft.enabled} onChange={(v) => set('enabled', v)} />
            <Toggle
              label="Water automatically"
              checked={draft.autoWater}
              onChange={(v) => set('autoWater', v)}
              hint={draft.autoWater ? undefined : 'Only waters when you press Water now.'}
            />
          </div>
        </section>

        <section className="card form-section">
          <h2>Moisture</h2>
          <p>
            Percentages of the sensor&apos;s calibrated range: 0 is its dry point, 100 is standing in water.
          </p>
          <div style={{ display: 'grid', gridTemplateColumns: 'auto 1fr', gap: 24, alignItems: 'start' }}>
            <SoilGauge pct={m.target} band={m} trigger={m.min} height={150} />
            <div className="stack">
              <div className="fields">
                <NumberField
                  label="Very dry (alert)"
                  value={m.critical}
                  onChange={(v) => setBand('critical', v)}
                  unit="%"
                />
                <NumberField
                  label="Start watering below"
                  value={m.min}
                  onChange={(v) => setBand('min', v)}
                  unit="%"
                />
                <NumberField
                  label="Water up to"
                  value={m.target}
                  onChange={(v) => setBand('target', v)}
                  unit="%"
                />
                <NumberField
                  label="Too wet above"
                  value={m.max}
                  onChange={(v) => setBand('max', v)}
                  unit="%"
                />
              </div>
              {err('moisture') && (
                <p className="small" style={{ color: 'var(--critical-ink)' }}>
                  {err('moisture')}
                </p>
              )}
              <SelectField<WateringStyle>
                label="Watering style"
                value={draft.style}
                onChange={(v) => set('style', v)}
                options={(Object.keys(STYLE_LABEL) as WateringStyle[]).map((k) => ({
                  value: k,
                  label: STYLE_LABEL[k],
                }))}
                hint={
                  draft.style === 'soak_and_dry'
                    ? 'Each session waters close to “too wet”, then lets it dry fully.'
                    : undefined
                }
              />
            </div>
          </div>
        </section>

        <section className="card form-section">
          <h2>Pot, bottle and pump</h2>
          <div className="fields">
            <NumberField
              label="Pot volume"
              value={draft.potVolumeMl}
              unit="ml"
              step={100}
              error={err('potVolumeMl')}
              onChange={(v) =>
                setDraft((d) => ({
                  ...d,
                  potVolumeMl: v,
                  ...(limitsTouched || !Number.isFinite(v) ? {} : limitsForPot(v)),
                }))
              }
            />
            <NumberField
              label="Pump flow"
              value={draft.pump.flowMlPerS}
              unit="ml/s"
              step={0.1}
              error={err('flow')}
              hint="Measure it on the plant page after saving."
              onChange={(v) => set('pump', { ...draft.pump, flowMlPerS: v })}
            />
          </div>
          <div className="fields">
            <NumberField
              label="Bottle holds"
              value={draft.bottle.capacityMl}
              unit="ml"
              step={50}
              error={err('capacityMl')}
              hint="What you pour in to refill it from empty."
              onChange={(v) => set('bottle', { ...draft.bottle, capacityMl: v })}
            />
            <NumberField
              label="Refill reminder below"
              value={draft.bottle.lowPct}
              unit="%"
              error={err('lowPct')}
              onChange={(v) => set('bottle', { ...draft.bottle, lowPct: v })}
            />
            <NumberField
              label="Keep in reserve"
              value={draft.bottle.reserveMl}
              unit="ml"
              step={10}
              error={err('reserveMl')}
              hint="Until the pump has learned its current, the gauge stops here so the pump never sucks air."
              onChange={(v) => set('bottle', { ...draft.bottle, reserveMl: v })}
            />
          </div>
          <Toggle
            label="Feel the pump run dry"
            checked={draft.pump.currentSense}
            onChange={(v) => set('pump', { ...draft.pump, currentSense: v })}
            hint="Needs the 1 Ω resistor in the pump wire. The node then notices an empty bottle, an unplugged or a jammed pump by the pump's current."
          />
          <div className="fields">
            <NumberField
              label="Largest pulse"
              value={draft.maxPulseMl}
              unit="ml"
              step={5}
              error={err('maxPulseMl')}
              onChange={(v) => setLimit('maxPulseMl', v)}
            />
            <NumberField
              label="Most per day"
              value={draft.maxDailyMl}
              unit="ml"
              step={10}
              error={err('maxDailyMl')}
              onChange={(v) => setLimit('maxDailyMl', v)}
            />
            <NumberField
              label="Soak between pulses"
              value={draft.soakMin}
              unit="min"
              error={err('soakMin')}
              hint="Time for water to reach the sensor before the next pulse."
              onChange={(v) => setLimit('soakMin', v)}
            />
            <NumberField
              label="Rest between sessions"
              value={draft.minIntervalH}
              unit="h"
              step={0.5}
              error={err('minIntervalH')}
              onChange={(v) => set('minIntervalH', v)}
            />
          </div>
        </section>

        <section className="card form-section">
          <h2>Light, temperature and humidity</h2>
          <p>Used for recommendations and alerts; hot, dry air also makes watering start a little sooner.</p>
          <div className="fields">
            <NumberField
              label="Light from"
              value={draft.lightLux.min}
              unit="lx"
              step={500}
              error={err('light')}
              onChange={(v) => set('lightLux', { ...draft.lightLux, min: v })}
            />
            <NumberField
              label="Light to"
              value={draft.lightLux.max}
              unit="lx"
              step={500}
              onChange={(v) => set('lightLux', { ...draft.lightLux, max: v })}
            />
            <NumberField
              label="Temperature from"
              value={draft.tempC.min}
              unit="°C"
              error={err('temp')}
              onChange={(v) => set('tempC', { ...draft.tempC, min: v })}
            />
            <NumberField
              label="Temperature to"
              value={draft.tempC.max}
              unit="°C"
              onChange={(v) => set('tempC', { ...draft.tempC, max: v })}
            />
            <NumberField
              label="Humidity from"
              value={draft.humidityPct.min}
              unit="%"
              error={err('humidity')}
              onChange={(v) => set('humidityPct', { ...draft.humidityPct, min: v })}
            />
            <NumberField
              label="Humidity to"
              value={draft.humidityPct.max}
              unit="%"
              onChange={(v) => set('humidityPct', { ...draft.humidityPct, max: v })}
            />
          </div>
        </section>

        <section className="card form-section">
          <h2>Resting season</h2>
          <p>Months when the plant grows slowly. Doses are scaled down and watering starts a little later.</p>
          <div className="month-picker" role="group" aria-label="Resting months">
            {Array.from({ length: 12 }, (_, i) => i + 1).map((mo) => {
              const on = draft.dormantMonths.includes(mo);
              return (
                <button
                  key={mo}
                  type="button"
                  aria-pressed={on}
                  onClick={() =>
                    set(
                      'dormantMonths',
                      on
                        ? draft.dormantMonths.filter((x) => x !== mo)
                        : [...draft.dormantMonths, mo].sort((a, b) => a - b),
                    )
                  }
                >
                  {monthName(mo)}
                </button>
              );
            })}
          </div>
          <div className="fields">
            <NumberField
              label="Dose while resting"
              value={Math.round(draft.dormancyFactor * 100)}
              unit="%"
              step={5}
              error={err('dormancyFactor')}
              onChange={(v) => set('dormancyFactor', v / 100)}
            />
          </div>
        </section>

        <section className="card form-section">
          <h2>If the sensor fails</h2>
          <SelectField
            label="Then"
            value={draft.onSensorFault}
            onChange={(v) => set('onSensorFault', v)}
            options={[
              { value: 'pause', label: 'Stop watering and alert me' },
              { value: 'fallback_schedule', label: 'Water on a fixed schedule' },
            ]}
            hint="A fixed schedule keeps the plant alive while you replace the sensor; it never runs if water did not reach the pot."
          />
          {draft.onSensorFault === 'fallback_schedule' && (
            <div className="fields">
              <NumberField
                label="Every"
                value={draft.fallbackEveryDays}
                unit="days"
                step={0.5}
                error={err('fallbackEveryDays')}
                onChange={(v) => set('fallbackEveryDays', v)}
              />
              <NumberField
                label="Amount"
                value={draft.fallbackMl}
                unit="ml"
                step={10}
                error={err('fallbackMl')}
                onChange={(v) => setLimit('fallbackMl', v)}
              />
            </div>
          )}
        </section>

        <section className="card form-section">
          <h2>Notes</h2>
          <div className="fields">
            <TextField label="Soil" value={draft.soil} onChange={(v) => set('soil', v)} maxLength={120} />
          </div>
          <Field label="Care notes" error={err('careNotes')}>
            {(fid) => (
              <textarea
                id={fid}
                className="input"
                value={draft.careNotes}
                maxLength={1000}
                onChange={(e) => set('careNotes', e.target.value)}
              />
            )}
          </Field>
        </section>

        <div className="form-actions">
          {existing && (
            <button
              className="btn btn-ghost btn-danger-text"
              onClick={() => setConfirmDelete(true)}
              style={{ marginRight: 'auto' }}
            >
              Remove plant
            </button>
          )}
          <Link
            href={existing ? `/plants/${encodeURIComponent(existing.id)}` : '/'}
            className="btn btn-ghost"
          >
            Cancel
          </Link>
          <button className="btn btn-water" onClick={save} disabled={saving}>
            {existing ? 'Save changes' : 'Add plant'}
          </button>
        </div>
      </div>

      <Dialog open={confirmDelete} onClose={() => setConfirmDelete(false)} labelledBy="del-title">
        <h2 id="del-title">Remove {existing?.name}?</h2>
        <p className="ink-2">Its channel stops watering right away. Logged history stays on the SD card.</p>
        <div className="row">
          <button className="btn btn-ghost" onClick={() => setConfirmDelete(false)}>
            Keep it
          </button>
          <button className="btn btn-stop" onClick={remove}>
            Remove plant
          </button>
        </div>
      </Dialog>
    </div>
  );
}
