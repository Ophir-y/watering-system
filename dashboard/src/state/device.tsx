// App-wide device state: who is logged in, live status (Server-Sent Events with a
// polling fallback), the plant profiles, and recent events.
import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useMemo,
  useRef,
  useState,
  type ReactNode,
} from 'react';

import { api, getToken, onUnauthorized, setToken } from '../api/client';
import { transport } from '../api/transport';
import type { DeviceEvent, Info, PlantProfile, Settings, Status } from '../api/types';

type Phase = 'loading' | 'offline' | 'setup' | 'login' | 'ready';

interface DeviceState {
  phase: Phase;
  info: Info | null;
  status: Status | null;
  plants: PlantProfile[];
  settings: Settings | null;
  live: boolean; // streaming updates are flowing
  recent: DeviceEvent[];
  signIn: (token: string) => void;
  signOut: () => void;
  refresh: () => Promise<void>;
  reloadPlants: () => Promise<void>;
  reloadSettings: () => Promise<void>;
  retry: () => void;
}

const Ctx = createContext<DeviceState | null>(null);

export function DeviceProvider({ children }: { children: ReactNode }) {
  const [phase, setPhase] = useState<Phase>('loading');
  const [info, setInfo] = useState<Info | null>(null);
  const [status, setStatus] = useState<Status | null>(null);
  const [plants, setPlants] = useState<PlantProfile[]>([]);
  const [settings, setSettings] = useState<Settings | null>(null);
  const [live, setLive] = useState(false);
  const [recent, setRecent] = useState<DeviceEvent[]>([]);
  const [attempt, setAttempt] = useState(0);
  const lastStatusAt = useRef(0);

  const refresh = useCallback(async () => {
    const s = await api.status();
    lastStatusAt.current = Date.now();
    setStatus(s);
  }, []);

  const reloadPlants = useCallback(async () => {
    setPlants(await api.plants());
  }, []);

  const reloadSettings = useCallback(async () => {
    setSettings(await api.settings());
  }, []);

  // Boot: what state is the device in, and are we logged in?
  useEffect(() => {
    let alive = true;
    api
      .info()
      .then(async (i) => {
        if (!alive) return;
        setInfo(i);
        if (i.setupRequired) return setPhase('setup');
        if (!getToken()) return setPhase('login');
        try {
          await Promise.all([refresh(), reloadPlants(), reloadSettings()]);
          if (alive) setPhase('ready');
        } catch {
          if (alive) setPhase(getToken() ? 'offline' : 'login');
        }
      })
      .catch(() => alive && setPhase('offline'));
    return () => {
      alive = false;
    };
  }, [attempt, refresh, reloadPlants, reloadSettings]);

  useEffect(
    () =>
      onUnauthorized(() => {
        setToken(null);
        setPhase('login');
      }),
    [],
  );

  // Live updates while signed in; poll if the stream is quiet for too long.
  useEffect(() => {
    if (phase !== 'ready') return;
    const token = getToken();
    if (!token) return;
    let closed = false;
    const stop = transport().stream(
      token,
      (event, data) => {
        if (closed) return;
        if (event === 'status') {
          lastStatusAt.current = Date.now();
          setStatus(data as Status);
          setLive(true);
        } else if (event === 'event') {
          const e = data as DeviceEvent;
          setRecent((r) => [e, ...r].slice(0, 30));
        }
      },
      () => setLive(false),
    );
    const poll = setInterval(() => {
      if (Date.now() - lastStatusAt.current > 8000) {
        setLive(false);
        refresh().catch(() => {});
      }
    }, 4000);
    return () => {
      closed = true;
      stop();
      clearInterval(poll);
    };
  }, [phase, refresh]);

  const signIn = useCallback((token: string) => {
    setToken(token);
    setPhase('loading');
    setAttempt((a) => a + 1);
  }, []);

  const signOut = useCallback(() => {
    api.logout().catch(() => {});
    setToken(null);
    setPhase('login');
  }, []);

  const value = useMemo<DeviceState>(
    () => ({
      phase,
      info,
      status,
      plants,
      settings,
      live,
      recent,
      signIn,
      signOut,
      refresh,
      reloadPlants,
      reloadSettings,
      retry: () => setAttempt((a) => a + 1),
    }),
    [
      phase,
      info,
      status,
      plants,
      settings,
      live,
      recent,
      signIn,
      signOut,
      refresh,
      reloadPlants,
      reloadSettings,
    ],
  );
  return <Ctx.Provider value={value}>{children}</Ctx.Provider>;
}

export function useDevice(): DeviceState {
  const v = useContext(Ctx);
  if (!v) throw new Error('useDevice outside DeviceProvider');
  return v;
}

// Status is guaranteed once the app shell renders (phase === 'ready').
export function useStatus(): Status {
  const { status } = useDevice();
  if (!status) throw new Error('status not loaded');
  return status;
}
