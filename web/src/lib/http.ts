// The real hub, over the API in docs/api.md. Used when the app is served by
// the hub (or proxied to one in development; see vite.config.ts).

import type { HubApi, NewFan, PairSignal } from './api';
import type {
  Backup,
  Capture,
  Fan,
  FanState,
  HubInfo,
  LearnSlot,
  LedSettings,
  ProtocolMatch,
  ReceiverState,
  Room,
  WifiNetwork,
} from './types';

type CaptureEvent = { type: 'capture'; now: number; capture: Capture; matches: ProtocolMatch[] };
type FanEvent = { type: 'fan'; fan: Fan };
type HubEvent = CaptureEvent | FanEvent;

export class HttpHub implements HubApi {
  /** Date.now() minus the hub's uptime clock, from the latest response. */
  private offset = 0;
  private socket: WebSocket | null = null;
  private retryMs = 1000;
  private listeners = new Set<(e: HubEvent) => void>();
  /** The last code learnButton returned, to skip the second burst of the same press. */
  private lastLearned: { key: string; at: number } | null = null;

  constructor(private base = '') {}

  // --- plumbing -------------------------------------------------------------

  private async request<T>(path: string, init: RequestInit = {}): Promise<T> {
    let res: Response;
    try {
      res = await fetch(this.base + path, init);
    } catch {
      throw new Error("Can't reach the hub. Is it powered and on this network?");
    }
    const now = Number(res.headers.get('X-Hub-Now'));
    if (now) this.offset = Date.now() - now;
    if (res.status === 204) return undefined as T;
    const body = await res.json().catch(() => null);
    if (!res.ok) throw new Error(body?.error ?? `The hub answered ${res.status}.`);
    return body as T;
  }

  private post<T>(path: string, body?: unknown): Promise<T> {
    return this.request<T>(path, {
      method: 'POST',
      headers: body === undefined ? {} : { 'Content-Type': 'application/json' },
      body: body === undefined ? undefined : JSON.stringify(body),
    });
  }

  private time(at: number | null): number | null {
    return at === null ? null : at + this.offset;
  }

  private fan(f: Fan): Fan {
    return { ...f, lastSentAt: this.time(f.lastSentAt) };
  }

  private capture(c: Capture): Capture {
    return { ...c, at: this.time(c.at) ?? Date.now() };
  }

  /** Subscribe to /api/events, opening the socket on first use and reconnecting when it drops. */
  private subscribe(cb: (e: HubEvent) => void): () => void {
    this.listeners.add(cb);
    this.connect();
    return () => {
      this.listeners.delete(cb);
      if (this.listeners.size === 0) {
        this.socket?.close();
        this.socket = null;
      }
    };
  }

  private connect() {
    if (this.socket || this.listeners.size === 0) return;
    const url = new URL(this.base + '/api/events', location.href);
    url.protocol = url.protocol === 'https:' ? 'wss:' : 'ws:';
    const ws = new WebSocket(url);
    this.socket = ws;
    ws.onopen = () => (this.retryMs = 1000);
    ws.onmessage = (msg) => {
      let e: HubEvent;
      try {
        e = JSON.parse(msg.data);
      } catch {
        return;
      }
      if (e.type === 'capture') this.offset = Date.now() - e.now;
      for (const l of this.listeners) l(e);
    };
    ws.onclose = () => {
      if (this.socket !== ws) return;
      this.socket = null;
      if (this.listeners.size === 0) return;
      setTimeout(() => this.connect(), this.retryMs);
      this.retryMs = Math.min(this.retryMs * 2, 10_000);
    };
  }

  private onCaptureEvent(cb: (e: CaptureEvent) => void) {
    return this.subscribe((e) => {
      if (e.type === 'capture') cb({ ...e, capture: this.capture(e.capture) });
    });
  }

  // --- HubApi ---------------------------------------------------------------

  async hub(): Promise<HubInfo> {
    const h = await this.request<HubInfo>('/api/hub');
    return { ...h, host: h.host || location.host };
  }

  rooms() {
    return this.request<Room[]>('/api/rooms');
  }

  addRoom(name: string) {
    return this.post<Room>('/api/rooms', { name });
  }

  async fans() {
    return (await this.request<Fan[]>('/api/fans')).map((f) => this.fan(f));
  }

