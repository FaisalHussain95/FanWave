<script lang="ts">
  import Icon from '../lib/Icon.svelte';
  import type { IconName } from '../lib/icons';
  import { href } from '../lib/router.svelte';

  type Tab = 'Control' | 'Rooms' | 'RF Lab' | 'Settings';
  let { active }: { active: Tab } = $props();

  const tabs: { label: Tab; icon: 'fan' | 'squares-four' | 'wave-sine' | 'gear-six'; to: string }[] = [
    { label: 'Control', icon: 'fan', to: href.dashboard },
    { label: 'Rooms', icon: 'squares-four', to: href.rooms },
    { label: 'RF Lab', icon: 'wave-sine', to: href.lab },
    { label: 'Settings', icon: 'gear-six', to: href.settings },
  ];
</script>

<nav aria-label="Main">
  {#each tabs as t (t.label)}
    {@const on = t.label === active}
    <a href={t.to} class:on aria-current={on ? 'page' : undefined}>
      <Icon name={(on ? `${t.icon}-fill` : t.icon) as IconName} size="22px" />
      {t.label}
    </a>
  {/each}
</nav>

<style>
  nav {
    position: fixed;
    left: 50%;
    bottom: 0;
    transform: translateX(-50%);
    width: 100%;
    max-width: 480px;
    display: flex;
    justify-content: space-around;
    padding: 6px 8px calc(10px + env(safe-area-inset-bottom));
    border-top: 1px solid var(--color-divider);
    background: color-mix(in srgb, var(--color-bg) 92%, transparent);
    backdrop-filter: blur(12px);
    z-index: 10;
  }
  a {
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    gap: 3px;
    min-width: 64px;
    min-height: 52px;
    font-size: 11px;
    text-decoration: none;
    color: var(--color-neutral-500);
    border-radius: var(--radius-md);
  }
  a.on { color: var(--color-accent); }
  a:not(.on):hover { color: var(--color-neutral-300); }
</style>
