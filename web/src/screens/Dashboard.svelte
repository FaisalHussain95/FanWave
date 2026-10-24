<script lang="ts">
  import TabBar from '../components/TabBar.svelte';
  import { app } from '../lib/app.svelte';
  import Icon from '../lib/Icon.svelte';
  import { go, href } from '../lib/router.svelte';
  import type { Fan } from '../lib/types';

  let roomFilter = $state<string | null>(null);

  const last = $derived(app.lastFan);
  const shown = $derived(roomFilter ? app.fans.filter((f) => f.roomId === roomFilter) : app.fans);
  const roomsWithFans = $derived(app.rooms.filter((r) => app.fans.some((f) => f.roomId === r.id)));

  function status(f: Fan) {
    const c = f.commanded;
    if (!c.power) return `${f.kind} · off`;
    return `${f.kind} · speed ${c.speed}${c.light && f.capabilities.includes('light') ? ' · light on' : ''}`;
  }

  function open(f: Fan) {
    app.useFan(f.id);
    go(href.fan(f.id));
  }
</script>

<div class="screen has-tabs">
  <div class="screen-body">
    <header class="head">
      <div class="brand">
        <div class="eyebrow status">
          <span class="dot" class:offline={!app.hub?.online}></span>
          {app.hub?.online ? 'Hub online · local' : 'Hub offline'}
        </div>
        <h1 class="screen-title">FanWave</h1>
      </div>
      <a class="btn btn-icon btn-ghost icon-btn" href={href.pair} aria-label="Add fan" onclick={() => app.startPairing()}>
        <Icon name="plus" />
      </a>
    </header>

    {#if last}
      {@const c = last.commanded}
      <button class="hero" onclick={() => open(last)}>
        <span class="hero-top">
          <span class="hero-text">
            <span class="kicker">Last used</span>
            <span class="hero-name">{last.name}</span>
            <span class="hero-sub">{last.kind} · {c.power ? `speed ${c.speed} of ${last.speeds}` : 'off'}</span>
          </span>
          <Icon name="fan" size="36px" />
        </span>
        <span class="bars" aria-hidden="true">
          {#each Array.from({ length: last.speeds }, (_, i) => i + 1) as n (n)}
            <span class:on={c.power && n <= c.speed}></span>
          {/each}
        </span>
      </button>
    {:else}
      <div class="panel empty">
        <p class="lede">No fans yet. Pair one by pressing a button on its remote.</p>
        <a class="btn btn-primary" href={href.pair} onclick={() => app.startPairing()}>Add a fan</a>
      </div>
    {/if}

    {#if roomsWithFans.length > 1}
      <div class="chips" role="group" aria-label="Filter by room">
        <button class="tag" class:tag-accent={!roomFilter} class:tag-outline={!!roomFilter} aria-pressed={!roomFilter} onclick={() => (roomFilter = null)}>All</button>
        {#each roomsWithFans as r (r.id)}
          {@const on = roomFilter === r.id}
          <button class="tag" class:tag-accent={on} class:tag-outline={!on} aria-pressed={on} onclick={() => (roomFilter = r.id)}>{r.name}</button>
        {/each}
      </div>
    {/if}

    <ul class="fans">
      {#each shown as f (f.id)}
        {@const on = f.commanded.power}
        <li class="fan" class:on>
          <button class="fan-main" onclick={() => open(f)}>
            <span class="fan-icon"><Icon name="fan" size="20px" /></span>
            <span class="fan-text">
              <span class="fan-name">{f.name}</span>
              <span class="fan-status">{status(f)}</span>
            </span>
          </button>
          <button class="power" aria-label={`${on ? 'Turn off' : 'Turn on'} ${f.name}`} aria-pressed={on} onclick={() => app.togglePower(f)}>
            <Icon name="power" size="18px" />
          </button>
        </li>
      {/each}
    </ul>
  </div>
  <TabBar active="Control" />
</div>

<style>
  .head { display: flex; align-items: center; justify-content: space-between; }
  .brand { display: flex; flex-direction: column; gap: 2px; }
  .status { display: flex; align-items: center; gap: 6px; }
  .dot.offline { background: var(--color-neutral-600); }

  .hero {
    display: flex; flex-direction: column; gap: 12px; padding: 16px; text-align: left;
    border: 0; border-radius: var(--radius-lg); cursor: pointer;
    background: radial-gradient(140% 120% at 0% 0%, var(--color-accent-900), var(--color-surface) 70%);
    box-shadow: var(--shadow-sm);
  }
  .hero:hover { box-shadow: var(--shadow-md); }
  .hero-top { display: flex; justify-content: space-between; align-items: flex-start; color: var(--color-accent); }
  .hero-text { display: flex; flex-direction: column; gap: 2px; color: var(--color-text); }
  .kicker { font-size: 11px; color: var(--color-accent-300); letter-spacing: 0.06em; text-transform: uppercase; }
  .hero-name { font-size: 18px; font-weight: 500; }
  .hero-sub { font-size: 12px; color: var(--color-neutral-400); }
  .bars { display: flex; gap: 4px; }
  .bars span { flex: 1; height: 6px; border-radius: 3px; background: var(--color-neutral-800); }
  .bars span.on { background: var(--color-accent); }

  .empty { padding: 16px; gap: 12px; align-items: flex-start; }

  .chips { display: flex; gap: 8px; flex-wrap: wrap; }
  .chips .tag { cursor: pointer; border: 1px solid transparent; min-height: 32px; font: inherit; font-size: 12px; }
  .chips .tag-outline { border-color: var(--color-accent); background: transparent; }

  .fans { list-style: none; margin: 0; padding: 0; display: flex; flex-direction: column; gap: 8px; }
  .fan {
    display: flex; align-items: center; gap: 12px; padding: 12px;
    border-radius: var(--radius-md); background: var(--color-surface); box-shadow: var(--shadow-sm);
    --fan-fg: var(--color-neutral-500); --fan-bg: var(--color-neutral-900); --fan-border: var(--color-neutral-800);
  }
  .fan.on { --fan-fg: var(--color-accent); --fan-bg: var(--color-accent-900); --fan-border: var(--color-accent); }
  .fan-main { flex: 1; min-width: 0; display: flex; align-items: center; gap: 12px; background: none; border: 0; padding: 0; text-align: left; cursor: pointer; }
  .fan-icon {
    width: 40px; height: 40px; border-radius: var(--radius-md); flex: none;
    display: flex; align-items: center; justify-content: center;
    background: var(--fan-bg); color: var(--fan-fg);
  }
  .fan-text { display: flex; flex-direction: column; gap: 2px; min-width: 0; }
  .fan-name { font-size: 14px; font-weight: 500; }
  .fan-status { font-size: 12px; color: var(--color-neutral-400); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .power {
    width: 44px; height: 44px; flex: none; border-radius: var(--radius-md); cursor: pointer;
    border: 1px solid var(--fan-border); background: transparent; color: var(--fan-fg);
    display: flex; align-items: center; justify-content: center;
  }
  .power:hover { background: color-mix(in srgb, var(--fan-fg) 12%, transparent); }
</style>
