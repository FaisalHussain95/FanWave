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
  accessPointFallback: boolean;
  localOnly: boolean;
  radioOk: boolean;
  firmware: string;
  update: { version: string; sizeBytes: number } | null;
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
}

export interface LearnSlot {
  key: string;
  label: string;
  capability: Capability;
  /** Code as shown to the user, e.g. "0x3A5C1 · 0110". */
  code: string | null;
  status: 'todo' | 'listening' | 'done' | 'skipped';
}

/** Everything the hub persists, as written by Backup and read by Restore. */
export interface Backup {
  version: 1;
  rooms: Room[];
  fans: Fan[];
}
