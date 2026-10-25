<script lang="ts">
  // What a frequency scan heard: the background with nothing pressed, and the
  // level while a button was held. A remote is where the two lines part; a
  // spur from the hub itself is loud in both. Points are spaced by step, not
  // by MHz, so the empty gap between the CC1101's bands takes no width; the
  // line breaks there instead.
  let { spectrum, markMHz = null }: { spectrum: [number, number, number][]; markMHz?: number | null } = $props();

  const W = 300;
  const H = 90;

  const lo = $derived(Math.min(-110, ...spectrum.map((p) => Math.min(p[1], p[2]))));
  const hi = $derived(Math.max(-30, ...spectrum.map((p) => Math.max(p[1], p[2]))));
  const x = (i: number) => (spectrum.length > 1 ? (i / (spectrum.length - 1)) * W : W / 2);
  const y = (dbm: number) => H - ((dbm - lo) / (hi - lo)) * H;

  /** Index ranges with no band gap inside. */
  const segments = $derived.by(() => {
    const out: [number, number][] = [];
    let start = 0;
    for (let i = 1; i <= spectrum.length; i++) {
      if (i === spectrum.length || spectrum[i][0] - spectrum[i - 1][0] > 5) {
        out.push([start, i - 1]);
        start = i;
      }
    }
    return out;
  });

  function path(series: 1 | 2) {
    return segments
      .map(([a, b]) => {
        let d = `M${x(a).toFixed(1)} ${y(spectrum[a][series]).toFixed(1)}`;
        for (let i = a + 1; i <= b; i++) d += `L${x(i).toFixed(1)} ${y(spectrum[i][series]).toFixed(1)}`;
        return d;
      })
      .join('');
  }

  const nearest = (mhz: number) =>
    spectrum.reduce((best, p, i) => (Math.abs(p[0] - mhz) < Math.abs(spectrum[best][0] - mhz) ? i : best), 0);
  const markIndex = $derived(markMHz !== null && spectrum.length ? nearest(markMHz) : null);

  let hover = $state<number | null>(null);

  function onpointermove(e: PointerEvent) {
    const r = (e.currentTarget as SVGElement).getBoundingClientRect();
    const f = Math.min(Math.max((e.clientX - r.left) / r.width, 0), 1);
    hover = Math.round(f * (spectrum.length - 1));
  }

  const shown = $derived(hover ?? markIndex);

  /** Axis labels: the two ends, and one "348 | 387" label at each band gap. */
  const ticks = $derived.by(() => {
    if (!spectrum.length) return [];
    const last = spectrum.length - 1;
    const out = [{ i: 0, text: spectrum[0][0].toFixed(0) }];
    for (let s = 1; s < segments.length; s++) {
      const [a] = segments[s];
      out.push({ i: a, text: `${spectrum[a - 1][0].toFixed(0)} | ${spectrum[a][0].toFixed(0)}` });
    }
    if (last > 0) out.push({ i: last, text: spectrum[last][0].toFixed(0) });
    return out;
  });
</script>

<div class="spectrum">
  <div class="readout mono" aria-live="polite">
    {#if shown !== null}
      {@const p = spectrum[shown]}
      <span class="f">{p[0].toFixed(3)} MHz</span>
      <span>background {p[1]}</span>
      <span>holding {p[2]} dBm</span>
    {:else}
      <span class="muted">Drag across the chart to read it</span>
    {/if}
  </div>

  <svg
    viewBox="0 0 {W} {H}"
    preserveAspectRatio="none"
    role="img"
    aria-label="Signal level across the scanned range, with nothing pressed and while holding the button"
    {onpointermove}
    onpointerleave={() => (hover = null)}
  >
    <line class="grid" x1="0" y1={H - 0.5} x2={W} y2={H - 0.5}></line>
    {#if shown !== null}
      <line class="cross" x1={x(shown)} y1="0" x2={x(shown)} y2={H}></line>
    {/if}
    <!-- Background drawn last: where the two match (noise, the hub's own spurs) it reads as background,
         so only a real rise shows in the accent colour. -->
    <path class="held" d={path(2)}></path>
    <path class="quiet" d={path(1)}></path>
  </svg>

  <div class="axis mono">
    {#each ticks as t (t.i)}
      <span style="left: {(x(t.i) / W) * 100}%">{t.text}</span>
    {/each}
  </div>

  <div class="legend">
    <span><i class="sw quiet"></i>Background</span>
    <span><i class="sw held"></i>Holding the button</span>
    <span class="spacer"></span>
    <span class="mono muted">{Math.round(lo)} to {Math.round(hi)} dBm</span>
  </div>
</div>

<style>
  .spectrum { display: flex; flex-direction: column; gap: 6px; }
  .readout { display: flex; gap: 10px; font-size: 11px; color: var(--color-neutral-400); min-height: 16px; flex-wrap: wrap; }
  .readout .f { color: var(--color-text); }
  .muted { color: var(--color-neutral-500); }
  svg { width: 100%; height: 90px; display: block; touch-action: pan-y; cursor: crosshair; }
  path { fill: none; stroke-width: 2; stroke-linejoin: round; vector-effect: non-scaling-stroke; }
  path.quiet { stroke: var(--color-neutral-600); }
  path.held { stroke: var(--color-accent-400); }
  .grid { stroke: var(--color-neutral-800); stroke-width: 1; vector-effect: non-scaling-stroke; }
  .cross { stroke: var(--color-neutral-500); stroke-width: 1; stroke-dasharray: 3 3; vector-effect: non-scaling-stroke; }
  .axis { position: relative; height: 14px; font-size: 10px; color: var(--color-neutral-500); }
  .axis span { position: absolute; transform: translateX(-50%); white-space: nowrap; }
  .axis span:first-child { transform: none; }
  .axis span:last-child { transform: translateX(-100%); }
  .legend { display: flex; align-items: center; gap: 12px; font-size: 11px; color: var(--color-neutral-400); }
  .legend span { display: inline-flex; align-items: center; gap: 6px; }
  .sw { width: 12px; height: 2px; border-radius: 1px; display: inline-block; }
  .sw.quiet { background: var(--color-neutral-600); }
  .sw.held { background: var(--color-accent-400); }
</style>
