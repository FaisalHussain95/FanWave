<script lang="ts">
  import { icons, type IconName } from './icons';

  let { name, size = '1em', label }: { name: IconName; size?: string; label?: string } = $props();
</script>

<span
  class="icon"
  style:font-size={size}
  role={label ? 'img' : undefined}
  aria-label={label}
  aria-hidden={label ? undefined : 'true'}
>
  <!-- Static SVG strings bundled from @phosphor-icons/core, never user input. -->
  {@html icons[name]}
</span>

<style>
  .icon {
    display: inline-flex;
    flex: none;
    line-height: 0;
  }
  .icon :global(svg) {
    width: 1em;
    height: 1em;
    display: block;
  }
</style>
