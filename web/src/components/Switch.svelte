<script lang="ts">
  let {
    checked,
    label,
    disabled = false,
    onchange,
  }: { checked: boolean; label: string; disabled?: boolean; onchange: (checked: boolean) => void } = $props();
</script>

<button
  type="button"
  class="switch"
  role="switch"
  aria-checked={checked}
  aria-label={label}
  {disabled}
  onclick={() => onchange(!checked)}
>
  <span class="thumb"></span>
</button>

<style>
  .switch {
    position: relative;
    flex: none;
    width: 44px;
    height: 26px;
    padding: 0;
    border-radius: 13px;
    border: 1px solid var(--color-neutral-700);
    background: var(--color-neutral-900);
    cursor: pointer;
    transition: background 0.15s, border-color 0.15s;
  }
  .switch[aria-checked='true'] {
    border-color: var(--color-accent);
    background: var(--color-accent-800);
  }
  .switch:disabled { opacity: 0.45; cursor: not-allowed; }
  .thumb {
    position: absolute;
    top: 3px;
    left: 3px;
    width: 18px;
    height: 18px;
    border-radius: 50%;
    background: var(--color-neutral-400);
    transition: transform 0.15s, background 0.15s;
  }
  .switch[aria-checked='true'] .thumb {
    transform: translateX(18px);
    background: var(--color-accent-200);
  }
</style>
