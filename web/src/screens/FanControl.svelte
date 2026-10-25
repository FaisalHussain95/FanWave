<script lang="ts">
  import { app } from '../lib/app.svelte';
  import { ago } from '../lib/format';
  import Icon from '../lib/Icon.svelte';
  import type { IconName } from '../lib/icons';
  import { href } from '../lib/router.svelte';
  import type { FanState } from '../lib/types';

  let { id }: { id: string } = $props();

  const fan = $derived(app.fan(id));

  $effect(() => {
    if (fan) app.useFan(fan.id);
  });

  type Extra = { label: string; icon: IconName; key: keyof FanState | null };
  const extras = $derived.by(() => {
    if (!fan) return [];
    const list: Extra[] = [{ label: 'Off', icon: 'power', key: null }];
    if (fan.capabilities.includes('light')) list.push({ label: 'Light', icon: 'lightbulb', key: 'light' });
    if (fan.capabilities.includes('reverse')) list.push({ label: 'Reverse', icon: 'arrows-counter-clockwise', key: 'reverse' });
    if (fan.capabilities.includes('timer')) list.push({ label: 'Timer', icon: 'timer', key: 'timer' });
    return list;
  });

  function extra(x: Extra) {
    if (!fan) return;
    if (x.key === null) app.command(fan.id, { power: false });
    else app.command(fan.id, { [x.key]: !fan.commanded[x.key] });
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
  {@const c = fan.commanded}
  {@const running = c.power}
  <div class="screen control" class:running>
    <div class="screen-body tight">
      <div class="top">
        <a class="btn btn-icon btn-ghost icon-btn" href={href.dashboard} aria-label="Back"><Icon name="caret-left" /></a>
        <a class="btn btn-icon btn-ghost icon-btn" href={href.remote(fan.id)} aria-label="Remote: pair, copy or remove"><Icon name="dots-three" /></a>
      </div>

      <div class="title">
        <h1 class="name">{fan.name}</h1>
        <span class="sub">{fan.kind} · RF {fan.frequencyMHz} MHz · {fan.protocolLabel}</span>
      </div>

      <div class="readout" aria-live="polite">
        <div class="big-row">
          <span class="big">{running ? c.speed : 'Off'}</span>
          {#if running}<span class="of">/ {fan.speeds}</span>{/if}
        </div>
        <div class="sent">
          <span class="ring" class:confirmed={!!fan.confirmed}></span>
          {#if fan.lastSentAt === null}
            Nothing sent yet · state not confirmed
          {:else}
            Sent {ago(fan.lastSentAt, app.now)} · {fan.confirmed ? 'confirmed by remote' : 'state not confirmed'}
          {/if}
        </div>
      </div>

      <div class="speeds-wrap">
        <span class="label" id="speed-label">Fan speed</span>
        <div class="speeds" role="group" aria-labelledby="speed-label" style:grid-template-columns={`repeat(${fan.speeds}, 1fr)`}>
          {#each Array.from({ length: fan.speeds }, (_, i) => i + 1) as n (n)}
            <button
              class="speed"
              class:lit={running && n <= c.speed}
              class:current={running && n === c.speed}
              style:height={`${52 + n * (60 / fan.speeds)}px`}
              aria-pressed={running && n === c.speed}
              aria-label={`Speed ${n}`}
              onclick={() => app.pickSpeed(fan, n)}>{n}</button>
          {/each}
        </div>
      </div>

      <div class="extras" style:grid-template-columns={`repeat(${extras.length}, 1fr)`}>
        {#each extras as x (x.label)}
          {@const on = x.key ? Boolean(c[x.key]) : false}
          <button class="extra" class:on aria-pressed={x.key ? on : undefined} onclick={() => extra(x)}>
            <Icon name={x.icon} size="20px" />{x.label}
          </button>
        {/each}
      </div>
    </div>
  </div>
{/if}

<style>
  .control { background: radial-gradient(90% 50% at 50% 30%, var(--color-bg), var(--color-bg) 70%); transition: background 0.3s; }
  .control.running { background: radial-gradient(90% 50% at 50% 30%, var(--color-accent-900), var(--color-bg) 70%); }
  .top { display: flex; align-items: center; justify-content: space-between; margin: 0 -8px; }
  .title { display: flex; flex-direction: column; gap: 4px; }
  .name { margin: 0; font-size: 28px; font-weight: 500; }
  .sub { font-size: 13px; color: var(--color-neutral-400); }

  .readout { flex: 1; min-height: 180px; display: flex; flex-direction: column; justify-content: center; gap: 4px; }
  .big-row { display: flex; align-items: baseline; gap: 8px; }
  .big { font-size: 112px; font-weight: 500; line-height: 1; letter-spacing: -0.04em; color: var(--color-neutral-600); }
  .running .big { color: var(--color-text); }
  .of { font-size: 16px; color: var(--color-neutral-400); }
  .sent { font-size: 12px; color: var(--color-neutral-500); display: flex; align-items: center; gap: 6px; }
  .ring { width: 7px; height: 7px; border-radius: 50%; border: 1px solid var(--color-neutral-500); }
  .ring.confirmed { background: var(--color-accent); border-color: var(--color-accent); }

  .speeds-wrap { display: flex; flex-direction: column; gap: 8px; }
  .speeds { display: grid; gap: 6px; align-items: end; }
  .speed {
    border-radius: var(--radius-md); cursor: pointer;
    border: 1px solid var(--color-neutral-800); background: transparent; color: var(--color-neutral-500);
    font: 500 15px var(--font-body);
    display: flex; align-items: flex-end; justify-content: center; padding-bottom: 10px;
    transition: background 0.15s, border-color 0.15s;
  }
  .speed.lit { border-color: var(--color-accent); background: var(--color-accent-900); color: var(--color-accent-200); }
  .speed.current { background: var(--color-accent-800); }

  .extras { display: grid; gap: 8px; }
  .extra {
    height: 64px; border-radius: var(--radius-md); cursor: pointer;
    border: 1px solid var(--color-neutral-800); background: transparent; color: var(--color-neutral-300);
    display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 4px;
    font: 500 11px var(--font-body);
  }
  .extra.on { border-color: var(--color-accent); background: var(--color-accent-900); color: var(--color-accent-200); }
  .extra:not(.on):hover { background: color-mix(in srgb, var(--color-text) 6%, transparent); }
</style>
