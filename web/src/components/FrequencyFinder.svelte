<script lang="ts">
  // "Find frequency" in the RF Lab: runs the hub's frequency scan (docs/api.md)
  // and offers the result as the receiver's new frequency.
  import { api } from '../lib/app.svelte';
  import Icon from '../lib/Icon.svelte';
  import type { ScanStatus } from '../lib/types';
  import ScanSpectrum from './ScanSpectrum.svelte';

  let { onuse, onbusy }: { onuse: (mhz: number) => void; onbusy: (busy: boolean) => void } = $props();

  const ranges = [
    { id: 'low', label: '300–464 MHz (all)', from: 300, to: 464 },
    { id: '433', label: '387–464 MHz (433 band)', from: 387, to: 464 },
    { id: 'near433', label: '430–437 MHz (near 433.92)', from: 430, to: 437 },
    { id: 'us', label: '300–348 MHz (US, 315)', from: 300, to: 348 },
    { id: 'high', label: '779–928 MHz (868/915)', from: 779, to: 928 },
  ];
  let rangeId = $state('low');
  let status = $state<ScanStatus | null>(null);
  let error = $state<string | null>(null);
  let abort: AbortController | null = null;

  const running = $derived(!!status && ['quiet', 'hold', 'refine'].includes(status.state));
  const mhz = (f: number) => `${f.toFixed(3)} MHz`;

  async function start() {
    const r = ranges.find((x) => x.id === rangeId)!;
    error = null;
    abort = new AbortController();
    status = { state: 'quiet', progress: 0 };
    onbusy(true);
    try {
      status = await api.scanFrequency({ fromMHz: r.from, toMHz: r.to }, (s) => (status = s), abort.signal);
    } catch (e) {
      status = null;
      error = e instanceof Error ? e.message : String(e);
    } finally {
      abort = null;
      onbusy(false);
    }
  }

  $effect(() => () => abort?.abort()); // leaving the screen stops a scan
</script>

<div class="panel finder">
  <div class="head">
    <span class="title"><span class="accent"><Icon name="crosshair" /></span>Find frequency</span>
    {#if running}
      <button class="btn btn-ghost small" onclick={() => abort?.abort()}><Icon name="x" />Stop</button>
    {/if}
  </div>

  {#if running && status}
    <div class="steps-line" aria-live="polite">
      {#if status.state === 'quiet'}
        <strong>Don't press anything.</strong> Measuring the background noise first…
      {:else if status.state === 'hold'}
        <strong>Now hold a button on the remote</strong>, close to the hub, until this finishes.
      {:else}
        <strong>Keep holding.</strong> Narrowing down near {mhz(status.candidateMHz ?? 0)}…
      {/if}
    </div>
    <div class="bar" role="progressbar" aria-valuemin="0" aria-valuemax="100" aria-valuenow={Math.round((status.progress ?? 0) * 100)}>
      <span style="width: {(status.progress ?? 0) * 100}%" class:hold={status.state !== 'quiet'}></span>
    </div>
  {:else}
    <p class="hint">
      Measures the background, then asks you to hold a remote button, and reports where the signal rose
      above it. Captures pause meanwhile.
    </p>
    <div class="go">
      <label class="visually-hidden" for="scan-range">Range to scan</label>
      <select id="scan-range" class="range-select" bind:value={rangeId}>
        {#each ranges as r (r.id)}<option value={r.id}>{r.label}</option>{/each}
      </select>
      <button class="btn btn-primary" onclick={start}><Icon name="play" />{status ? 'Scan again' : 'Scan'}</button>
    </div>
  {/if}

  {#if error}
    <p class="problem">{error}</p>
  {/if}

  {#if status?.state === 'done'}
    {#if status.found && status.frequencyMHz}
      <div class="result">
        <div>
          <div class="freq mono">{mhz(status.frequencyMHz)}</div>
          <div class="sub mono">+{status.riseDb} dB over background · {status.peakDbm} dBm</div>
        </div>
        <button class="btn btn-primary" onclick={() => onuse(status!.frequencyMHz!)}>Listen here</button>
      </div>
    {:else}
      <p class="problem">
        Nothing stood out from the background (floor {status.floorDbm} dBm). Hold the remote closer to the hub, make
        sure it transmits while held, or scan another range.
      </p>
    {/if}

    {#if status.candidates && status.candidates.length > (status.found ? 1 : 0)}
      <table class="cands">
        <caption class="label">{status.found ? 'Also rose' : 'Small rises'}</caption>
        <thead class="visually-hidden"><tr><th>Frequency</th><th>Rise</th><th>Level</th><th></th></tr></thead>
        <tbody>
          {#each status.candidates.slice(status.found ? 1 : 0) as c (c.frequencyMHz)}
            <tr>
              <td class="mono">{mhz(c.frequencyMHz)}</td>
              <td class="mono">+{c.riseDb} dB</td>
              <td class="mono muted">{c.peakDbm} dBm</td>
              <td><button class="btn btn-ghost small" onclick={() => onuse(c.frequencyMHz)}>Listen</button></td>
            </tr>
          {/each}
        </tbody>
      </table>
    {/if}

    {#if status.spectrum?.length}
      <ScanSpectrum spectrum={status.spectrum} markMHz={status.found ? (status.frequencyMHz ?? null) : null} />
    {/if}
  {:else if status?.state === 'failed'}
    <p class="problem">The scan failed: {status.error ?? 'unknown error'}.</p>
  {:else if status?.state === 'cancelled'}
    <p class="hint">Scan stopped.</p>
  {/if}
</div>

<style>
  .finder { gap: 10px; padding: 12px; }
  .head { display: flex; align-items: center; justify-content: space-between; min-height: 32px; }
  .title { display: inline-flex; align-items: center; gap: 8px; font-size: 14px; }
  .accent { color: var(--color-accent); display: inline-flex; }
  .small { font-size: 13px; min-height: 32px; }
  .hint, .steps-line, .problem { margin: 0; font-size: 12px; line-height: 1.45; color: var(--color-neutral-400); }
  .steps-line strong { color: var(--color-text); font-weight: 600; }
  .problem { color: var(--color-neutral-300); }
  .go { display: flex; gap: 8px; }
  .range-select {
    flex: 1; min-width: 0; min-height: 40px; padding: 0 10px; font-size: 13px; color: var(--color-text);
    background: transparent; border: 1px solid var(--color-divider); border-radius: calc(var(--radius-md) * 0.75);
  }
  .bar { height: 6px; border-radius: 3px; background: var(--color-neutral-900); overflow: hidden; }
  .bar span { display: block; height: 100%; border-radius: 3px; background: var(--color-neutral-600); transition: width 0.4s linear; }
  .bar span.hold { background: var(--color-accent); }
  .result { display: flex; align-items: center; justify-content: space-between; gap: 12px; }
  .result .btn { white-space: nowrap; flex-shrink: 0; }
  .freq { font-size: 22px; color: var(--color-accent-300); }
  .sub { font-size: 11px; color: var(--color-neutral-400); }
  .cands { width: 100%; border-collapse: collapse; font-size: 12px; }
  .cands caption { text-align: left; padding-bottom: 4px; }
  .cands td { padding: 2px 0; }
  .cands td:last-child { text-align: right; }
  .muted { color: var(--color-neutral-500); }
</style>
