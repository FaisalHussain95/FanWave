<script lang="ts">
  // A fan's remote: its address, pairing it with the fan's receiver, making a
  // virtual copy under a new address for another fan, and removing it.
  import { api, app } from '../lib/app.svelte';
  import Icon from '../lib/Icon.svelte';
  import { go, href } from '../lib/router.svelte';

  let { id }: { id: string } = $props();

  const fan = $derived(app.fan(id));

  let pairing = $state(false);
  let pairResult = $state<string | null>(null);

  let copyName = $state('');
  let copyRoom = $state(app.rooms[0]?.id ?? '');
  let copyAddress = $state('');
  let copying = $state(false);
  let copyError = $state<string | null>(null);

  let confirmRemove = $state(false);
  let removing = $state(false);

  $effect(() => {
    if (fan) copyRoom = fan.roomId;
  });

  async function pair() {
    if (!fan) return;
    pairing = true;
    pairResult = null;
    try {
      const { sent } = await api.pairFan(fan.id);
      pairResult =
        sent === 'pair'
          ? 'Pair signal sent. If the fan beeped or blinked, it has learned this remote.'
          : 'Sent Power (no Pair button was learned). If the fan beeped or blinked, it has learned this remote.';
    } catch (e) {
      pairResult = e instanceof Error ? e.message : String(e);
    } finally {
      pairing = false;
    }
  }

  async function duplicate(e: SubmitEvent) {
    e.preventDefault();
    if (!fan) return;
    copying = true;
    copyError = null;
    try {
      const copy = await app.duplicateFan(fan.id, {
        name: copyName.trim(),
        roomId: copyRoom,
        ...(copyAddress.trim() ? { address: copyAddress.trim() } : {}),
      });
      app.notify(`${copy.name} created with address ${copy.address}. Now pair it with its fan.`);
      copyName = '';
      copyAddress = '';
      go(href.remote(copy.id));
    } catch (err) {
      copyError = err instanceof Error ? err.message : String(err);
    } finally {
      copying = false;
    }
  }

  async function remove() {
    if (!fan) return;
    removing = true;
    try {
      const name = fan.name;
      await app.removeFan(fan.id);
      app.notify(`${name} removed.`);
      go(href.dashboard);
    } catch (e) {
      app.notify(e instanceof Error ? e.message : String(e));
      removing = false;
    }
  }
</script>

