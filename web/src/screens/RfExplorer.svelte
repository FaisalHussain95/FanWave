<script lang="ts">
  import FrequencyFinder from '../components/FrequencyFinder.svelte';
  import TabBar from '../components/TabBar.svelte';
  import { api, app } from '../lib/app.svelte';
  import Icon from '../lib/Icon.svelte';
  import { bitsToHex, nibbles, wavePath } from '../lib/rf';
  import { href } from '../lib/router.svelte';
  import type { Capture, FilteredReport, Modulation } from '../lib/types';

  let captures = $state<Capture[]>([]);
  let selectedId = $state<number | null>(null);
  let listening = $state(true);
  let modulation = $state<Modulation>('OOK');
  let frequency = $state(433.92);
  let minRssi = $state(-80);
  let scanning = $state(false);
  let edgesPerSecond = $state<number | null>(null);
  let ready = $state(false);

  // Common remote frequencies: US ceiling fans sit around 303–305 MHz.
  const presets = [303.875, 304.25, 315, 390, 418, 433.92];

  const selected = $derived(captures.find((c) => c.id === selectedId) ?? captures[0]);

  api.captures().then((list) => (captures = list));
  api.receiver().then((r) => {
    listening = r.listening;
    modulation = r.modulation;
    frequency = r.frequencyMHz;
    minRssi = r.minRssiDbm;
    scanning = !!r.scanning;
    ready = true;
  });

  $effect(() =>
    api.onCapture((c) => {
      captures = [c, ...captures].slice(0, 20);
    }),
  );

  // Push changes once the receiver's current state has been read.
  $effect(() => {
    const opts = { listening, modulation, frequencyMHz: frequency };
    if (ready) api.setReceiver(opts).catch((e) => app.notify(e instanceof Error ? e.message : String(e)));
  });

  function saveThreshold() {
    api.setReceiver({ minRssiDbm: minRssi }).catch((e) => app.notify(e instanceof Error ? e.message : String(e)));
  }

  // What the noise filter dropped over the last FILTER_WINDOW_MS, from the hub's periodic summaries.
  const FILTER_WINDOW_MS = 10_000;
  let filtered = $state<(FilteredReport & { at: number })[]>([]);
  let clock = $state(Date.now());
  $effect(() => api.onFiltered((r) => (filtered = [...filtered, { ...r, at: Date.now() }].slice(-20))));
  $effect(() => {
    const t = setInterval(() => (clock = Date.now()), 1000);
    return () => clearInterval(t);
  });
  // The receive pin's activity, refreshed while this screen is open.
  $effect(() => {
    const poll = () =>
      api.receiver().then((r) => (edgesPerSecond = r.edgesPerSecond ?? null), () => {});
    poll();
    const t = setInterval(poll, 2000);
    return () => clearInterval(t);
  });
  const pinDead = $derived(listening && !scanning && edgesPerSecond === 0);

  const recent = $derived(filtered.filter((r) => clock - r.at < FILTER_WINDOW_MS));
  const dropped = $derived(recent.reduce((n, r) => n + r.count, 0));
  const loudestDropped = $derived(recent.length ? Math.max(...recent.map((r) => r.strongestDbm)) : null);

  const frequencyOptions = $derived(presets.includes(frequency) ? presets : [...presets, frequency].sort((a, b) => a - b));

  function captureText(c: Capture) {
    return c.bits ? bitsToHex(c.bits) : `raw · ${c.pulseCount} pulses`;
  }
</script>

