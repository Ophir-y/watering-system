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
  busy: "Several commands are already waiting for this plant's node. Try again after its next check-in.",
  no_node: 'This plant has no node yet. Edit the plant to pick one.',
  emergency_stop: 'Watering is stopped. Resume it first.',
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
