<script lang="ts">
  import { api, app } from '../lib/app.svelte';
  import { percent } from '../lib/format';
  import Icon from '../lib/Icon.svelte';
  import { bitsToHex } from '../lib/rf';
  import { go, href } from '../lib/router.svelte';
  import type { Capture, ProtocolMatch } from '../lib/types';

  let { captureId }: { captureId: number } = $props();

  let capture = $state<Capture | null>(null);
  let matches = $state<ProtocolMatch[] | null>(null);
  let pickedIndex = $state(0);

  const picked = $derived(matches?.[pickedIndex] ?? null);
  const shortName = $derived(picked?.name.replace(/^Princeton /, '') ?? '');

  $effect(() => {
    const id = captureId;
    Promise.all([api.captures(), api.analyze(id)]).then(([list, m]) => {
      capture = list.find((c) => c.id === id) ?? null;
      matches = m;
    });
  });

  function use() {
    if (!picked) return;
    const address = picked.frame.find((f) => f.kind === 'address')?.value ?? null;
    app.startPairing({ protocol: picked.protocol, protocolName: picked.name, address, match: picked });
    go(href.learn);
  }

  function saveRaw() {
    app.startPairing();
    go(href.learn);
  }
</script>

<div class="screen">
  <div class="screen-body tight det">
    <div class="head">
      <a class="btn btn-icon btn-ghost icon-btn" href={href.lab} aria-label="Back"><Icon name="caret-left" /></a>
      <div class="stack">
        <h1 class="title">Protocol Detective</h1>
        <span class="sub mono">Capture #{captureId}{capture?.bits ? ` · ${bitsToHex(capture.bits)}` : ''}</span>
      </div>
    </div>

    {#if matches === null}
      <p class="lede">Comparing against known decoders…</p>
    {:else if matches.length === 0}
      <div class="panel none">
        <span class="strong">No decoder matched</span>
        <p class="lede">This capture didn't decode to a fixed-width frame. Save it as a raw code and FanWave will replay the exact timings.</p>
      </div>
    {:else}
      <div class="section">
        <span class="label" id="cands">Candidate decoders</span>
        <div class="cands" role="radiogroup" aria-labelledby="cands">
          {#each matches as m, i (m.protocol)}
            {@const on = i === pickedIndex}
            <button class="cand" class:on role="radio" aria-checked={on} onclick={() => (pickedIndex = i)}>
              <span class="cand-top"><span class="strong">{m.name}</span><span class="pct mono">{percent(m.confidence)}</span></span>
              <span class="meter"><span style:width={percent(m.confidence)}></span></span>
              <span class="note">{m.note}</span>
            </button>
          {/each}
        </div>
      </div>

      {#if picked}
        <div class="section">
          <span class="label">Frame structure · {shortName}</span>
          <div class="frame" aria-hidden="true">
            {#each picked.frame as f (f.label)}
              <span class="seg-box {f.kind}" style:flex={f.weight}>{f.short}</span>
            {/each}
          </div>
          <div class="panel">
            {#each picked.frame as f (f.label)}
              <div class="frow">
                <span class="muted">{f.label}</span>
                <span class="mono">{f.bits}</span>
                <span class="mono val">{f.value}</span>
              </div>
            {/each}
          </div>
        </div>
      {/if}
    {/if}

    <div class="spacer"></div>
    <div class="actions">
      <button class="btn btn-ghost" onclick={saveRaw}>Save as raw</button>
      <button class="btn btn-primary" disabled={!picked} onclick={use}>Use {shortName || 'decoder'}</button>
    </div>
  </div>
</div>

<style>
  .det { --gutter: 16px; gap: 16px; }
  .head { display: flex; align-items: center; gap: 4px; margin-left: -8px; }
  .stack { display: flex; flex-direction: column; }
  .title { margin: 0; font-size: 18px; font-weight: 500; }
  .sub { font-size: 11px; color: var(--color-neutral-500); }
  .strong { font-size: 14px; font-weight: 500; }
  .none { padding: 14px; gap: 6px; }
  .section { display: flex; flex-direction: column; gap: 8px; }
  .cands { display: flex; flex-direction: column; gap: 8px; }
  .cand {
    display: flex; flex-direction: column; gap: 8px; padding: 12px; text-align: left; cursor: pointer;
    border: 0; border-radius: var(--radius-md); background: var(--color-surface); box-shadow: var(--shadow-sm);
    --pct: var(--color-neutral-400); --bar: var(--color-neutral-600);
  }
  .cand.on { box-shadow: 0 0 0 1px var(--color-accent); --pct: var(--color-accent-300); --bar: var(--color-accent); }
  .cand-top { display: flex; justify-content: space-between; align-items: baseline; }
  .pct { font-size: 14px; color: var(--pct); }
  .meter { height: 4px; border-radius: 2px; background: var(--color-neutral-800); overflow: hidden; }
  .meter span { display: block; height: 100%; background: var(--bar); border-radius: 2px; }
  .note { font-size: 11px; color: var(--color-neutral-400); }

  .frame { display: flex; gap: 3px; height: 36px; }
  .seg-box {
    border-radius: var(--radius-sm); display: flex; align-items: center; justify-content: center; font-size: 10px;
    background: var(--color-neutral-900); border: 1px solid var(--color-neutral-800); color: var(--color-accent-200);
  }
  .seg-box.sync { color: var(--color-neutral-400); }
  .seg-box.address { background: var(--color-accent-900); border-color: var(--color-accent-700); }
  .seg-box.command { background: var(--color-accent-800); border-color: var(--color-accent); }
  .frow { display: grid; grid-template-columns: 72px 1fr auto; gap: 8px; align-items: center; padding: 9px 12px; font-size: 12px; }
  .frow + .frow { border-top: 1px solid var(--color-divider); }
  .muted { color: var(--color-neutral-400); }
  .val { color: var(--color-accent-300); }
</style>
