import type {
  Backup,
  Capture,
  Fan,
  FanState,
  HubInfo,
  LearnSlot,
  Modulation,
  ProtocolMatch,
  Room,
  WifiNetwork,
} from './types';

/** A signal heard while pairing: how many times so far, and the best guess at its protocol. */
export interface PairSignal {
  count: number;
  rssi: number;
  match: ProtocolMatch | null;
  address: string | null;
}

export interface NewFan {
  name: string;
  roomId: string;
  kind: string;
  protocol: string | null;
  address: string | null;
  slots: LearnSlot[];
}

/**
 * Everything the UI needs from the hub. The firmware will serve this over
 * HTTP and a WebSocket; until then MockHub stands in for it.
 */
export interface HubApi {
  hub(): Promise<HubInfo>;
  rooms(): Promise<Room[]>;
  addRoom(name: string): Promise<Room>;
  fans(): Promise<Fan[]>;
  /** Transmit a command. Resolves with the fan's new commanded state. */
  command(fanId: string, patch: Partial<FanState>): Promise<Fan>;
  addFan(fan: NewFan): Promise<Fan>;

  scanWifi(): Promise<WifiNetwork[]>;
  connectWifi(ssid: string, password: string): Promise<void>;

  captures(): Promise<Capture[]>;
  setReceiver(opts: { listening: boolean; modulation: Modulation }): Promise<void>;
  /** Subscribe to new captures. Returns an unsubscribe function. */
  onCapture(cb: (capture: Capture) => void): () => void;
  analyze(captureId: number): Promise<ProtocolMatch[]>;

  /** Listen for a remote during pairing. Returns a stop function. */
  listenForRemote(cb: (signal: PairSignal) => void): () => void;
  /** Wait for the next press of a remote button and resolve with its code. */
  learnButton(slot: LearnSlot, signal: AbortSignal): Promise<string>;

  installUpdate(onProgress: (fraction: number) => void): Promise<void>;
  backup(): Promise<Backup>;
  restore(backup: Backup): Promise<void>;
}
