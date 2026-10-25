import type {
  Backup,
  Capture,
  Fan,
  FanState,
  FilteredReport,
  HubInfo,
  LearnSlot,
  LedSettings,
  ProtocolMatch,
  ReceiverState,
  Room,
  ScanOptions,
  ScanStatus,
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

  /** Called when the hub reports a fan changed, e.g. its remote was used. Returns an unsubscribe function. */
  onFan(cb: (fan: Fan) => void): () => void;

  scanWifi(): Promise<WifiNetwork[]>;
  /** Join a network. Resolves with the hub's new address on it, when known. */
  connectWifi(ssid: string, password: string): Promise<{ address: string | null }>;

  captures(): Promise<Capture[]>;
  receiver(): Promise<ReceiverState>;
  setReceiver(opts: Partial<ReceiverState>): Promise<ReceiverState>;
  /** Subscribe to new captures. Returns an unsubscribe function. */
  onCapture(cb: (capture: Capture) => void): () => void;
  analyze(captureId: number): Promise<ProtocolMatch[]>;
  /** Subscribe to summaries of the bursts the hub drops as noise. Returns an unsubscribe function. */
  onFiltered(cb: (report: FilteredReport) => void): () => void;
  /**
   * Find a remote's frequency: the hub measures the background, then asks for
   * a button to be held. onProgress sees every update; resolves once the scan
   * is done, failed or cancelled. Aborting the signal cancels it.
   */
  scanFrequency(opts: ScanOptions, onProgress: (status: ScanStatus) => void, signal: AbortSignal): Promise<ScanStatus>;

  /** Listen for a remote during pairing. Returns a stop function. */
  listenForRemote(cb: (signal: PairSignal) => void): () => void;
  /** Wait for the next press of a remote button and resolve with its code and capture. */
  learnButton(slot: LearnSlot, signal: AbortSignal): Promise<{ code: string; captureId: number | null }>;

  /** Change status LED settings. Resolves with the settings the hub stored. */
  setLed(patch: Partial<LedSettings>): Promise<LedSettings>;
  /** Blink the status LED for a few seconds so the user can find the board. */
  identify(): Promise<void>;

  installUpdate(onProgress: (fraction: number) => void): Promise<void>;
  /** Upload a firmware .bin; the hub installs it and restarts. Resolves with the new version. */
  uploadFirmware(file: File, onProgress: (fraction: number) => void): Promise<string>;
  backup(): Promise<Backup>;
  restore(backup: Backup): Promise<void>;
}
