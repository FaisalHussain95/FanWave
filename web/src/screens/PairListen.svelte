<script lang="ts">
  import type { PairSignal } from '../lib/api';
  import { api, app } from '../lib/app.svelte';
  import { percent } from '../lib/format';
  import Icon from '../lib/Icon.svelte';
  import { go, href } from '../lib/router.svelte';

  let signal = $state<PairSignal | null>(null);

  $effect(() => api.listenForRemote((s) => (signal = s)));

  function next() {
    const m = signal?.match ?? null;
    app.startPairing({ protocol: m?.protocol ?? null, protocolName: m?.name ?? null, address: signal?.address ?? null, match: m });
    go(href.learn);
  }
</script>

<div class="screen">
  <div class="screen-body tight">
    <div class="nav">
      <a class="btn btn-icon btn-ghost icon-btn" href={href.dashboard} aria-label="Close"><Icon name="x" /></a>
      <div class="steps" aria-hidden="true"><span class="on"></span><span class="on"></span><span></span><span></span></div>
    </div>

    <div class="intro">
      <span class="eyebrow">Step 2 of 4</span>
      <h1 class="title">Press any button on the fan's remote</h1>
      <p class="lede">Hold it within a metre of the hub. We'll try to recognise the protocol.</p>
    </div>

    <div class="radar" class:heard={!!signal}>
      <span class="ring r1"></span>
      <span class="ring r2"></span>
      <span class="core"><Icon name="broadcast" size="44px" /></span>
    </div>

    <div class="panel result" aria-live="polite">
      {#if !signal}
        <div class="line"><span class="muted">Listening on 433.92 MHz…</span></div>
      {:else}
        <div class="line">
          <span class="ok"><span class="accent"><Icon name="check-circle-fill" /></span>Signal received ×{signal.count}</span>
          <span class="muted">{signal.rssi} dBm</span>
        </div>
        <div class="line">
          <span class="muted">Likely protocol</span>
          <span>{signal.match ? `${signal.match.name} · ${percent(signal.match.confidence)}` : 'Analysing…'}</span>
        </div>
        <div class="line">
          <span class="muted">Remote address</span>
          <span class="mono">{signal.address ?? '—'}</span>
        </div>
      {/if}
    </div>

    <div class="actions">
      <a class="btn btn-ghost" href={href.lab}>Open in RF Lab</a>
      <button class="btn btn-primary" disabled={!signal} onclick={next}>Continue</button>
    </div>
  </div>
</div>

<style>
  .nav { display: flex; align-items: center; gap: 8px; margin-left: -8px; }
  .intro { display: flex; flex-direction: column; gap: 8px; }
  .title { margin: 0; font-size: 24px; font-weight: 500; line-height: 1.2; }

  .radar { flex: 1; min-height: 260px; position: relative; display: flex; align-items: center; justify-content: center; }
  .ring { position: absolute; border-radius: 50%; }
  .r1 { width: 240px; height: 240px; border: 1px solid var(--color-accent-800); }
  .r2 { width: 176px; height: 176px; border: 1px solid var(--color-accent-700); }
  .radar:not(.heard) .ring { animation: pulse 2.4s ease-out infinite; }
  .radar:not(.heard) .r2 { animation-delay: 0.6s; }
  .core {
    width: 112px; height: 112px; border-radius: 50%; border: 1px solid var(--color-accent);
    display: flex; align-items: center; justify-content: center;
    box-shadow: 0 0 48px var(--color-accent-800); color: var(--color-accent);
  }
  @keyframes pulse { 0% { transform: scale(0.85); opacity: 1; } 100% { transform: scale(1.1); opacity: 0.2; } }

  .result { gap: 10px; padding: 14px; }
  .line { display: flex; justify-content: space-between; gap: 12px; font-size: 13px; }
  .ok { display: flex; align-items: center; gap: 8px; }
  .muted { color: var(--color-neutral-400); }
  .accent { color: var(--color-accent); display: inline-flex; }
</style>
