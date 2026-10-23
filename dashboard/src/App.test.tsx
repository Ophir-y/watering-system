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

  it('shows a waiting node and the bottles on the overview', async () => {
    device = new MockDevice({ autoTick: false });
    setTransport(device);
    setToken('a3f1c0de5eed0000a3f1c0de5eed0000');
    render(<App />);
    expect(await screen.findByText(/new plant node found/i)).toBeTruthy();
    expect(screen.getByRole('link', { name: /add as a plant/i })).toBeTruthy();
    expect(screen.getByRole('heading', { name: 'Bottles' })).toBeTruthy();
    expect(screen.getAllByRole('img', { name: /bottle/i }).length).toBeGreaterThanOrEqual(
      device.plants.length,
    );
  });

  it('opens a plant with its bottle, pump and node', async () => {
    device = new MockDevice({ autoTick: false });
    setTransport(device);
    setToken('a3f1c0de5eed0000a3f1c0de5eed0000');
    window.location.hash = `#/plants/${device.plants[0].id}`;
    render(<App />);
    expect(await screen.findByRole('heading', { name: 'Bottle and pump' })).toBeTruthy();
    expect(screen.getByRole('heading', { name: 'Sensor and node' })).toBeTruthy();
    expect(screen.getByRole('button', { name: /refilled/i })).toBeTruthy();
    expect(screen.getByText(device.plants[0].node!)).toBeTruthy();
  });

  it('adds a waiting node from its own page', async () => {
    device = new MockDevice({ autoTick: false });
    setTransport(device);
    setToken('a3f1c0de5eed0000a3f1c0de5eed0000');
    const node = device.waiting[0].id;
    window.location.hash = `#/plants/new/${node}`;
    render(<App />);
    expect(await screen.findByRole('heading', { name: /add a plant/i })).toBeTruthy();
    const picker = screen.getByLabelText(/plant node/i) as HTMLSelectElement;
    expect(picker.value).toBe(node);
    expect(screen.getByLabelText(/bottle holds/i)).toBeTruthy();
  });

  it('renders the room and settings pages', async () => {
    device = new MockDevice({ autoTick: false });
    setTransport(device);
    setToken('a3f1c0de5eed0000a3f1c0de5eed0000');
    window.location.hash = '#/climate';
    const { unmount } = render(<App />);
    expect(await screen.findByRole('heading', { name: 'Room', level: 1 })).toBeTruthy();
    unmount();
    window.location.hash = '#/settings';
    render(<App />);
    expect(await screen.findByLabelText(/nodes check in every/i)).toBeTruthy();
  });

  it('starts the setup wizard on a new device', async () => {
    device = new MockDevice({ autoTick: false, setupRequired: true });
    setTransport(device);
    render(<App />);
    expect(await screen.findByRole('heading', { name: /set up plantcare/i })).toBeTruthy();
  });
});
