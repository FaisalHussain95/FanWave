<script lang="ts">
  import Switch from '../components/Switch.svelte';
  import TabBar from '../components/TabBar.svelte';
  import { api, app } from '../lib/app.svelte';
  import { bytes, percent, uptime } from '../lib/format';
  import Icon from '../lib/Icon.svelte';
  import { href } from '../lib/router.svelte';
  import type { Backup } from '../lib/types';

  let progress = $state<number | null>(null);
  let fileInput = $state<HTMLInputElement>();

  const hub = $derived(app.hub);
  // Follows the hub, but tracks the slider while it's dragged; saved on release.
  let brightness = $derived(app.hub?.led.brightness ?? 0);
  let identifying = $state(false);

  async function identify() {
    identifying = true;
    try {
      await api.identify();
      await new Promise((r) => setTimeout(r, 5000));
    } catch (e) {
      app.notify(`Couldn't reach the hub: ${e instanceof Error ? e.message : e}`);
    } finally {
      identifying = false;
    }
  }

  async function update() {
    progress = 0;
    try {
      await api.installUpdate((f) => (progress = f));
      await app.refreshHub();
      app.notify(`Firmware ${app.hub?.firmware} installed`);
    } catch (e) {
      app.notify(`Update failed: ${e instanceof Error ? e.message : e}`);
    } finally {
      progress = null;
    }
  }

  async function backup() {
    const data = await api.backup();
    const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = `fanwave-backup-${new Date().toISOString().slice(0, 10)}.json`;
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 1000);
  }

  async function restore(e: Event) {
    const file = (e.currentTarget as HTMLInputElement).files?.[0];
    if (!file) return;
    try {
      await api.restore(JSON.parse(await file.text()) as Backup);
      await app.load();
      app.notify('Settings restored');
    } catch (err) {
      app.notify(err instanceof SyntaxError ? 'This file is not valid JSON.' : err instanceof Error ? err.message : String(err));
    } finally {
      if (fileInput) fileInput.value = '';
    }
  }
</script>

