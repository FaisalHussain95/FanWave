<script lang="ts">
  import TabBar from '../components/TabBar.svelte';
  import { app } from '../lib/app.svelte';
  import Icon from '../lib/Icon.svelte';
  import type { IconName } from '../lib/icons';
  import { href } from '../lib/router.svelte';
  import type { Capability } from '../lib/types';

  const capIcons: Partial<Record<Capability, IconName>> = {
    speed: 'gauge',
    light: 'lightbulb',
    reverse: 'arrows-counter-clockwise',
    timer: 'timer',
  };

  let adding = $state(false);
  let newRoom = $state('');
  let input = $state<HTMLInputElement>();

  const groups = $derived(app.rooms.map((r) => ({ room: r, fans: app.fans.filter((f) => f.roomId === r.id) })));

  async function add(e: SubmitEvent) {
    e.preventDefault();
    const name = newRoom.trim();
    if (!name) return;
    await app.addRoom(name);
    newRoom = '';
    adding = false;
  }

  $effect(() => {
    if (adding) input?.focus();
  });
</script>

<div class="screen has-tabs">
  <div class="screen-body">
    <div class="head">
      <h1 class="screen-title">Rooms</h1>
      <button class="btn btn-secondary" style="height:36px" aria-expanded={adding} onclick={() => (adding = !adding)}>
        <Icon name="plus" />Room
      </button>
    </div>

    {#if adding}
      <form class="add" onsubmit={add}>
        <label class="visually-hidden" for="room-name">Room name</label>
        <input id="room-name" class="input" placeholder="Kitchen" maxlength="30" bind:value={newRoom} bind:this={input} />
        <button class="btn btn-primary" disabled={!newRoom.trim()}>Add</button>
      </form>
    {/if}

    {#each groups as g (g.room.id)}
      <section class="group" aria-labelledby={`room-${g.room.id}`}>
        <div class="group-head">
          <h2 id={`room-${g.room.id}`}>{g.room.name}</h2>
          <span>{g.fans.length} fan{g.fans.length === 1 ? '' : 's'}</span>
        </div>
        <div class="panel">
          {#each g.fans as f (f.id)}
            <a class="row" href={href.fan(f.id)}>
              <span class="faint"><Icon name="dots-six-vertical" /></span>
              <span class="text">
                <span class="name">{f.name}</span>
                <span class="meta mono">{f.protocolLabel}</span>
              </span>
              <span class="caps">
                {#each f.capabilities as c (c)}
                  {@const icon = capIcons[c]}
                  {#if icon}<Icon name={icon} label={c} />{/if}
                {/each}
              </span>
              <span class="faint"><Icon name="caret-right" /></span>
            </a>
          {:else}
            <div class="row none">No fans yet</div>
          {/each}
        </div>
      </section>
    {/each}

    <a class="btn btn-ghost" href={href.pair} onclick={() => app.startPairing()}><Icon name="plus" />Add a fan</a>
  </div>
  <TabBar active="Rooms" />
</div>

<style>
  .head { display: flex; align-items: center; justify-content: space-between; }
  .add { display: flex; gap: 8px; }
  .add .input { flex: 1; }
  .group { display: flex; flex-direction: column; gap: 8px; }
  .group-head { display: flex; justify-content: space-between; font-size: 12px; color: var(--color-neutral-400); text-transform: uppercase; letter-spacing: 0.06em; }
  .group-head h2 { margin: 0; font: inherit; letter-spacing: inherit; }
  .faint { color: var(--color-neutral-600); display: inline-flex; }
  .text { flex: 1; display: flex; flex-direction: column; gap: 3px; min-width: 0; }
  .name { font-size: 14px; font-weight: 500; }
  .meta { font-size: 11px; color: var(--color-neutral-500); }
  .caps { display: flex; gap: 4px; color: var(--color-neutral-400); font-size: 14px; }
  .none { color: var(--color-neutral-500); font-size: 13px; }
</style>
