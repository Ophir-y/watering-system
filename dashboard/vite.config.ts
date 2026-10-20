/// <reference types="vitest/config" />
import react from '@vitejs/plugin-react';
import { defineConfig, loadEnv } from 'vite';

// `npm run dev`      proxies /api to the real device (VITE_DEVICE_URL, default http://plantcare.local)
// `npm run dev:mock` runs against an in-browser simulated device; no hardware needed
export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, process.cwd(), '');
  const device = env.VITE_DEVICE_URL || 'http://plantcare.local';
  return {
    plugins: [react()],
    define: {
      __MOCK__: JSON.stringify(mode === 'mock' || mode === 'test'),
    },
    server: {
      proxy: mode === 'mock' ? undefined : { '/api': { target: device, changeOrigin: true } },
    },
    build: {
      target: 'es2020',
      assetsInlineLimit: 0,
      cssCodeSplit: false,
      modulePreload: { polyfill: false },
      reportCompressedSize: false,
    },
    test: {
      environment: 'jsdom',
      include: ['src/**/*.test.{ts,tsx}'],
      setupFiles: ['src/test/setup.ts'],
    },
  };
});
