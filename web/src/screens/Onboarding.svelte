<script lang="ts">
  import { api, app } from '../lib/app.svelte';
  import Icon from '../lib/Icon.svelte';
  import { go, href } from '../lib/router.svelte';
  import type { WifiNetwork } from '../lib/types';

  let networks = $state<WifiNetwork[] | null>(null);
  let selected = $state<string | null>(null);
  let password = $state('');
  let connecting = $state(false);
  let error = $state<string | null>(null);
  /** Set when the hub joined the network but this phone is still on the hub's own access point. */
  let joined = $state<{ ssid: string; address: string } | null>(null);

  const chosen = $derived(networks?.find((n) => n.ssid === selected));

  api.scanWifi().then((list) => {
    networks = list;
    selected = list[0]?.ssid ?? null;
  });

  function strength(rssi: number) {
    return rssi > -60 ? 'wifi-high' : rssi > -72 ? 'wifi-medium' : 'wifi-low';
  }

  async function connect(e: SubmitEvent) {
    e.preventDefault();
    if (!selected) return;
    connecting = true;
    error = null;
    try {
      const { address } = await api.connectWifi(selected, password);
      app.finishSetup();
      if (address && location.hostname === '192.168.4.1') {
        joined = { ssid: selected, address };
        return;
      }
      await app.refreshHub();
      go(href.dashboard);
    } catch (err) {
      error = err instanceof Error ? err.message : String(err);
    } finally {
      connecting = false;
    }
  }

  function stayOffline() {
    app.finishSetup();
    go(href.dashboard);
  }
</script>

<div class="screen onboarding">
  {#if joined}
    <div class="screen-body">
      <div class="intro">
        <div class="badge"><Icon name="check-circle" size="24px" /></div>
        <div class="eyebrow">Step 3 of 3</div>
        <h1 class="title">The hub is on {joined.ssid}</h1>
        <p class="lede">
          Switch this phone back to <span class="hl">{joined.ssid}</span>, then open the hub at its new address.
          Its setup network closes in two minutes.
        </p>
      </div>
      <div class="panel address">
        <span class="eyebrow">Hub address</span>
        <span class="mono addr">http://{joined.address}</span>
      </div>
      <div class="spacer"></div>
      <a class="btn btn-primary btn-block" style="height:48px" href={`http://${joined.address}/`}>Open FanWave</a>
    </div>
  {:else}
  <form class="screen-body" onsubmit={connect}>
    <div class="intro">
      <div class="badge"><Icon name="fan" size="24px" /></div>
      <div class="eyebrow">Step 2 of 3</div>
      <h1 class="title">Connect the hub to your Wi‑Fi</h1>
      <p class="lede">
        You're on the hub's setup network <span class="hl">{app.hub?.name ?? 'FanWave'}</span>. Pick your home network.
      </p>
    </div>

    <fieldset class="panel">
      <legend class="visually-hidden">Wi‑Fi network</legend>
      {#if !networks}
        <div class="row muted">Scanning…</div>
      {:else}
        {#each networks as n (n.ssid)}
          <label class="row">
            <input type="radio" name="ssid" value={n.ssid} bind:group={selected} class="visually-hidden" />
            <span class="muted"><Icon name={strength(n.rssi)} /></span>
            <span class="grow">{n.ssid}</span>
            {#if n.secured}<span class="muted small"><Icon name="lock-simple" label="Secured" /></span>{/if}
            {#if n.ssid === selected}<span class="accent"><Icon name="check-circle-fill" label="Selected" /></span>{/if}
          </label>
        {/each}
      {/if}
    </fieldset>

    {#if chosen?.secured}
      <div class="field">
        <label for="wifi-password">Password for {chosen.ssid}</label>
        <input id="wifi-password" class="input" type="password" autocomplete="current-password" bind:value={password} />
      </div>
    {/if}

    {#if error}<p class="error" role="alert">{error}</p>{/if}

    <div class="spacer"></div>
    <div class="buttons">
      <button class="btn btn-primary btn-block" style="height:48px" disabled={!selected || connecting}>
        {connecting ? 'Connecting…' : 'Connect'}
      </button>
      <button type="button" class="btn btn-ghost btn-block" style="height:44px" onclick={stayOffline}>
        Stay offline (access point only)
      </button>
    </div>
  </form>
  {/if}
</div>

<style>
  .onboarding { background: radial-gradient(120% 60% at 0% 0%, var(--color-accent-900), var(--color-bg) 60%); }
  .onboarding .screen-body { padding-top: 40px; gap: 24px; }
  .intro { display: flex; flex-direction: column; gap: 10px; }
  .badge {
    width: 44px; height: 44px; border-radius: var(--radius-md);
    border: 1px solid var(--color-accent); color: var(--color-accent);
    display: flex; align-items: center; justify-content: center;
    box-shadow: 0 0 24px var(--color-accent-800); margin-bottom: 8px;
  }
  .title { margin: 0; font-size: 26px; font-weight: 500; line-height: 1.2; }
  .hl { color: var(--color-text); }
  fieldset { margin: 0; padding: 0; border: 0; min-width: 0; }
  fieldset label { cursor: pointer; }
  fieldset label:has(input:focus-visible) { outline: 2px solid var(--color-accent); outline-offset: -2px; }
  .panel > .row + .row { border-top: 1px solid var(--color-divider); }
  .grow { flex: 1; }
  .muted { color: var(--color-neutral-400); }
  .small { font-size: 13px; }
  .accent { color: var(--color-accent); }
  .field { display: flex; flex-direction: column; }
  .field label { color: var(--color-neutral-400); }
  .buttons { display: flex; flex-direction: column; gap: 10px; }
  .error { margin: 0; font-size: 13px; color: var(--color-accent-300); }
  .address { padding: 14px; gap: 4px; }
  .addr { font-size: 18px; word-break: break-all; }
</style>
