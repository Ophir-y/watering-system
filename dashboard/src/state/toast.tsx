import { createContext, useCallback, useContext, useMemo, useState, type ReactNode } from 'react';

import { ApiError } from '../api/client';

interface Toast {
  id: number;
  kind: 'info' | 'error';
  text: string;
}

interface ToastApi {
  show: (text: string) => void;
  error: (err: unknown) => void;
}

const Ctx = createContext<ToastApi>({ show: () => {}, error: () => {} });

const FRIENDLY: Record<string, string> = {
  busy: 'Another plant is being watered right now. Try again in a minute.',
  emergency_stop: 'Watering is stopped. Resume it first.',
  leak: 'The leak sensor is wet. Dry it, then resume.',
  reservoir_empty: 'The reservoir is empty. Refill it first.',
  lockout: 'This outlet is locked after a fault. Clear the fault first.',
  plant_disabled: 'This plant is disabled.',
  plant_daily_limit: 'This plant reached its daily limit.',
  total_daily_limit: 'The daily total for all plants is used up.',
  invalid_amount: 'Choose an amount between 5 and the pulse limit.',
};

export function messageFor(err: unknown): string {
  if (err instanceof ApiError) return FRIENDLY[err.code] ?? FRIENDLY[err.message] ?? err.message;
  return err instanceof Error ? err.message : String(err);
}

let nextId = 1;

export function ToastProvider({ children }: { children: ReactNode }) {
  const [toasts, setToasts] = useState<Toast[]>([]);
  const push = useCallback((kind: Toast['kind'], text: string) => {
    const id = nextId++;
    setToasts((t) => [...t.slice(-2), { id, kind, text }]);
    setTimeout(() => setToasts((t) => t.filter((x) => x.id !== id)), kind === 'error' ? 7000 : 4000);
  }, []);
  const api = useMemo<ToastApi>(
    () => ({ show: (text) => push('info', text), error: (err) => push('error', messageFor(err)) }),
    [push],
  );
  return (
    <Ctx.Provider value={api}>
      {children}
      <div className="toasts" role="status" aria-live="polite">
        {toasts.map((t) => (
          <div key={t.id} className="toast" data-kind={t.kind}>
            {t.text}
          </div>
        ))}
      </div>
    </Ctx.Provider>
  );
}

export function useToast() {
  return useContext(Ctx);
}
