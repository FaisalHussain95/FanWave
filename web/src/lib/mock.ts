// In-browser stand-in for the hub, seeded with the fixtures from the design.
// Fans and rooms persist to localStorage so the demo survives a reload.

import type { HubApi, NewFan, PairSignal } from './api';
import type {
  Backup,
  Capture,
  Fan,
  FanState,
  HubInfo,
  LearnSlot,
  LedSettings,
  Modulation,
  ProtocolMatch,
  ReceiverState,
  Room,
  WifiNetwork,
} from './types';
import { bitsToHex, splitFrame } from './rf';

const STORE_KEY = 'fanwave.mock.v1';
const LED_KEY = 'fanwave.mock.led.v1';

const defaultLed: LedSettings = { enabled: true, brightness: 40, idle: 'dim', radioFlashes: true };

function loadLed(): LedSettings {
  try {
    const raw = localStorage.getItem(LED_KEY);
    if (raw) return { ...defaultLed, ...JSON.parse(raw) };
  } catch {
    /* storage unavailable or corrupt: use the defaults */
  }
  return { ...defaultLed };
}

const off: FanState = { power: false, speed: 1, light: false, reverse: false, timer: false };

const seedRooms: Room[] = [
  { id: 'living', name: 'Living' },
  { id: 'bedroom', name: 'Bedroom' },
  { id: 'outdoor', name: 'Outdoor' },
  { id: 'office', name: 'Office' },
];

const seedFans: Fan[] = [
  {
    id: 'living-room', name: 'Living Room', roomId: 'living', kind: 'Ceiling fan',
    protocol: 'pt2262', protocolLabel: 'PT2262 · 0x3A5C1', frequencyMHz: 433.92, speeds: 6,
    capabilities: ['power', 'speed', 'light', 'reverse'],
    commanded: { ...off, power: true, speed: 3, light: true }, confirmed: null, lastSentAt: null,
  },
  {
    id: 'bedroom', name: 'Bedroom', roomId: 'bedroom', kind: 'Ceiling fan',
    protocol: 'ev1527', protocolLabel: 'EV1527 · 0x91F04', frequencyMHz: 433.92, speeds: 3,
    capabilities: ['power', 'speed', 'light'],
    commanded: { ...off, speed: 2 }, confirmed: null, lastSentAt: null,
  },
  {
    id: 'kids-room', name: 'Kids room', roomId: 'bedroom', kind: 'Ceiling fan',
    protocol: null, protocolLabel: 'Raw · 6 codes', frequencyMHz: 433.92, speeds: 3,
    capabilities: ['power', 'speed'],
    commanded: { ...off }, confirmed: null, lastSentAt: null,
  },
  {
    id: 'office', name: 'Office', roomId: 'office', kind: 'Tower fan',
    protocol: 'pt2262', protocolLabel: 'PT2262 · 0x1C0A7', frequencyMHz: 433.92, speeds: 3,
    capabilities: ['power', 'speed', 'light'],
    commanded: { ...off, power: true, speed: 2, light: true }, confirmed: null, lastSentAt: null,
  },
  {
    id: 'terrace', name: 'Terrace', roomId: 'outdoor', kind: 'Outdoor fan',
    protocol: null, protocolLabel: 'FSK · unknown · raw', frequencyMHz: 433.92, speeds: 3,
    capabilities: ['power', 'speed', 'timer'],
    commanded: { ...off }, confirmed: null, lastSentAt: null,
  },
];

const commandNames: Record<string, string> = {
  '0110': 'Power',
  '0001': 'Speed 1',
  '0010': 'Speed 2',
  '0011': 'Speed 3',
  '0100': 'Speed 4',
  '0101': 'Speed 5',
  '0111': 'Speed 6',
  '1000': 'Light',
  '1001': 'Reverse',
  '1010': 'Timer',
};

const slotCommands: Record<string, string> = Object.fromEntries(
  Object.entries(commandNames).map(([bits, name]) => [name, bits]),
);

function capture(id: number, bits: string | null, extra: Partial<Capture> = {}): Capture {
  const cmd = bits ? bits.slice(20) : '';
  return {
    id, at: Date.now() - (15 - id) * 40_000, modulation: 'OOK', rssi: -48, repeats: 3, bits,
    pulseCount: bits ? bits.length * 2 + 2 : 212,
    shortUs: bits ? 350 : null, longUs: bits ? 1050 : null, syncMs: bits ? 10.9 : null,
    durationMs: 24.8,
    label: bits ? (commandNames[cmd] ?? 'Unknown') : 'FSK?',
    ...extra,
  };
}

const seedCaptures: Capture[] = [
  capture(14, '001110100101110000010110'),
  capture(13, '001110100101110000010011'),
  capture(12, '100100011111000001001000', { label: 'Unknown', rssi: -61 }),
  capture(11, null, { modulation: 'FSK', rssi: -70, repeats: 1, label: 'FSK?' }),
];

