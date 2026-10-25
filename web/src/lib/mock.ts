// In-browser stand-in for the hub, seeded with the fixtures from the design.
// Fans and rooms persist to localStorage so the demo survives a reload.

import type { HubApi, NewFan, PairSignal } from './api';
import { read } from './storage';
import type {
  Backup,
  Capture,
  Fan,
  FanState,
  FilteredReport,
  HubInfo,
  LearnSlot,
  LedSettings,
  Modulation,
  ProtocolMatch,
  ReceiverState,
  Room,
  ScanOptions,
  ScanStatus,
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

const bands: [number, number][] = [[300, 348], [387, 464], [779, 928]];

function mockScanResult(opts: ScanOptions, base: Partial<ScanStatus>): ScanStatus {
  const remote = 433.92, spur = 390;
  const steps: number[] = [];
  for (let f = opts.fromMHz; f <= opts.toMHz + 1e-9; f = Math.round((f + 0.25) * 1000) / 1000) {
    if (bands.some(([lo, hi]) => f >= lo && f <= hi)) steps.push(f);
  }
  const noise = () => -101 + Math.round(Math.random() * 5);
  const spectrum = steps.map((f): [number, number, number] => {
    const quiet = Math.abs(f - spur) < 0.13 ? -52 : noise();
    const off = Math.abs(f - remote);
    const held = off < 0.4 ? Math.max(quiet, Math.round(-44 - off * 60)) : Math.max(quiet, noise());
    return [f, quiet, held];
  });
  const hasRemote = remote >= opts.fromMHz && remote <= opts.toMHz && bands.some(([lo, hi]) => remote >= lo && remote <= hi);
  const floor = -99;
  return {
    ...base, state: 'done', progress: 1, found: hasRemote, floorDbm: floor,
    ...(hasRemote ? { frequencyMHz: 433.915, peakDbm: -44, riseDb: 55 } : {}),
    candidates: hasRemote ? [{ frequencyMHz: 434, peakDbm: -48, riseDb: 51 }] : [],
    spectrum,
  };
}

/** The derived fields the hub adds: address, copyable, pairCode. */
function annotate(f: Fan): Fan {
  const address = f.address ?? f.protocolLabel.match(/0x[0-9A-F]+/i)?.[0] ?? null;
  return { ...f, address, copyable: !!f.protocol && !!address, pairCode: f.pairCode ?? false };
}

export class MockHub implements HubApi {
  private state = load();
  private captureList = structuredClone(seedCaptures);
  private nextCaptureId = 15;
  private listening = true;
  private modulation: Modulation = 'OOK';
  private frequencyMHz = 433.92;
  private minRssiDbm = -80;
  private scanning = false;
  private captureSubs = new Set<(c: Capture) => void>();
  private captureTimer: ReturnType<typeof setInterval> | null = null;
  private info: HubInfo = {
    name: 'FanWave‑3F2A', hardware: 'ESP32‑S3 · CC1101', host: 'fanwave.local', online: true,
    uptimeSeconds: 6 * 86400 + 4 * 3600, wifiRssi: -58, heapFreeBytes: 212 * 1024,
    // Like a fresh hub, start off any network until this browser has been through setup.
    wifiNetwork: read('fanwave.setupDone') === '1' ? 'Maison‑5G' : null, accessPointFallback: true, localOnly: true, radioOk: true,
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
    return structuredClone(this.state.fans.map(annotate));
  }

  private fanRemovedSubs = new Set<(id: string) => void>();

  async duplicateFan(fanId: string, copy: { name: string; roomId: string; address?: string }) {
    await wait(200);
    const src = this.state.fans.find((f) => f.id === fanId);
    if (!src) throw new Error('Unknown fan.');
    if (!annotate(src).copyable) throw new Error('Only remotes whose buttons all decode, from one remote, can be copied.');
    const norm = (a: string) => '0x' + parseInt(a.replace(/^0x/i, ''), 16).toString(16).toUpperCase().padStart(5, '0');
    const taken = new Set(this.state.fans.flatMap((f) => { const a = annotate(f).address; return a ? [norm(a)] : []; }));
    let address: string;
    if (copy.address) {
      const v = parseInt(copy.address.replace(/^0x/i, ''), 16);
      if (!/^(0x)?[0-9a-f]+$/i.test(copy.address) || v > 0xfffff) throw new Error('The address must be hex, at most 20 bits (0x0 to 0xFFFFF).');
      address = '0x' + v.toString(16).toUpperCase().padStart(5, '0');
      if (taken.has(address)) throw new Error('Another fan already uses that address.');
    } else {
      do address = '0x' + Math.floor(1 + Math.random() * 0xffffe).toString(16).toUpperCase().padStart(5, '0');
      while (taken.has(address));
    }
    const base = copy.name.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '') || 'fan';
    let id = base;
    for (let n = 2; this.state.fans.some((f) => f.id === id); n++) id = `${base}-${n}`;
    const fan: Fan = {
      ...structuredClone(src), id, name: copy.name, roomId: copy.roomId, address, copyOf: src.name,
      protocolLabel: `${(src.protocol ?? '').toUpperCase()} · ${address}`,
      commanded: { ...off }, confirmed: null, lastSentAt: null,
    };
    this.state.fans.push(fan);
    this.save();
    return structuredClone(annotate(fan));
  }

  async pairFan(fanId: string) {
    await wait(1200);
    const fan = this.state.fans.find((f) => f.id === fanId);
    if (!fan) throw new Error('Unknown fan.');
    return { sent: fan.pairCode ? 'pair' : 'power' };
  }

  async removeFan(fanId: string) {
    await wait(150);
    if (!this.state.fans.some((f) => f.id === fanId)) throw new Error('Unknown fan.');
    this.state.fans = this.state.fans.filter((f) => f.id !== fanId);
    this.save();
    for (const cb of this.fanRemovedSubs) cb(fanId);
  }

  onFanRemoved(cb: (id: string) => void) {
    this.fanRemovedSubs.add(cb);
    return () => this.fanRemovedSubs.delete(cb);
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

  private receiverState(): ReceiverState {
    return {
      listening: this.listening, modulation: this.modulation, frequencyMHz: this.frequencyMHz,
      minRssiDbm: this.minRssiDbm, scanning: this.scanning,
      edgesPerSecond: this.listening && !this.scanning ? 30000 + Math.round(Math.random() * 8000) : 0,
    };
  }

  async receiver(): Promise<ReceiverState> {
    await wait(40);
    return this.receiverState();
  }

  async setReceiver(opts: Partial<ReceiverState>) {
    await wait(60);
    const radio = (opts.listening !== undefined && opts.listening !== this.listening) ||
      (opts.modulation !== undefined && opts.modulation !== this.modulation) ||
      (opts.frequencyMHz !== undefined && opts.frequencyMHz !== this.frequencyMHz);
    if (this.scanning && radio) throw new Error('A frequency scan is running; try again when it finishes.');
    if (opts.minRssiDbm !== undefined) {
      if (opts.minRssiDbm < -110 || opts.minRssiDbm > -30) throw new Error('The noise threshold must be between -110 and -30 dBm.');
      this.minRssiDbm = Math.round(opts.minRssiDbm);
    }
    this.listening = opts.listening ?? this.listening;
    this.modulation = opts.modulation ?? this.modulation;
    this.frequencyMHz = opts.frequencyMHz ?? this.frequencyMHz;
    this.syncCaptureTimer();
    return this.receiverState();
  }

  onFiltered(cb: (r: FilteredReport) => void) {
    // A little noise just under the threshold every few seconds, more when it's set low.
    const timer = setInterval(() => {
      if (!this.listening || this.scanning) return;
      const count = Math.max(0, Math.round((this.minRssiDbm + 112) / 6 + Math.random() * 3 - 1));
      if (count) cb({ count, strongestDbm: this.minRssiDbm - 1 - Math.round(Math.random() * 6), thresholdDbm: this.minRssiDbm, periodMs: 2000 });
    }, 2000);
    return () => clearInterval(timer);
  }

  scanFrequency(opts: ScanOptions, onProgress: (s: ScanStatus) => void, signal: AbortSignal) {
    // A remote at 433.92 MHz that only shows while held, and a crystal spur at
    // 390 MHz that's loud in both passes: what the real scan has to tell apart.
    const quietMs = (opts.quietSeconds ?? 3) * 1000;
    const holdMs = (opts.holdSeconds ?? 9) * 1000;
    const base = { fromMHz: opts.fromMHz, toMHz: opts.toMHz, stepKHz: 250 };
    if (this.scanning) return Promise.reject(new Error('A scan is already running.'));
    this.scanning = true;
    return new Promise<ScanStatus>((resolve) => {
      const start = Date.now();
      const finish = (s: ScanStatus) => {
        clearInterval(timer);
        this.scanning = false;
        onProgress(s);
        resolve(s);
      };
      signal.addEventListener('abort', () => finish({ ...base, state: 'cancelled' }));
      const timer = setInterval(() => {
        const t = Date.now() - start;
        const progress = Math.min(t / (quietMs + holdMs), 0.99);
        if (t < quietMs) onProgress({ ...base, state: 'quiet', progress });
        else if (t < quietMs + holdMs * 0.75) onProgress({ ...base, state: 'hold', progress });
        else if (t < quietMs + holdMs) onProgress({ ...base, state: 'refine', progress, candidateMHz: 434 });
        else finish(mockScanResult(opts, base));
      }, 400);
    });
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
