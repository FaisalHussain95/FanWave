<script lang="ts">
  import { onMount } from 'svelte';
  import { api, app } from '../lib/app.svelte';
  import Icon from '../lib/Icon.svelte';
  import type { IconName } from '../lib/icons';
  import { go, href } from '../lib/router.svelte';
  import type { Capability, LearnSlot } from '../lib/types';

  const defs: [string, Capability, IconName][] = [
    ['Power', 'power', 'power'],
    ['Speed 1', 'speed', 'number-one'],
    ['Speed 2', 'speed', 'number-two'],
    ['Speed 3', 'speed', 'number-three'],
    ['Speed 4', 'speed', 'number-four'],
    ['Speed 5', 'speed', 'number-five'],
    ['Speed 6', 'speed', 'number-six'],
    ['Light', 'light', 'lightbulb'],
    ['Reverse', 'reverse', 'arrows-counter-clockwise'],
    ['Timer', 'timer', 'timer'],
  ];
  const iconFor = Object.fromEntries(defs.map(([label, , icon]) => [label, icon])) as Record<string, IconName>;

  let slots = $state<LearnSlot[]>(
    defs.map(([label, capability]) => ({ key: label.toLowerCase().replace(' ', '-'), label, capability, code: null, status: 'todo' })),
  );
  let phase = $state<'learn' | 'details'>('learn');
  let name = $state('');
  let roomId = $state(app.rooms[0]?.id ?? '');
  let kind = $state('Ceiling fan');
  let saving = $state(false);
  let controller: AbortController | null = null;

  const learned = $derived(slots.filter((s) => s.status === 'done').length);
  const active = $derived(slots.find((s) => s.status === 'listening'));

  function stop() {
    controller?.abort();
    controller = null;
    for (const s of slots) if (s.status === 'listening') s.status = 'todo';
  }

  async function listen(slot: LearnSlot) {
    stop();
    slot.status = 'listening';
    const ctl = (controller = new AbortController());
    try {
      const learned = await api.learnButton($state.snapshot(slot), ctl.signal);
      slot.code = learned.code;
      slot.captureId = learned.captureId;
      slot.status = 'done';
      controller = null;
      const next = slots.find((s) => s.status === 'todo');
      if (next) listen(next);
    } catch {
      /* aborted: another row was picked or the user skipped */
    }
  }

  function skip() {
    if (!active) return;
    const slot = active;
    stop();
    slot.status = 'skipped';
    slot.code = null;
    const next = slots.find((s) => s.status === 'todo');
    if (next) listen(next);
  }

  onMount(() => {
    listen(slots[0]);
    return stop;
  });

  function toDetails() {
    stop();
    phase = 'details';
  }

  async function save(e: SubmitEvent) {
    e.preventDefault();
    saving = true;
    try {
      const fan = await api.addFan({
        name: name.trim(),
        roomId,
        kind,
        protocol: app.pair.protocol,
        address: app.pair.address,
        slots: $state.snapshot(slots),
      });
      app.fans.push(fan);
      app.useFan(fan.id);
      app.notify(`${fan.name} is ready`);
      go(href.fan(fan.id));
    } catch (err) {
      app.notify(err instanceof Error ? err.message : String(err));
    } finally {
      saving = false;
    }
  }

  function codeText(s: LearnSlot) {
    if (s.status === 'listening') return 'Listening…';
    if (s.status === 'skipped') return 'Skipped';
    return s.code ?? '—';
  }

  function stateIcon(s: LearnSlot): IconName {
    return s.status === 'done' ? 'check-circle-fill' : s.status === 'listening' ? 'broadcast' : s.status === 'skipped' ? 'minus-circle' : 'circle';
  }
</script>

