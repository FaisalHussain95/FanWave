// Shapes shared by the UI and the hub API. They follow the fan model in
// docs/architecture.md.

export type Capability = 'power' | 'speed' | 'light' | 'reverse' | 'timer';

export type Modulation = 'OOK' | 'FSK';

export interface FanState {
  power: boolean;
  speed: number;
  light: boolean;
  reverse: boolean;
  timer: boolean;
}

export interface Fan {
  id: string;
  name: string;
  roomId: string;
  kind: string;
  /** Decoder id, or null when the fan replays raw captures. */
  protocol: string | null;
  /** Human label for the protocol line, e.g. "PT2262 · 0x3A5C1" or "Raw · 6 codes". */
  protocolLabel: string;
  frequencyMHz: number;
  modulation?: Modulation;
  speeds: number;
  capabilities: Capability[];
  /** What FanWave last sent. */
  commanded: FanState;
  /** What the hub last heard from a remote, if anything. Null means unknown. */
  confirmed: FanState | null;
  /** Epoch ms of the last command sent, or null if never. */
  lastSentAt: number | null;
}

export interface Room {
  id: string;
  name: string;
}

export interface HubInfo {
  name: string;
  hardware: string;
  host: string;
  online: boolean;
  uptimeSeconds: number;
  wifiRssi: number;
  heapFreeBytes: number;
  wifiNetwork: string | null;
  /** The hub is running its own setup access point. */
  setupMode?: boolean;
  accessPointFallback: boolean;
  localOnly: boolean;
  radioOk: boolean;
  led: LedSettings;
  firmware: string;
  update: { version: string; sizeBytes: number } | null;
}

/** The board's status LED. See "Status LED" in docs/architecture.md. */
export interface LedSettings {
  /** False turns the LED off except for errors. */
  enabled: boolean;
  /** 0–100. */
  brightness: number;
  /** What to show when online and idle. */
  idle: 'dim' | 'off';
  /** Flash on send and receive. */
  radioFlashes: boolean;
}

export interface WifiNetwork {
  ssid: string;
  rssi: number;
  secured: boolean;
}

export interface Capture {
  id: number;
  at: number;
  modulation: Modulation;
  rssi: number;
  frequencyMHz?: number;
  repeats: number;
  /** Decoded bits, when the pulse train decodes to a fixed-width frame. */
  bits: string | null;
  /** Number of raw pulses when the capture doesn't decode. */
  pulseCount: number;
  shortUs: number | null;
  longUs: number | null;
  syncMs: number | null;
  durationMs: number;
  label: string;
}

export interface FrameField {
  label: string;
  short: string;
  /** Relative width in the frame diagram. */
  weight: number;
  bits: string;
  value: string;
  kind: 'sync' | 'address' | 'command' | 'stop';
}

export interface ProtocolMatch {
  protocol: string;
  name: string;
  /** 0–1. */
  confidence: number;
  note: string;
  frame: FrameField[];
  /** Decoded fields, when the protocol has an address part. */
  address?: string;
  command?: string;
  bits?: string;
}

export interface ReceiverState {
  listening: boolean;
  modulation: Modulation;
  frequencyMHz: number;
  /** Undecoded bursts weaker than this (dBm) are dropped as noise. Saved on the hub. */
  minRssiDbm: number;
  /** A frequency scan has the radio: frequency, modulation and listening can't change. */
  scanning?: boolean;
}

/** Bursts the hub dropped as noise over the last periodMs. */
export interface FilteredReport {
  count: number;
  strongestDbm: number;
  thresholdDbm: number;
  periodMs: number;
}

export type ScanState = 'idle' | 'quiet' | 'hold' | 'refine' | 'done' | 'failed' | 'cancelled';

export interface ScanOptions {
  fromMHz: number;
  toMHz: number;
  quietSeconds?: number;
  holdSeconds?: number;
}

export interface ScanCandidate {
  frequencyMHz: number;
  peakDbm: number;
  /** How far it rose over the same frequency's background. */
  riseDb: number;
}

/** A frequency scan, as GET /api/receiver/scan and the "scan" event report it. See docs/api.md. */
export interface ScanStatus {
  state: ScanState;
  progress?: number;
  fromMHz?: number;
  toMHz?: number;
  stepKHz?: number;
  /** While refining: where it's narrowing down. */
  candidateMHz?: number;
  // Once done:
  found?: boolean;
  frequencyMHz?: number;
  peakDbm?: number;
  floorDbm?: number;
  riseDb?: number;
  candidates?: ScanCandidate[];
  /** [MHz, background dBm, while-holding dBm], at most 240 points. */
  spectrum?: [number, number, number][];
  error?: string;
}

export interface LearnSlot {
  key: string;
  label: string;
  capability: Capability;
  /** Code as shown to the user, e.g. "0x3A5C1 · 0110". */
  code: string | null;
  /** The capture the hub stores this button from. */
  captureId?: number | null;
  status: 'todo' | 'listening' | 'done' | 'skipped';
}

/** Everything the hub persists, as written by Backup and read by Restore. */
export interface Backup {
  version: 1;
  rooms: Room[];
  fans: Fan[];
}
