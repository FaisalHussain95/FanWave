<script lang="ts">
  import TabBar from '../components/TabBar.svelte';
  import { api, app } from '../lib/app.svelte';
  import { bytes, percent, uptime } from '../lib/format';
  import Icon from '../lib/Icon.svelte';
  import { href } from '../lib/router.svelte';
  import type { Backup } from '../lib/types';

  let progress = $state<number | null>(null);
  let fileInput = $state<HTMLInputElement>();

  const hub = $derived(app.hub);

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
</style>