const wait = (ms: number) => new Promise<void>((r) => setTimeout(r, ms));

function load(): { rooms: Room[]; fans: Fan[] } {
  try {
    const raw = localStorage.getItem(STORE_KEY);
    if (raw) return JSON.parse(raw);
  } catch {
    /* storage unavailable: fall back to the seed */
  }
  return { rooms: structuredClone(seedRooms), fans: structuredClone(seedFans) };
}

function matchesFor(c: Capture): ProtocolMatch[] {
  if (!c.bits) return [];
  const frame = splitFrame(c.bits, commandNames);
  const isEv = c.bits.startsWith('1001');
  const pt: ProtocolMatch = {
    protocol: 'pt2262', name: 'Princeton PT2262', confidence: isEv ? 0.58 : 0.92,
    note: '24 bits · 1:3 ratio · sync 31× short', frame,
  };
  const ev: ProtocolMatch = {
    protocol: 'ev1527', name: 'EV1527', confidence: isEv ? 0.88 : 0.61,
    note: isEv ? '20-bit learning-code address · 4 data bits' : 'Bit timing fits, address length differs', frame,
  };
  const nexa: ProtocolMatch = {
    protocol: 'nexa', name: 'Nexa / HomeEasy', confidence: 0.08,
    note: 'Manchester coding not detected', frame,
  };
  return [pt, ev, nexa].sort((a, b) => b.confidence - a.confidence);
}

export class MockHub implements HubApi {
  private state = load();
  private captureList = structuredClone(seedCaptures);
  private nextCaptureId = 15;
  private listening = true;
  private modulation: Modulation = 'OOK';
  private frequencyMHz = 433.92;
  private captureSubs = new Set<(c: Capture) => void>();
  private captureTimer: ReturnType<typeof setInterval> | null = null;
  private info: HubInfo = {
    name: 'FanWave‑3F2A', hardware: 'ESP32‑S3 · CC1101', host: 'fanwave.local', online: true,
    uptimeSeconds: 6 * 86400 + 4 * 3600, wifiRssi: -58, heapFreeBytes: 212 * 1024,
    wifiNetwork: 'Maison‑5G', accessPointFallback: true, localOnly: true, radioOk: true,
    led: loadLed(),
    firmware: '0.8.3', update: { version: '0.9.0', sizeBytes: 1.4 * 1024 * 1024 },
  };

  private save() {
    try {
      localStorage.setItem(STORE_KEY, JSON.stringify(this.state));
    } catch {
      /* storage unavailable: keep in memory only */
    }
  }

  async hub() {
    await wait(80);
    return structuredClone(this.info);
  }

  async rooms() {
    await wait(60);
    return structuredClone(this.state.rooms);
  }

