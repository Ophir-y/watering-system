import './styles/fonts.css';
import './styles/tokens.css';
import './styles/app.css';

import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';

import { App } from './App';
import { applyTheme, getThemeChoice } from './theme';

applyTheme(getThemeChoice());

async function start() {
  if (__MOCK__) {
    // Simulated device for `npm run dev:mock`; stripped from production builds.
    const { installMock } = await import('./mock');
    installMock();
  }
  createRoot(document.getElementById('root')!).render(
    <StrictMode>
      <App />
    </StrictMode>,
  );
}

start();
