import type { FrameField } from './types';

/** "0011 1010" → "0x3A". Pads on the left to whole nibbles. */
export function bitsToHex(bits: string): string {
  const padded = bits.padStart(Math.ceil(bits.length / 4) * 4, '0');
  let hex = '';
  for (let i = 0; i < padded.length; i += 4) hex += parseInt(padded.slice(i, i + 4), 2).toString(16);
  return '0x' + hex.toUpperCase();
}

/** Group bits in nibbles for display. */
export function nibbles(bits: string): string {
  return bits.replace(/(.{4})(?=.)/g, '$1 ');
}

/**
 * SVG path for an OOK pulse train: each bit is a high then a low, long-short
 * for 1 and short-long for 0, in a 300×70 box with the baseline at y=60.
 */
export function wavePath(bits: string | null, width = 300): string {
  const hiY = 14;
  const loY = 60;
  if (!bits) {
    // Undecoded capture: draw an irregular train so it still reads as a signal.
    let d = `M0 ${loY}`;
    let x = 2;
    let seed = 7;
    while (x < width - 4) {
      seed = (seed * 9301 + 49297) % 233280;
      const hi = 2 + (seed % 7);
      const lo = 2 + ((seed >> 3) % 6);
      d += ` L${x} ${hiY} L${x + hi} ${hiY} L${x + hi} ${loY} L${x + hi + lo} ${loY}`;
      x += hi + lo;
    }
    return d + ` L${width} ${loY}`;
  }
  const unit = (width - 8) / (bits.length * 12);
  let x = 4;
  let d = `M0 ${loY} L${x} ${loY}`;
  for (const b of bits) {
    const hi = (b === '1' ? 9 : 3) * unit;
    const lo = (b === '1' ? 3 : 9) * unit;
    d += ` L${x.toFixed(1)} ${hiY} L${(x + hi).toFixed(1)} ${hiY} L${(x + hi).toFixed(1)} ${loY} L${(x + hi + lo).toFixed(1)} ${loY}`;
    x += hi + lo;
  }
  return d + ` L${width} ${loY}`;
}

/** Split a 24-bit fixed-code frame into sync, 20-bit address, 4-bit command and stop. */
export function splitFrame(bits: string, commandNames: Record<string, string>): FrameField[] {
  const address = bits.slice(0, 20);
  const command = bits.slice(20);
  return [
    { label: 'Sync', short: 'S', weight: 2, bits: '—', value: 'pulse 31×', kind: 'sync' },
    { label: 'Address', short: 'Address', weight: 10, bits: nibbles(address), value: bitsToHex(address), kind: 'address' },
    { label: 'Command', short: 'Cmd', weight: 3, bits: command, value: commandNames[command] ?? bitsToHex(command), kind: 'command' },
    { label: 'Stop', short: '', weight: 1, bits: '1', value: '—', kind: 'stop' },
  ];
}