  async addRoom(name: string) {
    await wait(120);
    const base = name.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '') || 'room';
    let id = base;
    for (let n = 2; this.state.rooms.some((r) => r.id === id); n++) id = `${base}-${n}`;
    const room = { id, name };
    this.state.rooms.push(room);
    this.save();
    return structuredClone(room);
  }

  async fans() {
    await wait(60);
    return structuredClone(this.state.fans);
  }

  async command(fanId: string, patch: Partial<FanState>) {
    await wait(150);
    const fan = this.state.fans.find((f) => f.id === fanId);
    if (!fan) throw new Error(`Unknown fan ${fanId}`);
    fan.commanded = { ...fan.commanded, ...patch };
    fan.lastSentAt = Date.now();
    this.save();
    return structuredClone(fan);
  }

  async addFan(draft: NewFan) {
    await wait(200);
    const learned = draft.slots.filter((s) => s.status === 'done');
    const has = (c: string) => learned.some((s) => s.capability === c);
    const speeds = learned.filter((s) => s.capability === 'speed').length;
    const base = draft.name.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '') || 'fan';
    let id = base;
    for (let n = 2; this.state.fans.some((f) => f.id === id); n++) id = `${base}-${n}`;
    const fan: Fan = {
      id, name: draft.name, roomId: draft.roomId, kind: draft.kind,
      protocol: draft.protocol,
      protocolLabel: draft.protocol
        ? `${draft.protocol.toUpperCase()} · ${draft.address ?? '?'}`
        : `Raw · ${learned.length} codes`,
      frequencyMHz: 433.92,
      speeds: Math.max(speeds, 1),
      capabilities: (['power', 'speed', 'light', 'reverse', 'timer'] as const).filter(
        (c) => c === 'power' || c === 'speed' || has(c),
      ),
      commanded: { ...off }, confirmed: null, lastSentAt: null,
    };
    this.state.fans.push(fan);
    this.save();
    return structuredClone(fan);
  }

  async scanWifi(): Promise<WifiNetwork[]> {
    await wait(600);
    return [
      { ssid: 'Maison‑5G', rssi: -52, secured: true },
      { ssid: 'Maison‑2G', rssi: -60, secured: true },
      { ssid: 'Freebox‑8E21', rssi: -71, secured: true },
      { ssid: 'Guest', rssi: -78, secured: false },
    ];
  }

  async connectWifi(ssid: string, password: string) {
    await wait(1200);
    if (password.length > 0 && password.length < 8) throw new Error('That password is too short for WPA2.');
    this.info.wifiNetwork = ssid;
    return { address: null };
  }

  async captures() {
    await wait(60);
    return structuredClone(this.captureList);
  }

  onFan() {
    return () => {};
  }

  async receiver(): Promise<ReceiverState> {
    await wait(40);
    return { listening: this.listening, modulation: this.modulation, frequencyMHz: this.frequencyMHz };
  }

  async setReceiver(opts: Partial<ReceiverState>) {
    await wait(60);
    this.listening = opts.listening ?? this.listening;
    this.modulation = opts.modulation ?? this.modulation;
    this.frequencyMHz = opts.frequencyMHz ?? this.frequencyMHz;
    this.syncCaptureTimer();
    return { listening: this.listening, modulation: this.modulation, frequencyMHz: this.frequencyMHz };
  }

  onCapture(cb: (c: Capture) => void) {
    this.captureSubs.add(cb);
    this.syncCaptureTimer();
    return () => {
      this.captureSubs.delete(cb);
      this.syncCaptureTimer();
    };
  }

  private syncCaptureTimer() {
    const want = this.listening && this.captureSubs.size > 0;
    if (want && !this.captureTimer) {
      this.captureTimer = setInterval(() => this.emitCapture(), 9000);
    } else if (!want && this.captureTimer) {
      clearInterval(this.captureTimer);
      this.captureTimer = null;
    }
  }

  private emitCapture() {
    const template = seedCaptures[this.nextCaptureId % 3];
    const c: Capture = {
      ...structuredClone(template),
      id: this.nextCaptureId++,
      at: Date.now(),
      modulation: this.modulation,
      rssi: -45 - Math.round(Math.random() * 20),
    };
    this.captureList.unshift(c);
    this.captureList = this.captureList.slice(0, 20);
    for (const cb of this.captureSubs) cb(structuredClone(c));
  }

  async analyze(captureId: number) {
    await wait(300);
    const c = this.captureList.find((x) => x.id === captureId);
    return c ? matchesFor(c) : [];
  }

  listenForRemote(cb: (s: PairSignal) => void) {
    let count = 0;
    const bits = seedCaptures[0].bits!;
    const timer = setInterval(() => {
      count++;
      cb({
        count,
        rssi: -48,
        match: count > 1 ? matchesFor(seedCaptures[0])[0] : null,
        address: count > 1 ? bitsToHex(bits.slice(0, 20)) : null,
      });
      if (count >= 3) clearInterval(timer);
    }, 1100);
    return () => clearInterval(timer);
  }

  learnButton(slot: LearnSlot, signal: AbortSignal) {
    return new Promise<{ code: string; captureId: number | null }>((resolve, reject) => {
      const t = setTimeout(() => {
        const cmd = slotCommands[slot.label] ?? '1111';
        resolve({ code: `0x3A5C1 · ${cmd}`, captureId: null });
      }, 1400);
      signal.addEventListener('abort', () => {
        clearTimeout(t);
        reject(new DOMException('Stopped listening', 'AbortError'));
      });
    });
  }

  async setLed(patch: Partial<LedSettings>) {
    await wait(80);
    const next = { ...this.info.led, ...patch };
    next.brightness = Math.min(100, Math.max(0, Math.round(next.brightness)));
    this.info.led = next;
    try {
      localStorage.setItem(LED_KEY, JSON.stringify(next));
    } catch {
      /* storage unavailable: keep in memory only */
    }
    return { ...next };
  }

  async identify() {
    await wait(80);
  }

  async uploadFirmware(file: File, onProgress: (f: number) => void) {
    if (!file.name.endsWith('.bin')) throw new Error('Choose a firmware .bin file.');
    for (let i = 1; i <= 10; i++) {
      await wait(120);
      onProgress(i / 10);
    }
    this.info.firmware = '0.9.0';
    this.info.update = null;
    return this.info.firmware;
  }

  async installUpdate(onProgress: (f: number) => void) {
    for (let i = 1; i <= 20; i++) {
      await wait(150);
      onProgress(i / 20);
    }
    if (this.info.update) this.info.firmware = this.info.update.version;
    this.info.update = null;
  }

  async backup(): Promise<Backup> {
    return { version: 1, ...structuredClone(this.state) };
  }

  async restore(b: Backup) {
    if (b?.version !== 1 || !Array.isArray(b.rooms) || !Array.isArray(b.fans)) {
      throw new Error('This file is not a FanWave backup.');
    }
    this.state = { rooms: b.rooms, fans: b.fans };
    this.save();
  }
}
