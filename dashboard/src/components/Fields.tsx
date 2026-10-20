import { useId, type ReactNode } from 'react';

interface FieldProps {
  label: string;
  hint?: ReactNode;
  error?: string | null;
  children: (id: string) => ReactNode;
}

export function Field({ label, hint, error, children }: FieldProps) {
  const id = useId();
  return (
    <label className="field" htmlFor={id}>
      <span>{label}</span>
      {children(id)}
      {error ? <span className="error">{error}</span> : hint ? <span className="hint">{hint}</span> : null}
    </label>
  );
}

interface NumberFieldProps {
  label: string;
  value: number;
  onChange: (v: number) => void;
  unit?: string;
  min?: number;
  max?: number;
  step?: number;
  hint?: ReactNode;
  error?: string | null;
}

export function NumberField({
  label,
  value,
  onChange,
  unit,
  min,
  max,
  step = 1,
  hint,
  error,
}: NumberFieldProps) {
  return (
    <Field label={label} hint={hint} error={error}>
      {(id) => (
        <span className="input-unit">
          <input
            id={id}
            className="input num"
            type="number"
            inputMode="decimal"
            value={Number.isFinite(value) ? value : ''}
            min={min}
            max={max}
            step={step}
            onChange={(e) => onChange(e.target.value === '' ? NaN : Number(e.target.value))}
          />
          {unit && <span>{unit}</span>}
        </span>
      )}
    </Field>
  );
}

interface TextFieldProps {
  label: string;
  value: string;
  onChange: (v: string) => void;
  type?: string;
  placeholder?: string;
  hint?: ReactNode;
  error?: string | null;
  autoComplete?: string;
  maxLength?: number;
}

export function TextField({
  label,
  value,
  onChange,
  type = 'text',
  placeholder,
  hint,
  error,
  autoComplete,
  maxLength,
}: TextFieldProps) {
  return (
    <Field label={label} hint={hint} error={error}>
      {(id) => (
        <input
          id={id}
          className="input"
          type={type}
          value={value}
          placeholder={placeholder}
          autoComplete={autoComplete}
          maxLength={maxLength}
          onChange={(e) => onChange(e.target.value)}
        />
      )}
    </Field>
  );
}

interface SelectFieldProps<T extends string | number> {
  label: string;
  value: T;
  options: { value: T; label: string; disabled?: boolean }[];
  onChange: (v: T) => void;
  hint?: ReactNode;
}

export function SelectField<T extends string | number>({
  label,
  value,
  options,
  onChange,
  hint,
}: SelectFieldProps<T>) {
  return (
    <Field label={label} hint={hint}>
      {(id) => (
        <select
          id={id}
          className="input"
          value={String(value)}
          onChange={(e) => {
            const opt = options.find((o) => String(o.value) === e.target.value);
            if (opt) onChange(opt.value);
          }}
        >
          {options.map((o) => (
            <option key={String(o.value)} value={String(o.value)} disabled={o.disabled}>
              {o.label}
            </option>
          ))}
        </select>
      )}
    </Field>
  );
}

export function Toggle({
  label,
  checked,
  onChange,
  hint,
}: {
  label: string;
  checked: boolean;
  onChange: (v: boolean) => void;
  hint?: ReactNode;
}) {
  return (
    <div className="field">
      <label className="switch">
        <input type="checkbox" role="switch" checked={checked} onChange={(e) => onChange(e.target.checked)} />
        {label}
      </label>
      {hint && <span className="hint">{hint}</span>}
    </div>
  );
}