<div class="screen">
  <div class="screen-body tight">
    <div class="nav">
      {#if phase === 'learn'}
        <a class="btn btn-icon btn-ghost icon-btn" href={href.pair} aria-label="Back"><Icon name="caret-left" /></a>
      {:else}
        <button class="btn btn-icon btn-ghost icon-btn" aria-label="Back" onclick={() => (phase = 'learn')}><Icon name="caret-left" /></button>
      {/if}
      <div class="steps" aria-hidden="true">
        <span class="on"></span><span class="on"></span><span class="on"></span><span class:on={phase === 'details'}></span>
      </div>
    </div>

    {#if phase === 'learn'}
      <div class="intro">
        <span class="eyebrow">Step 3 of 4 · {learned} of {slots.length} learned</span>
        <h1 class="title">Teach each button</h1>
        <p class="lede">Tap a row, then press that button on the remote. Skip what your fan doesn't have.</p>
      </div>

      <ul class="slots">
        {#each slots as s (s.key)}
          <li>
            <button class="slot {s.status}" aria-label={`${s.label}: ${codeText(s)}`} onclick={() => listen(s)}>
              <span class="slot-icon"><Icon name={iconFor[s.label]} size="18px" /></span>
              <span class="slot-label">{s.label}</span>
              <span class="slot-code mono">{codeText(s)}</span>
              <span class="slot-state"><Icon name={stateIcon(s)} size="16px" /></span>
            </button>
          </li>
        {/each}
      </ul>

      <div class="spacer"></div>
      <div class="actions">
        <button class="btn btn-ghost" disabled={!active} onclick={skip}>Skip {active?.label ?? 'button'}</button>
        <button class="btn btn-primary" disabled={learned === 0} onclick={toDetails}>
          Continue with {learned} button{learned === 1 ? '' : 's'}
        </button>
      </div>
    {:else}
      <form class="details" onsubmit={save}>
        <div class="intro">
          <span class="eyebrow">Step 4 of 4</span>
          <h1 class="title">Name your fan</h1>
          <p class="lede">
            {learned} button{learned === 1 ? '' : 's'} learned{app.pair.protocolName ? ` · ${app.pair.protocolName}` : ' · raw codes'}{app.pair.address ? ` · ${app.pair.address}` : ''}.
          </p>
        </div>
        <div class="field">
          <label for="fan-name">Name</label>
          <input id="fan-name" class="input" required maxlength="40" placeholder="Kitchen" bind:value={name} />
        </div>
        <div class="field">
          <label for="fan-room">Room</label>
          <select id="fan-room" class="input" bind:value={roomId}>
            {#each app.rooms as r (r.id)}<option value={r.id}>{r.name}</option>{/each}
          </select>
        </div>
        <fieldset class="field">
          <legend>Type</legend>
          <div class="seg">
            {#each ['Ceiling fan', 'Tower fan', 'Outdoor fan'] as k (k)}
              <label class="seg-opt"><input type="radio" name="kind" value={k} bind:group={kind} />{k}</label>
            {/each}
          </div>
        </fieldset>
        <div class="spacer"></div>
        <button class="btn btn-primary btn-block" style="height:48px" disabled={saving || !name.trim() || !roomId}>
          {saving ? 'Saving…' : 'Save fan'}
        </button>
      </form>
    {/if}
  </div>
</div>

<style>
  .nav { display: flex; align-items: center; gap: 8px; margin-left: -8px; }
  .intro { display: flex; flex-direction: column; gap: 8px; }
  .title { margin: 0; font-size: 24px; font-weight: 500; line-height: 1.2; }

  .slots { list-style: none; margin: 0; padding: 0; display: flex; flex-direction: column; gap: 6px; }
  .slot {
    width: 100%; display: flex; align-items: center; gap: 12px; padding: 10px 12px; min-height: 44px;
    border: 0; border-radius: var(--radius-md); cursor: pointer; text-align: left;
    background: var(--color-surface); box-shadow: var(--shadow-sm);
    --code: var(--color-neutral-600); --state: var(--color-neutral-600);
  }
  .slot.done { --code: var(--color-neutral-400); --state: var(--color-accent); }
  .slot.listening { background: var(--color-accent-900); box-shadow: 0 0 0 1px var(--color-accent); --code: var(--color-accent-300); --state: var(--color-accent); }
  .slot-icon { color: var(--color-neutral-400); display: inline-flex; }
  .slot-label { flex: 1; font-size: 14px; }
  .slot-code { font-size: 12px; color: var(--code); }
  .slot-state { color: var(--state); display: inline-flex; }
  .slot.listening .slot-state { animation: blink 1.2s ease-in-out infinite; }
  @keyframes blink { 50% { opacity: 0.35; } }

  .details { flex: 1; display: flex; flex-direction: column; gap: 20px; }
  .field { display: flex; flex-direction: column; margin: 0; padding: 0; border: 0; min-width: 0; }
  .field legend { font-size: 12px; margin-bottom: 5px; color: color-mix(in srgb, var(--color-text) 70%, transparent); padding: 0; }
  select.input { appearance: none; }
  .seg { display: flex; }
  .seg-opt { flex: 1; justify-content: center; min-height: 40px; position: relative; }
</style>
