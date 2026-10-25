import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';

// The build is served from the hub's flash, so keep it small and relative.
export default defineConfig({
  base: './',
  plugins: [svelte()],
  build: {
    target: 'es2020',
    assetsInlineLimit: 0,
    reportCompressedSize: true,
  },
});
