<script lang="ts">
  import { app } from './lib/app.svelte';
  import { router } from './lib/router.svelte';
  import Dashboard from './screens/Dashboard.svelte';
  import FanControl from './screens/FanControl.svelte';
  import FanRemote from './screens/FanRemote.svelte';
  import Onboarding from './screens/Onboarding.svelte';
  import PairListen from './screens/PairListen.svelte';
  import PairLearn from './screens/PairLearn.svelte';
  import ProtocolDetective from './screens/ProtocolDetective.svelte';
  import RfExplorer from './screens/RfExplorer.svelte';
  import Rooms from './screens/Rooms.svelte';
  import Settings from './screens/Settings.svelte';

  app.load();

  const route = $derived(app.needsSetup && router.route.name === 'dashboard' ? { name: 'setup' as const } : router.route);
</script>

{#if !app.loaded}
  <div class="screen" aria-busy="true"></div>
{:else if app.loadError}
  <div class="screen">
    <div class="screen-body">
      <h1 class="screen-title">Can't reach the hub</h1>
      <p class="lede">{app.loadError}</p>
      <button class="btn btn-primary" onclick={() => app.load()}>Try again</button>
    </div>
  </div>
{:else if route.name === 'setup'}
  <Onboarding />
{:else if route.name === 'dashboard'}
  <Dashboard />
{:else if route.name === 'fan'}
  {#key route.id}<FanControl id={route.id} />{/key}
{:else if route.name === 'remote'}
  {#key route.id}<FanRemote id={route.id} />{/key}
{:else if route.name === 'rooms'}
  <Rooms />
{:else if route.name === 'settings'}
  <Settings />
{:else if route.name === 'lab'}
  <RfExplorer />
{:else if route.name === 'detective'}
  {#key route.captureId}<ProtocolDetective captureId={route.captureId} />{/key}
{:else if route.name === 'pair'}
  <PairListen />
{:else if route.name === 'learn'}
  <PairLearn />
{/if}

{#if app.toast}
  <div class="toast" role="status">{app.toast}</div>
{/if}
