import { useState, type ReactNode } from 'react';
import { Link, useLocation } from 'wouter';

import { api } from '../api/client';
import { useDevice, useStatus } from '../state/device';
import { useToast } from '../state/toast';
import { Dialog } from './Dialog';
import { Icon, type IconName } from './Icon';

const NAV: { href: string; label: string; short: string; icon: IconName }[] = [
  { href: '/', label: 'Plants', short: 'Plants', icon: 'sprout' },
  { href: '/activity', label: 'Activity', short: 'Activity', icon: 'list' },
  { href: '/alerts', label: 'Alerts', short: 'Alerts', icon: 'bell' },
  { href: '/climate', label: 'Room', short: 'Room', icon: 'thermometer' },
  { href: '/settings', label: 'Settings', short: 'Settings', icon: 'sliders' },
];

function isActive(location: string, href: string) {
  return href === '/' ? location === '/' || location.startsWith('/plants') : location.startsWith(href);
}

export function StopControl({ compact }: { compact?: boolean }) {
  const status = useStatus();
  const { refresh } = useDevice();
  const toast = useToast();
  const [confirm, setConfirm] = useState(false);
  const stopped = status.automation.emergencyStop;

  const act = async (fn: () => Promise<unknown>, done: string) => {
    try {
      await fn();
      toast.show(done);
      await refresh();
    } catch (e) {
      toast.error(e);
    }
  };

  if (stopped) {
    return (
      <>
        <button className={`btn btn-water${compact ? ' btn-small' : ''}`} onClick={() => setConfirm(true)}>
          <Icon name="drop" size={16} /> Resume watering
        </button>
        <Dialog open={confirm} onClose={() => setConfirm(false)} labelledBy="resume-title">
          <h2 id="resume-title">Resume watering?</h2>
          <p className="ink-2">Every plant node picks up where it left off at its next check-in.</p>
          <div className="row">
            <button className="btn btn-ghost" onClick={() => setConfirm(false)}>
              Keep stopped
            </button>
            <button
              className="btn btn-water"
              onClick={() => {
                setConfirm(false);
                act(api.resume, 'Watering resumed');
              }}
            >
              Resume watering
            </button>
          </div>
        </Dialog>
      </>
    );
  }
  return (
    <button
      className={`btn btn-stop${compact ? ' btn-small' : ''}`}
      onClick={() =>
        act(() => api.stopAll('stopped from the dashboard'), 'Stopping: each node stops at its next check-in')
      }
      title="Closes every pump and valve immediately and pauses automatic watering"
    >
      <Icon name="stop" size={16} /> Stop all
    </button>
  );
}

export function Shell({ children }: { children: ReactNode }) {
  const [location] = useLocation();
  const status = useStatus();
  const { live } = useDevice();
  const urgent = status.alerts.critical + status.alerts.warning;
  const net = status.network;

  return (
    <div className="shell">
      <aside className="rail">
        <Link href="/" className="brand">
          <img src="/favicon.svg" width="28" height="28" alt="" />
          <strong>{status.device.name}</strong>
        </Link>
        <nav aria-label="Main" className="stack" style={{ gap: 2 }}>
          {NAV.map((n) => (
            <Link
              key={n.href}
              href={n.href}
              className="nav-link"
              aria-current={isActive(location, n.href) ? 'page' : undefined}
            >
              <Icon name={n.icon} />
              {n.label}
              {n.href === '/alerts' && urgent > 0 && <span className="count">{urgent}</span>}
            </Link>
          ))}
        </nav>
        <div className="rail-footer">
          <StopControl />
          <span>
            <Icon name="wifi" size={14} />{' '}
            {net.mode === 'ap'
              ? `Hotspot ${net.apSsid}`
              : net.ssid
                ? `${net.ssid}${net.internet ? '' : ' (no internet)'}`
                : 'Not connected'}
          </span>
          <span>
            {live ? 'Live' : 'Updating every few seconds'} · v{status.device.version}
          </span>
        </div>
      </aside>

      <header className="topbar">
        <Link href="/" className="brand" style={{ padding: 0 }}>
          <img src="/favicon.svg" width="24" height="24" alt="" />
          <strong>{status.device.name}</strong>
        </Link>
        <StopControl compact />
      </header>

      <main className="main" id="main">
        {children}
      </main>

      <nav className="tabbar" aria-label="Main">
        {NAV.map((n) => (
          <Link key={n.href} href={n.href} aria-current={isActive(location, n.href) ? 'page' : undefined}>
            <Icon name={n.icon} size={20} />
            {n.short}
            {n.href === '/alerts' && urgent > 0 && <span className="dot" />}
          </Link>
        ))}
      </nav>
    </div>
  );
}
