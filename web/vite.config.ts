import { defineConfig, loadEnv } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';

// The build is served from the hub's flash, so keep it small and relative.
//
// In development the app runs against an in-browser mock hub. To use a real
// one, point the dev server at it: VITE_HUB_URL=http://192.168.1.23 npm run dev
export default defineConfig(({ mode }) => {
  const hub = loadEnv(mode, '.', 'VITE_').VITE_HUB_URL;
  const origin = hub ? new URL(hub).origin : undefined;
  return {
    base: './',
    plugins: [svelte()],
    build: {
      target: 'es2020',
      assetsInlineLimit: 0,
      reportCompressedSize: true,
    },
    server: origin
      ? {
          proxy: {
            '/api': {
              target: origin,
              changeOrigin: true,
              ws: true,
              // The hub refuses requests whose Origin isn't its own address.
              headers: { origin },
            },
          },
        }
      : undefined,
  };
});