<div class="screen has-tabs">
  <div class="screen-body lab">
    <div class="head">
      <h1 class="screen-title">RF Explorer</h1>
      <button class="btn btn-secondary" style="height:34px" aria-pressed={listening} disabled={scanning} onclick={() => (listening = !listening)}>
        <span class="accent"><Icon name={listening ? 'record-fill' : 'play'} /></span>{listening ? 'Listening' : 'Paused'}
      </button>
    </div>

    <div class="tune">
      <div class="seg" role="group" aria-label="Modulation">
        {#each ['OOK', 'FSK'] as const as m (m)}
          <button class="seg-opt" class:on={modulation === m} aria-pressed={modulation === m} disabled={scanning} onclick={() => (modulation = m)}>{m}</button>
        {/each}
      </div>
      <label class="visually-hidden" for="rx-freq">Frequency</label>
      <select id="rx-freq" class="freq mono" bind:value={frequency} disabled={scanning}>
        {#each frequencyOptions as f (f)}<option value={f}>{f} MHz</option>{/each}
      </select>
    </div>

    <FrequencyFinder onuse={(f) => (frequency = f)} onbusy={(b) => (scanning = b)} />

    <div class="panel noise">
      <div class="noise-row">
        <label for="noise-threshold">Noise threshold</label>
        <input
          id="noise-threshold"
          class="range"
          type="range"
          min="-110"
          max="-30"
          step="1"
          bind:value={minRssi}
          onchange={saveThreshold}
        />
        <span class="mono value">{minRssi} dBm</span>
      </div>
      <p class="hint">
        {#if dropped}
          Dropped as noise in the last 10 s: {dropped} burst{dropped === 1 ? '' : 's'}, loudest {loudestDropped} dBm.
          {#if loudestDropped !== null && loudestDropped >= minRssi - 6}
            Some are close to the threshold; lower it if a weak remote isn't showing up.
          {/if}
        {:else}
          Undecoded bursts weaker than this are dropped as noise. Nothing dropped in the last 10 s.
        {/if}
      </p>
      {#if edgesPerSecond !== null}
        <p class="hint mono" class:warn={pinDead}>
          Receive pin (GDO2): {edgesPerSecond.toLocaleString()} edges/s
        </p>
      {/if}
      {#if pinDead}
        <p class="hint warn">
          Nothing is reaching the receive pin. A listening radio always produces some noise here, so check the wire
          from the CC1101's GDO2 to the ESP32 (GPIO5 by default).
        </p>
      {/if}
    </div>

    {#if selected}
      <div class="panel wave">
        <div class="wave-head">
          <span>Capture #{selected.id} · {selected.repeats} repeat{selected.repeats === 1 ? '' : 's'}</span>
          <span class="mono">RSSI {selected.rssi} dBm</span>
        </div>
        <svg viewBox="0 0 300 70" preserveAspectRatio="none" role="img" aria-label={`Pulse train of capture ${selected.id}`}>
          <line x1="0" y1="60" x2="300" y2="60" stroke="var(--color-neutral-800)" stroke-width="1"></line>
          <path d={wavePath(selected.bits)} fill="none" stroke="var(--color-accent)" stroke-width="1.4" vector-effect="non-scaling-stroke"></path>
        </svg>
        <div class="axis mono">
          <span>0 ms</span><span>{(selected.durationMs / 2).toFixed(1)} ms</span><span>{selected.durationMs} ms</span>
        </div>
      </div>

      <div class="stats">
        <div class="panel stat"><span class="k">Short</span><span class="v mono">{selected.shortUs ? `${selected.shortUs} µs` : '—'}</span></div>
        <div class="panel stat"><span class="k">Long</span><span class="v mono">{selected.longUs ? `${selected.longUs} µs` : '—'}</span></div>
        <div class="panel stat"><span class="k">Sync</span><span class="v mono">{selected.syncMs ? `${selected.syncMs} ms` : '—'}</span></div>
      </div>

      <div class="bits-wrap">
        <div class="bits-head">
          <span class="label">{selected.bits ? `Bitstream · ${selected.bits.length} bits` : 'No fixed-width frame decoded'}</span>
          <a class="btn btn-ghost" href={href.detective(selected.id)}><Icon name="magnifying-glass" />Identify</a>
        </div>
        {#if selected.bits}
          <div class="bits mono">
            <span class="addr">{nibbles(selected.bits.slice(0, 20))}</span> <span>{nibbles(selected.bits.slice(20))}</span>
          </div>
          <div class="hex mono">hex {bitsToHex(selected.bits)} · matches repeats {selected.repeats}/{selected.repeats}</div>
        {:else}
          <div class="bits mono muted">{selected.pulseCount} raw pulses · {selected.modulation}</div>
        {/if}
      </div>
    {/if}

    <div class="recent">
      <span class="label">Recent captures</span>
      <ul>
        {#each captures as c (c.id)}
          <li>
            <button class="cap" class:on={c.id === selected?.id} aria-pressed={c.id === selected?.id} onclick={() => (selectedId = c.id)}>
              <span class="id mono">#{c.id}</span>
              <span class="hexcol mono">{captureText(c)}</span>
              <span class="lbl">{c.label}</span>
            </button>
          </li>
        {/each}
      </ul>
    </div>
  </div>
  <TabBar active="RF Lab" />
</div>

<style>
  .lab { --gutter: 16px; gap: 14px; padding-top: 20px; }
  .head { display: flex; align-items: center; justify-content: space-between; }
  .head .screen-title { font-size: 22px; }
  .accent { color: var(--color-accent); display: inline-flex; }
  .tune { display: flex; gap: 8px; align-items: center; }
  .freq {
    appearance: none; min-height: 36px; padding: 0 10px; font-size: 11px; cursor: pointer;
    color: var(--color-accent); background: transparent;
    border: 1px solid var(--color-accent); border-radius: calc(var(--radius-md) * 0.75);
  }
  .seg { flex: 1; }
  .noise { gap: 6px; padding: 12px; }
  .noise-row { display: flex; align-items: center; gap: 10px; font-size: 14px; }
  .noise-row label { white-space: nowrap; }
  .range { flex: 1; min-width: 80px; accent-color: var(--color-accent); }
  .value { font-size: 12px; width: 58px; text-align: right; color: var(--color-neutral-300); }
  .hint { margin: 0; font-size: 12px; line-height: 1.45; color: var(--color-neutral-400); }
  .hint.warn { color: var(--color-neutral-200); }
  .seg-opt { flex: 1; justify-content: center; background: transparent; border: 0; color: var(--color-neutral-400); min-height: 36px; }
  .seg-opt + .seg-opt { border-left: 1px solid var(--color-divider); }
  .seg-opt.on { background: var(--color-accent-900); color: var(--color-accent-200); box-shadow: inset 0 0 0 1px var(--color-accent); }

  .wave { gap: 8px; padding: 12px; }
  .wave-head { display: flex; justify-content: space-between; font-size: 11px; color: var(--color-neutral-400); }
  .wave svg { width: 100%; height: 70px; display: block; }
  .axis { display: flex; justify-content: space-between; font-size: 10px; color: var(--color-neutral-600); }

  .stats { display: grid; grid-template-columns: repeat(3, 1fr); gap: 8px; }
  .stat { gap: 2px; padding: 10px; }
  .k { font-size: 10px; color: var(--color-neutral-500); text-transform: uppercase; letter-spacing: 0.06em; }
  .v { font-size: 14px; }

  .bits-wrap { display: flex; flex-direction: column; gap: 6px; }
  .bits-head { display: flex; justify-content: space-between; align-items: center; }
  .bits-head .btn { font-size: 13px; min-height: 32px; }
  .bits {
    font-size: 13px; line-height: 1.6; letter-spacing: 0.04em; padding: 10px 12px;
    border-radius: var(--radius-md); background: var(--color-neutral-900); word-break: break-all;
  }
  .addr { color: var(--color-accent-300); }
  .muted { color: var(--color-neutral-400); }
  .hex { font-size: 12px; color: var(--color-neutral-500); }

  .recent { display: flex; flex-direction: column; gap: 6px; }
  .recent ul { list-style: none; margin: 0; padding: 0; display: flex; flex-direction: column; gap: 6px; }
  .cap {
    width: 100%; display: flex; align-items: center; gap: 10px; font-size: 12px; padding: 8px 10px; min-height: 40px;
    border: 0; border-radius: var(--radius-md); background: var(--color-surface); cursor: pointer; text-align: left;
  }
  .cap.on { background: var(--color-accent-900); }
  .id { color: var(--color-neutral-500); width: 28px; }
  .hexcol { flex: 1; }
  .lbl { color: var(--color-neutral-400); }
</style>