  async command(fanId: string, patch: Partial<FanState>) {
    return this.fan(await this.post<Fan>(`/api/fans/${encodeURIComponent(fanId)}/command`, patch));
  }

  async addFan(fan: NewFan) {
    return this.fan(await this.post<Fan>('/api/fans', fan));
  }

  onFan(cb: (fan: Fan) => void) {
    return this.subscribe((e) => {
      if (e.type === 'fan') cb(this.fan(e.fan));
    });
  }

  scanWifi() {
    return this.request<WifiNetwork[]>('/api/wifi/scan');
  }

  async connectWifi(ssid: string, password: string) {
    const r = await this.post<{ address: string }>('/api/wifi', { ssid, password });
    return { address: r.address || null };
  }

  async captures() {
    const r = await this.request<{ now: number; captures: Capture[] }>('/api/captures');
    this.offset = Date.now() - r.now;
    return r.captures.map((c) => this.capture(c));
  }

  receiver() {
    return this.request<ReceiverState>('/api/receiver');
  }

  setReceiver(opts: Partial<ReceiverState>) {
    return this.post<ReceiverState>('/api/receiver', opts);
  }

  onCapture(cb: (capture: Capture) => void) {
    return this.onCaptureEvent((e) => cb(e.capture));
  }

  analyze(captureId: number) {
    return this.request<ProtocolMatch[]>(`/api/captures/${captureId}/analysis`);
  }

  listenForRemote(cb: (signal: PairSignal) => void) {
    let count = 0;
    let key: string | null = null;
    return this.onCaptureEvent(({ capture, matches }) => {
      const best = matches[0] ?? null;
      // Count repeats of the same code; a different code starts over.
      const k = best ? `${best.protocol}:${best.bits}` : 'raw';
      count = k === key ? count + 1 : 1;
      key = k;
      cb({ count, rssi: capture.rssi, match: best, address: best?.address ?? null });
    });
  }

  learnButton(_slot: LearnSlot, signal: AbortSignal) {
    return new Promise<{ code: string; captureId: number | null }>((resolve, reject) => {
      const stop = this.onCaptureEvent(({ capture, matches }) => {
        const m = matches[0];
        // One press can arrive as two bursts; don't learn it twice.
        const key = m ? `${m.protocol}:${m.bits}` : `raw:${capture.pulseCount}`;
        const prev = this.lastLearned;
        if (prev && prev.key === key && Date.now() - prev.at < 1500) {
          prev.at = Date.now();
          return;
        }
        this.lastLearned = { key, at: Date.now() };
        stop();
        const cmdBits = m?.frame.find((f) => f.kind === 'command')?.bits;
        const code = m
          ? `${m.address ?? m.name} · ${cmdBits ?? m.command ?? ''}`.trim()
          : `raw · ${capture.pulseCount} pulses`;
        resolve({ code, captureId: capture.id });
      });
      signal.addEventListener('abort', () => {
        stop();
        reject(new DOMException('Stopped listening', 'AbortError'));
      });
    });
  }

  setLed(patch: Partial<LedSettings>) {
    return this.post<LedSettings>('/api/led', patch);
  }

  async identify() {
    await this.post<void>('/api/identify');
  }

  async installUpdate() {
    throw new Error('The hub has no update server. Upload a firmware file instead.');
  }

  uploadFirmware(file: File, onProgress: (fraction: number) => void) {
    // XMLHttpRequest, because fetch() can't report upload progress.
    return new Promise<string>((resolve, reject) => {
      const xhr = new XMLHttpRequest();
      xhr.open('POST', this.base + '/api/ota');
      xhr.setRequestHeader('Content-Type', 'application/octet-stream');
      xhr.upload.onprogress = (e) => e.lengthComputable && onProgress(e.loaded / e.total);
      xhr.onload = () => {
        let body: { version?: string; error?: string } = {};
        try {
          body = JSON.parse(xhr.responseText);
        } catch {
          /* not JSON */
        }
        if (xhr.status === 200 && body.version) resolve(body.version);
        else reject(new Error(body.error ?? `The hub answered ${xhr.status}.`));
      };
      xhr.onerror = () => reject(new Error('The upload was interrupted.'));
      xhr.send(file);
    });
  }

  backup() {
    return this.request<Backup>('/api/backup');
  }

  async restore(backup: Backup) {
    await this.post<void>('/api/restore', backup);
  }
}