{#if !fan}
  <div class="screen">
    <div class="screen-body">
      <h1 class="screen-title">Fan not found</h1>
      <p class="lede">It may have been removed from the hub.</p>
      <a class="btn btn-primary" href={href.dashboard}>Back to fans</a>
    </div>
  </div>
{:else}
  <div class="screen">
    <div class="screen-body remote">
      <div class="top">
        <a class="btn btn-icon btn-ghost icon-btn" href={href.fan(fan.id)} aria-label="Back to {fan.name}"><Icon name="caret-left" /></a>
      </div>
      <div>
        <span class="eyebrow">{fan.name}</span>
        <h1 class="screen-title">Remote</h1>
      </div>

      <div class="panel info">
        <div class="row"><span class="k">Protocol</span><span>{fan.protocolLabel}</span></div>
        <div class="row"><span class="k">Address</span><span class="mono">{fan.address ?? '—'}</span></div>
        <div class="row"><span class="k">Frequency</span><span class="mono">{fan.frequencyMHz} MHz{fan.modulation ? ` · ${fan.modulation}` : ''}</span></div>
        {#if fan.copyOf}
          <div class="row"><span class="k">Virtual copy of</span><span>{fan.copyOf}</span></div>
        {/if}
      </div>

      <section>
        <h2 class="h2"><span class="accent"><Icon name="broadcast" /></span>Pair with the fan</h2>
        <ol class="steps-list">
          <li>Switch the fan's power off at the wall switch or breaker, then back on.</li>
          <li>Within a few seconds, tap <strong>Send pair signal</strong>.</li>
          <li>Most receivers beep or blink the light once they've learned the remote.</li>
        </ol>
        <p class="hint">
          {fan.pairCode
            ? 'Sends the Pair button learned from the remote, under this remote’s address.'
            : 'No Pair button was learned, so this sends Power, which most receivers also accept while pairing.'}
          Only the fan that was just powered on listens for a new remote.
        </p>
        <button class="btn btn-primary" disabled={pairing} onclick={pair}>
          <Icon name="broadcast" />{pairing ? 'Sending…' : 'Send pair signal'}
        </button>
        {#if pairResult}<p class="result" aria-live="polite">{pairResult}</p>{/if}
      </section>

      <section>
        <h2 class="h2"><span class="accent"><Icon name="plus" /></span>Make a virtual copy</h2>
        {#if fan.copyable}
          <p class="hint">
            The same buttons under a new address, for another fan of the same model. Pair the copy with that fan, and
            each fan answers only to its own remote.
          </p>
          <form class="copy" onsubmit={duplicate}>
            <label for="copy-name">Name</label>
            <input id="copy-name" class="input" required maxlength="40" placeholder="Bedroom fan" bind:value={copyName} />
            <label for="copy-room">Room</label>
            <select id="copy-room" class="input" bind:value={copyRoom}>
              {#each app.rooms as r (r.id)}<option value={r.id}>{r.name}</option>{/each}
            </select>
            <label for="copy-address">Address <span class="opt">optional, hex</span></label>
            <input id="copy-address" class="input mono" maxlength="18" placeholder="Random" autocomplete="off" spellcheck="false" bind:value={copyAddress} />
            {#if copyError}<p class="problem">{copyError}</p>{/if}
            <button class="btn btn-primary" disabled={copying || !copyName.trim()}>{copying ? 'Creating…' : 'Create copy'}</button>
          </form>
        {:else}
          <p class="hint">
            {fan.protocol
              ? 'Copies need every button decoded from a single remote, with a known address field. Learn the buttons again from one remote to make this possible.'
              : 'This remote was learned as raw recordings, which carry no address to change, so it can’t be copied.'}
          </p>
        {/if}
      </section>

      <section>
        <h2 class="h2"><span class="accent"><Icon name="trash" /></span>Remove</h2>
        {#if !confirmRemove}
          <button class="btn btn-secondary" onclick={() => (confirmRemove = true)}>Remove {fan.name}…</button>
        {:else}
          <p class="hint">
            Deletes this remote and its learned buttons from the hub. The fan itself keeps the remotes it has paired
            with; to clear those, use the fan's own reset procedure.
          </p>
          <div class="confirm">
            <button class="btn btn-secondary" onclick={() => (confirmRemove = false)}>Cancel</button>
            <button class="btn btn-danger" disabled={removing} onclick={remove}>{removing ? 'Removing…' : 'Remove'}</button>
          </div>
        {/if}
      </section>
    </div>
  </div>
{/if}

<style>
  .remote { gap: 18px; padding-top: 16px; padding-bottom: 32px; }
  .top { display: flex; }
  .info .row { justify-content: space-between; gap: 12px; font-size: 13px; }
  .k { color: var(--color-neutral-400); }
  section { display: flex; flex-direction: column; gap: 10px; }
  .h2 { margin: 0; font-size: 16px; font-weight: 600; display: flex; align-items: center; gap: 8px; }
  .accent { color: var(--color-accent); display: inline-flex; }
  .steps-list { margin: 0; padding-left: 20px; display: flex; flex-direction: column; gap: 4px; font-size: 14px; line-height: 1.45; }
  .hint, .result, .problem { margin: 0; font-size: 12px; line-height: 1.5; color: var(--color-neutral-400); }
  .result { color: var(--color-neutral-200); }
  .problem { color: var(--color-neutral-200); }
  .copy { display: flex; flex-direction: column; gap: 6px; }
  .copy label { font-size: 12px; color: var(--color-neutral-400); margin-top: 4px; }
  .opt { color: var(--color-neutral-600); }
  .copy .btn { margin-top: 8px; }
  .confirm { display: flex; gap: 8px; }
  .confirm .btn { flex: 1; }
  .btn-danger { border-color: var(--color-neutral-300); color: var(--color-neutral-100); }
</style>
