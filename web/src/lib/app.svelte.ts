import type { HubApi } from './api';
import { MockHub } from './mock';
import { read, write } from './storage';
import type { Fan, FanState, HubInfo, LedSettings, ProtocolMatch, Room } from './types';

export const api: HubApi = new MockHub();

const LAST_FAN = 'fanwave.lastFan';
const SETUP_DONE = 'fanwave.setupDone';

export interface PairDraft {
  protocol: string | null;
  protocolName: string | null;
  address: string | null;
  match: ProtocolMatch | null;
}

class AppState {
  hub = $state<HubInfo | null>(null);
  rooms = $state<Room[]>([]);
  fans = $state<Fan[]>([]);
  loaded = $state(false);
  loadError = $state<string | null>(null);
  toast = $state<string | null>(null);
  lastFanId = $state<string | null>(read(LAST_FAN));
  setupDone = $state(read(SETUP_DONE) === '1');
  /** Ticks so "sent 2 min ago" stays current. */
  now = $state(Date.now());
  pair = $state<PairDraft>({ protocol: null, protocolName: null, address: null, match: null });

  private toastTimer: ReturnType<typeof setTimeout> | null = null;

  constructor() {
    setInterval(() => (this.now = Date.now()), 15_000);
  }

  async load() {
    try {
      const [hub, rooms, fans] = await Promise.all([api.hub(), api.rooms(), api.fans()]);
      this.hub = hub;
      this.rooms = rooms;
      this.fans = fans;
      this.loadError = null;
    } catch (e) {
      this.loadError = e instanceof Error ? e.message : String(e);
    } finally {
      this.loaded = true;
    }
  }

  async refreshHub() {
    this.hub = await api.hub();
  }

  fan(id: string | null): Fan | undefined {
    return id ? this.fans.find((f) => f.id === id) : undefined;
  }

  get lastFan(): Fan | undefined {
    return this.fan(this.lastFanId) ?? this.fans[0];
  }

  room(id: string): Room | undefined {
    return this.rooms.find((r) => r.id === id);
  }

  useFan(id: string) {
    this.lastFanId = id;
    write(LAST_FAN, id);
  }

  finishSetup() {
    this.setupDone = true;
    write(SETUP_DONE, '1');
  }

  notify(message: string) {
    this.toast = message;
    if (this.toastTimer) clearTimeout(this.toastTimer);
    this.toastTimer = setTimeout(() => (this.toast = null), 3500);
  }

  /** Send a command, updating the screen straight away and undoing it if the hub refuses. */
  async command(fanId: string, patch: Partial<FanState>) {
    const fan = this.fan(fanId);
    if (!fan) return;
    const before = { commanded: fan.commanded, lastSentAt: fan.lastSentAt };
    fan.commanded = { ...fan.commanded, ...patch };
    fan.lastSentAt = Date.now();
    this.useFan(fanId);
    try {
      const updated = await api.command(fanId, patch);
      const i = this.fans.findIndex((f) => f.id === fanId);
      if (i >= 0) this.fans[i] = updated;
    } catch (e) {
      fan.commanded = before.commanded;
      fan.lastSentAt = before.lastSentAt;
      this.notify(`Couldn't reach ${fan.name}: ${e instanceof Error ? e.message : e}`);
    }
  }

  togglePower(fan: Fan) {
    return this.command(fan.id, { power: !fan.commanded.power });
  }

  /** Tapping the active speed turns the fan off, matching the design. */
  pickSpeed(fan: Fan, speed: number) {
    if (fan.commanded.power && fan.commanded.speed === speed) return this.command(fan.id, { power: false });
    return this.command(fan.id, { power: true, speed });
  }

  /** Change LED settings straight away, undoing it if the hub refuses. */
  async setLed(patch: Partial<LedSettings>) {
    const hub = this.hub;
    if (!hub) return;
    const before = hub.led;
    hub.led = { ...hub.led, ...patch };
    try {
      hub.led = await api.setLed(patch);
    } catch (e) {
      hub.led = before;
      this.notify(`Couldn't change the LED: ${e instanceof Error ? e.message : e}`);
    }
  }

  startPairing(draft: Partial<PairDraft> = {}) {
    this.pair = { protocol: null, protocolName: null, address: null, match: null, ...draft };
  }

  async addRoom(name: string) {
    const room = await api.addRoom(name);
    this.rooms.push(room);
    return room;
  }
}

export const app = new AppState();
