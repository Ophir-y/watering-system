import { cleanup, render, screen } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it } from 'vitest';

import { App } from './App';
import { setToken } from './api/client';
import { setTransport } from './api/transport';
import { MockDevice } from './mock/device';

describe('App against the simulated device', () => {
  let device: MockDevice;

  beforeEach(() => {
    window.location.hash = '#/';
  });

  afterEach(() => {
    cleanup();
    device?.dispose();
    setToken(null);
  });

  it('asks for a password when signed out', async () => {
    device = new MockDevice({ autoTick: false });
    setTransport(device);
    setToken(null);
    render(<App />);
    expect(await screen.findByRole('heading', { name: /sign in/i })).toBeTruthy();
  });

  it('shows every plant on the overview', async () => {
    device = new MockDevice({ autoTick: false });
    setTransport(device);
    setToken('a3f1c0de5eed0000a3f1c0de5eed0000');
    render(<App />);
    for (const p of device.plants)
      expect(await screen.findByText(p.name, { selector: '.tag-name' })).toBeTruthy();
    expect(screen.getByRole('heading', { level: 1 }).textContent).toMatch(/plant|range|attention/i);
    expect(screen.getAllByRole('img', { name: /moisture/i }).length).toBe(device.plants.length);
  });

  it('starts the setup wizard on a new device', async () => {
    device = new MockDevice({ autoTick: false, setupRequired: true });
    setTransport(device);
    render(<App />);
    expect(await screen.findByRole('heading', { name: /set up plantcare/i })).toBeTruthy();
  });
});