<div class="screen has-tabs">
  <div class="screen-body">
    <h1 class="screen-title">Hub</h1>

    {#if hub}
      <div class="panel card-pad">
        <div class="between">
          <div class="stack">
            <span class="strong">{hub.name}</span>
            <span class="sub">{hub.hardware} · {hub.host}</span>
          </div>
          <span class="tag" class:tag-accent={hub.online} class:tag-neutral={!hub.online}>{hub.online ? 'Online' : 'Offline'}</span>
        </div>
        <dl class="stats">
          <div><dt>Uptime</dt><dd>{uptime(hub.uptimeSeconds)}</dd></div>
          <div><dt>Wi‑Fi</dt><dd>{hub.wifiRssi} dBm</dd></div>
          <div><dt>Heap</dt><dd>{bytes(hub.heapFreeBytes)} free</dd></div>
        </dl>
      </div>

      {#if hub.update}
        <div class="update">
          <div class="between">
            <div class="stack">
              <span class="strong">Firmware {hub.update.version} available</span>
              <span class="sub accent">Installed {hub.firmware} · {bytes(hub.update.sizeBytes)}</span>
            </div>
            <span class="accent-icon"><Icon name="arrow-circle-up" size="22px" /></span>
          </div>
          {#if progress === null}
            <button class="btn btn-primary btn-block" style="height:40px" onclick={update}>Update over the air</button>
          {:else}
            <div class="progress" role="progressbar" aria-valuemin="0" aria-valuemax="100" aria-valuenow={Math.round(progress * 100)} aria-label="Firmware update">
              <span style:width={percent(progress)}></span>
            </div>
            <span class="sub accent">Installing… {percent(progress)}. Keep the hub powered.</span>
          {/if}
        </div>
      {:else}
        <p class="sub">Firmware {hub.firmware} · up to date</p>
      {/if}

      <div class="panel">
        <a class="row" href={href.setup}>
          <span class="muted"><Icon name="wifi-high" size="18px" /></span><span class="grow">Wi‑Fi</span>
          <span class="value">{hub.wifiNetwork ?? 'Not connected'}</span><span class="faint"><Icon name="caret-right" /></span>
        </a>
        <div class="row">
          <span class="muted"><Icon name="broadcast" size="18px" /></span><span class="grow">Access point fallback</span>
          <span class="value">{hub.accessPointFallback ? 'On' : 'Off'}</span>
        </div>
        <a class="row" href={href.lab}>
          <span class="muted"><Icon name="wave-sine" size="18px" /></span><span class="grow">RF diagnostics</span>
          <span class="value">{hub.radioOk ? 'CC1101 OK' : 'Radio not found'}</span><span class="faint"><Icon name="caret-right" /></span>
        </a>
        <button class="row" onclick={backup}>
          <span class="muted"><Icon name="download-simple" size="18px" /></span><span class="grow">Backup settings</span>
          <span class="value">JSON</span><span class="faint"><Icon name="caret-right" /></span>
        </button>
        <button class="row" onclick={() => fileInput?.click()}>
          <span class="muted"><Icon name="upload-simple" size="18px" /></span><span class="grow">Restore</span>
          <span class="faint"><Icon name="caret-right" /></span>
        </button>
        <div class="row">
          <span class="muted"><Icon name="shield-check" size="18px" /></span><span class="grow">Local-only mode</span>
          <span class="value">{hub.localOnly ? 'On' : 'Off'}</span>
        </div>
      </div>

      {@const led = hub.led}
      <section class="group" aria-labelledby="led-title">
        <h2 id="led-title" class="group-title">Status LED</h2>
        <div class="panel">
          <div class="row">
            <span class="muted"><Icon name="lightbulb" size="18px" /></span>
            <span class="grow stack">
              <span>Status LED</span>
              <span class="hint">{led.enabled ? 'Shows Wi‑Fi, radio and update status' : 'Off. Errors still show in red'}</span>
            </span>
            <Switch label="Status LED" checked={led.enabled} onchange={(v) => app.setLed({ enabled: v })} />
          </div>
          <div class="row" class:off={!led.enabled}>
            <span class="muted"><Icon name="sun-dim" size="18px" /></span>
            <label for="led-brightness" class="grow">Brightness</label>
            <input
              id="led-brightness"
              class="range"
              type="range"
              min="5"
              max="100"
              step="5"
              disabled={!led.enabled}
              bind:value={brightness}
              onchange={() => app.setLed({ brightness })}
            />
            <span class="value num">{percent(brightness / 100)}</span>
          </div>
          <div class="row" class:off={!led.enabled}>
            <span class="muted"><Icon name="circle" size="18px" /></span>
            <span class="grow" id="led-idle">When idle</span>
            <div class="seg" role="group" aria-labelledby="led-idle">
              {#each [['dim', 'Dim'], ['off', 'Off']] as const as [value, text] (value)}
                <button
                  class="seg-opt"
                  class:on={led.idle === value}
                  aria-pressed={led.idle === value}
                  disabled={!led.enabled}
                  onclick={() => app.setLed({ idle: value })}>{text}</button>
              {/each}
            </div>
          </div>
          <div class="row" class:off={!led.enabled}>
            <span class="muted"><Icon name="broadcast" size="18px" /></span>
            <span class="grow">Flash on send and receive</span>
            <Switch
              label="Flash on send and receive"
              checked={led.radioFlashes}
              disabled={!led.enabled}
              onchange={(v) => app.setLed({ radioFlashes: v })}
            />
          </div>
          <button class="row" disabled={identifying} onclick={identify}>
            <span class="muted"><Icon name="crosshair" size="18px" /></span>
            <span class="grow stack">
              <span>Identify hub</span>
              <span class="hint">Blinks the LED so you can find this board</span>
            </span>
            <span class="value">{identifying ? 'Blinking…' : ''}</span>
          </button>
        </div>
      </section>

      <input class="visually-hidden" type="file" accept="application/json,.json" tabindex="-1" aria-hidden="true" bind:this={fileInput} onchange={restore} />
    {/if}
  </div>
  <TabBar active="Settings" />
</div>

<style>
  .card-pad { gap: 10px; padding: 14px; }
  .between { display: flex; justify-content: space-between; align-items: center; gap: 12px; }
  .stack { display: flex; flex-direction: column; gap: 2px; }
  .strong { font-size: 14px; font-weight: 500; }
  .sub { font-size: 12px; color: var(--color-neutral-400); margin: 0; }
  .sub.accent { color: var(--color-accent-300); }
  .stats { display: grid; grid-template-columns: repeat(3, 1fr); gap: 8px; margin: 0; font-size: 12px; }
  .stats div { display: flex; flex-direction: column; gap: 2px; }
  .stats dt { color: var(--color-neutral-500); }
  .stats dd { margin: 0; }
  .update {
    display: flex; flex-direction: column; gap: 12px; padding: 14px;
    border-radius: var(--radius-md); border: 1px solid var(--color-accent-700); background: var(--color-accent-900);
  }
  .accent-icon { color: var(--color-accent); display: inline-flex; }
  .progress { height: 6px; border-radius: 3px; background: var(--color-accent-800); overflow: hidden; }
  .progress span { display: block; height: 100%; background: var(--color-accent); transition: width 0.15s; }
  .muted { color: var(--color-neutral-400); display: inline-flex; }
  .faint { color: var(--color-neutral-600); display: inline-flex; }
  .grow { flex: 1; }
  .value { font-size: 12px; color: var(--color-neutral-500); }
  .num { min-width: 34px; text-align: right; font-variant-numeric: tabular-nums; }
  .hint { font-size: 12px; color: var(--color-neutral-500); }
  .group { display: flex; flex-direction: column; gap: 8px; }
  .group-title { margin: 0; font-size: 12px; font-weight: 400; color: var(--color-neutral-400); text-transform: uppercase; letter-spacing: 0.06em; }
  .row.off { opacity: 0.5; }
  .range { width: 112px; accent-color: var(--color-accent); }
  .seg-opt { background: transparent; border: 0; color: var(--color-neutral-400); min-height: 32px; min-width: 48px; justify-content: center; }
  .seg-opt + .seg-opt { border-left: 1px solid var(--color-divider); }
  .seg-opt.on { color: var(--color-accent); box-shadow: inset 0 0 0 1px var(--color-accent); }
  .seg-opt:disabled { cursor: not-allowed; }
  button.row:disabled { cursor: default; }
</style>
