import { setToken } from '../api/client';
import { setTransport } from '../api/transport';
import { MockDevice } from './device';

// `?setup` in the URL starts the simulated device unconfigured, to try the wizard.
export function installMock(): MockDevice {
  const setup = new URLSearchParams(window.location.search).has('setup');
  const device = new MockDevice({ setupRequired: setup });
  setTransport(device);
  if (!setup) setToken('a3f1c0de5eed0000a3f1c0de5eed0000');
  console.info('[PlantCare] Simulated device. Password: plantcare');
  return device;
}
