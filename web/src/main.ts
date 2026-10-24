import './styles/fonts.css';
import './styles/nocturne.css';
import './styles/app.css';
import { mount } from 'svelte';
import App from './App.svelte';

mount(App, { target: document.getElementById('app')! });

if ('serviceWorker' in navigator && import.meta.env.PROD) {
  navigator.serviceWorker.register('./sw.js').catch(() => {
    /* offline shell is a nicety; the app works without it */
  });
}
