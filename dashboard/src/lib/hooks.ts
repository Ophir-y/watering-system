import { useCallback, useEffect, useLayoutEffect, useRef, useState } from 'react';

export interface AsyncState<T> {
  data: T | null;
  error: unknown;
  loading: boolean;
  reload: () => void;
}

// Loads data when the key changes; keeps the previous data while reloading so
// charts and lists never flash empty.
export function useAsync<T>(fn: () => Promise<T>, key: string): AsyncState<T> {
  const [result, setResult] = useState<{ key: string; data: T | null; error: unknown } | null>(null);
  const [nonce, setNonce] = useState(0);
  const fnRef = useRef(fn);
  useLayoutEffect(() => {
    fnRef.current = fn;
  });
  const fullKey = `${key}#${nonce}`;

  useEffect(() => {
    let alive = true;
    fnRef.current().then(
      (data) => alive && setResult({ key: fullKey, data, error: null }),
      (error) => alive && setResult((prev) => ({ key: fullKey, data: prev?.data ?? null, error })),
    );
    return () => {
      alive = false;
    };
  }, [fullKey]);

  const reload = useCallback(() => setNonce((n) => n + 1), []);
  return {
    data: result?.data ?? null,
    error: result?.error ?? null,
    loading: result?.key !== fullKey,
    reload,
  };
}

// Unix seconds, refreshed periodically, for "3 min ago" style labels.
export function useNow(intervalMs = 30000): number {
  const [now, setNow] = useState(() => Math.floor(Date.now() / 1000));
  useEffect(() => {
    const t = setInterval(() => setNow(Math.floor(Date.now() / 1000)), intervalMs);
    return () => clearInterval(t);
  }, [intervalMs]);
  return now;
}
