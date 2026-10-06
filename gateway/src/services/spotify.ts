import { createHash, randomBytes } from 'node:crypto';
import type { Logger } from '../logger.ts';
import type { SettingsFile } from '../settings.ts';
import type { SecretStore } from '../store.ts';
import { bestMatch, fold } from '../text.ts';

const AUTH_URL = 'https://accounts.spotify.com';
const API_URL = 'https://api.spotify.com/v1';
const SCOPES = [
  'user-read-playback-state',
  'user-modify-playback-state',
  'user-read-currently-playing',
  'playlist-read-private',
  'playlist-read-collaborative',
];
const REFRESH_SECRET = 'spotify.refresh_token';

export interface SpotifyDevice {
  id: string | null;
  name: string;
  type: string;
  is_active: boolean;
  is_restricted: boolean;
  volume_percent: number | null;
}

interface Artist {
  name: string;
}
interface Image {
  url: string;
  width?: number;
}
export interface SpotifyTrack {
  uri: string;
  name: string;
  explicit: boolean;
  duration_ms: number;
  artists: Artist[];
  album?: { name: string; images: Image[] };
}
interface Playlist {
  id: string;
  uri: string;
  name: string;
  owner?: { id: string; display_name?: string };
  images?: Image[] | null;
}
interface Album {
  id: string;
  uri: string;
  name: string;
  artists: Artist[];
  images?: Image[];
}

export interface Playback {
  is_playing: boolean;
  progress_ms: number | null;
  device?: SpotifyDevice;
  item?: SpotifyTrack | null;
  shuffle_state?: boolean;
}

export interface PlayResult {
  title: string;
  device: string;
  kind: 'track' | 'playlist' | 'album';
}

export class SpotifyError extends Error {
  status: number;
  constructor(message: string, status: number) {
    super(message);
    this.status = status;
  }
}

const trackLabel = (t: SpotifyTrack) => `${t.name} - ${t.artists.map((a) => a.name).join(', ')}`;

/**
 * Spotify Web API with Authorization Code + PKCE (no client secret on the server). Spotify only plays audio on
 * Spotify Connect devices of the account (phone, computer, Echo, TV...), never on third-party hardware like the robot.
 */
export class SpotifyService {
  #settings: SettingsFile;
  #secrets: SecretStore;
  #redirectUri: string;
  #log: Logger;
  #fetch: typeof fetch;
  #pending = new Map<string, { verifier: string; expires: number }>();
  #access: { token: string; expires: number } | undefined;
  #user: { id: string; name: string } | undefined;

  constructor(opts: {
    settings: SettingsFile;
    secrets: SecretStore;
    publicUrl: string;
    log: Logger;
    fetch?: typeof fetch;
  }) {
    this.#settings = opts.settings;
    this.#secrets = opts.secrets;
    this.#redirectUri = new URL('/spotify/callback', opts.publicUrl).toString();
    this.#log = opts.log;
    this.#fetch = opts.fetch ?? fetch;
  }

  get redirectUri(): string {
    return this.#redirectUri;
  }

  get clientId(): string {
    return this.#settings.get().spotifyClientId;
  }

  async connected(): Promise<boolean> {
    return Boolean(this.clientId && (await this.#secrets.get(REFRESH_SECRET)));
  }

  /* ------------------------------- OAuth (PKCE) ------------------------------ */

  authorizeUrl(now = Date.now()): string {
    if (!this.clientId) throw new SpotifyError('Spotify Client ID is not configured', 400);
    for (const [s, p] of this.#pending) if (p.expires < now) this.#pending.delete(s);
    const verifier = randomBytes(48).toString('base64url');
    const state = randomBytes(18).toString('base64url');
    this.#pending.set(state, { verifier, expires: now + 10 * 60_000 });
    const url = new URL('/authorize', AUTH_URL);
    url.search = new URLSearchParams({
      response_type: 'code',
      client_id: this.clientId,
      scope: SCOPES.join(' '),
      redirect_uri: this.#redirectUri,
      state,
      code_challenge_method: 'S256',
      code_challenge: createHash('sha256').update(verifier).digest('base64url'),
    }).toString();
    return url.toString();
  }

  /** The state (random, single use, 10 min) is what ties the callback to a panel login. */
  async handleCallback(code: string, state: string): Promise<void> {
    const pending = this.#pending.get(state);
    this.#pending.delete(state);
    if (!pending || pending.expires < Date.now()) throw new SpotifyError('login expired, try again', 400);
    await this.#tokenRequest({
      grant_type: 'authorization_code',
      code,
      redirect_uri: this.#redirectUri,
      code_verifier: pending.verifier,
    });
    this.#user = undefined;
  }

  async disconnect(): Promise<void> {
    await this.#secrets.set(REFRESH_SECRET, undefined);
    this.#access = undefined;
    this.#user = undefined;
  }

  async #tokenRequest(params: Record<string, string>): Promise<void> {
    const res = await this.#fetch(`${AUTH_URL}/api/token`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams({ ...params, client_id: this.clientId }),
      signal: AbortSignal.timeout(10_000),
    });
    const body = (await res.json().catch(() => ({}))) as Record<string, unknown>;
    if (!res.ok) {
      if (body.error === 'invalid_grant') await this.disconnect();
      throw new SpotifyError(`Spotify login failed: ${String(body.error_description ?? body.error ?? res.status)}`, 401);
    }
    this.#access = { token: String(body.access_token), expires: Date.now() + (Number(body.expires_in) - 60) * 1000 };
    // PKCE refresh tokens rotate: always keep the newest one
    if (typeof body.refresh_token === 'string') await this.#secrets.set(REFRESH_SECRET, body.refresh_token);
  }

  async #token(force = false): Promise<string> {
    if (!force && this.#access && this.#access.expires > Date.now()) return this.#access.token;
    const refresh = await this.#secrets.get(REFRESH_SECRET);
    if (!this.clientId || !refresh) throw new SpotifyError('Spotify is not connected', 401);
    await this.#tokenRequest({ grant_type: 'refresh_token', refresh_token: refresh });
    return this.#access!.token;
  }

  /* ---------------------------------- Web API --------------------------------- */

  async api<T>(method: string, path: string, opts: { query?: Record<string, string>; body?: unknown } = {}) {
    const url = new URL(API_URL + path);
    if (opts.query) url.search = new URLSearchParams(opts.query).toString();
    for (let attempt = 0; attempt < 2; attempt++) {
      const token = await this.#token(attempt > 0);
      const init: RequestInit = {
        method,
        headers: { Authorization: `Bearer ${token}`, 'Content-Type': 'application/json' },
        signal: AbortSignal.timeout(10_000),
      };
      if (opts.body !== undefined) init.body = JSON.stringify(opts.body);
      const res = await this.#fetch(url, init);
      if (res.status === 401 && attempt === 0) continue;
      if (res.status === 204 || res.status === 202) return undefined as T | undefined;
      const text = await res.text();
      const data = text ? (JSON.parse(text) as Record<string, unknown>) : undefined;
      if (!res.ok) {
        const err = data?.error as { message?: string; reason?: string } | undefined;
        const reason = err?.reason === 'PREMIUM_REQUIRED' ? 'Spotify Premium is required to control playback' : '';
        throw new SpotifyError(reason || err?.message || `Spotify error ${res.status}`, res.status);
      }
      return data as T | undefined;
    }
    throw new SpotifyError('Spotify authorization failed', 401);
  }

  async me(): Promise<{ id: string; name: string }> {
    if (!this.#user) {
      const me = await this.api<{ id: string; display_name?: string }>('GET', '/me');
      this.#user = { id: me!.id, name: me!.display_name || me!.id };
    }
    return this.#user;
  }

  async devices(): Promise<SpotifyDevice[]> {
    return (await this.api<{ devices: SpotifyDevice[] }>('GET', '/me/player/devices'))?.devices ?? [];
  }

  async playback(): Promise<Playback | undefined> {
    return this.api<Playback>('GET', '/me/player', { query: { additional_types: 'track' } });
  }

  async search(q: string, types = 'track,playlist,album') {
    // Since Feb 2026 the search limit is 10 per type for development-mode apps
    const data = await this.api<{
      tracks?: { items: (SpotifyTrack | null)[] };
      playlists?: { items: (Playlist | null)[] };
      albums?: { items: (Album | null)[] };
    }>('GET', '/search', { query: { q, type: types, limit: '10', market: 'from_token' } });
    return {
      tracks: (data?.tracks?.items ?? []).filter((t): t is SpotifyTrack => Boolean(t)),
      playlists: (data?.playlists?.items ?? []).filter((p): p is Playlist => Boolean(p)),
      albums: (data?.albums?.items ?? []).filter((a): a is Album => Boolean(a)),
    };
  }

  async myPlaylists(): Promise<Playlist[]> {
    return (await this.api<{ items: Playlist[] }>('GET', '/me/playlists', { query: { limit: '50' } }))?.items ?? [];
  }

  /** Device by (partial, accent-insensitive) name, else the default from settings, else the active one, else any. */
  async resolveDevice(name?: string): Promise<SpotifyDevice> {
    const devices = (await this.devices()).filter((d) => d.id && !d.is_restricted);
    if (devices.length === 0) {
      throw new SpotifyError('No Spotify device is available. Open Spotify on a phone, computer or speaker first.', 404);
    }
    const byName = (n: string) =>
      devices.find((d) => fold(d.name) === fold(n)) ?? bestMatch(devices, n, (d) => `${d.name} ${d.type}`);
    if (name) {
      const found = byName(name);
      if (!found) throw new SpotifyError(`No Spotify device called "${name}". Available: ${devices.map((d) => d.name).join(', ')}`, 404);
      return found;
    }
    const preferred = this.#settings.get().spotifyDefaultDevice;
    return (preferred && byName(preferred)) || devices.find((d) => d.is_active) || devices[0]!;
  }

  async #explicitIn(kind: 'playlist' | 'album', id: string): Promise<boolean> {
    const path = kind === 'album' ? `/albums/${id}/tracks` : `/playlists/${id}/items`;
    const data = await this.api<{ items: Record<string, unknown>[] }>('GET', path, { query: { limit: '50' } });
    return (data?.items ?? []).some((entry) => {
      // album tracks are the items themselves; playlist entries wrap them (`item`, formerly `track`)
      const track = (kind === 'album' ? entry : (entry.item ?? entry.track)) as { explicit?: boolean } | null;
      return Boolean(track?.explicit);
    });
  }

  /** "toca X": the user's own playlists first, then tracks (several, as a queue), honoring kid mode. */
  async playQuery(query: string, deviceName?: string): Promise<PlayResult> {
    const { kidMode } = this.#settings.get();
    const device = await this.resolveDevice(deviceName);
    const folded = fold(query);
    const wantsPlaylist = /\b(playlist|lista)\b/.test(folded);
    const wantsAlbum = /\b(album|disco)\b/.test(folded);
    const clean =
      folded.replace(/\b(a |o )?(playlist|lista|album|disco)( de| do| da| das| dos)?\b/g, ' ').replace(/\s+/g, ' ').trim() ||
      query;

    const mine = bestMatch(await this.myPlaylists(), clean, (p) => p.name);
    if (mine && (wantsPlaylist || fold(mine.name).includes(fold(clean)))) {
      if (!(kidMode && (await this.#explicitIn('playlist', mine.id)))) {
        await this.#play(device, { context_uri: mine.uri });
        return { title: mine.name, device: device.name, kind: 'playlist' };
      }
    }

    const found = await this.search(clean);
    if (wantsAlbum || wantsPlaylist) {
      const pool = wantsAlbum ? found.albums : found.playlists;
      for (const item of pool.slice(0, 3)) {
        if (kidMode && (await this.#explicitIn(wantsAlbum ? 'album' : 'playlist', item.id))) continue;
        await this.#play(device, { context_uri: item.uri });
        return { title: item.name, device: device.name, kind: wantsAlbum ? 'album' : 'playlist' };
      }
    }

    const tracks = found.tracks.filter((t) => !(kidMode && t.explicit));
    if (tracks.length === 0) {
      throw new SpotifyError(
        found.tracks.length ? 'Only explicit songs were found and kid mode is on.' : `Nothing found for "${query}".`,
        404,
      );
    }
    await this.#play(device, { uris: tracks.map((t) => t.uri) });
    return { title: trackLabel(tracks[0]!), device: device.name, kind: 'track' };
  }

  async playUri(uri: string, deviceId?: string): Promise<string> {
    const device = deviceId ? (await this.devices()).find((d) => d.id === deviceId) : await this.resolveDevice();
    if (!device) throw new SpotifyError('Device not found', 404);
    const [, kind, id] = uri.split(':');
    if (!kind || !id) throw new SpotifyError('Invalid Spotify URI', 400);
    if (this.#settings.get().kidMode) {
      if (kind === 'track') {
        const t = await this.api<SpotifyTrack>('GET', `/tracks/${id}`);
        if (t?.explicit) throw new SpotifyError('Explicit song blocked by kid mode', 403);
      } else if ((kind === 'album' || kind === 'playlist') && (await this.#explicitIn(kind, id))) {
        throw new SpotifyError('Contains explicit songs: blocked by kid mode', 403);
      }
    }
    await this.#play(device, kind === 'track' ? { uris: [uri] } : { context_uri: uri });
    return device.name;
  }

  async #play(device: SpotifyDevice, body: { uris?: string[]; context_uri?: string }): Promise<void> {
    await this.api('PUT', '/me/player/play', { query: { device_id: device.id! }, body });
    await this.#capVolume(device);
    this.#log.info('spotify play', { device: device.name, kind: body.context_uri ? 'context' : 'tracks' });
  }

  async #capVolume(device: SpotifyDevice): Promise<void> {
    const max = this.#settings.get().spotifyMaxVolume;
    if (device.volume_percent !== null && device.volume_percent > max) {
      await this.api('PUT', '/me/player/volume', { query: { volume_percent: String(max), device_id: device.id! } });
    }
  }

  async control(action: 'pause' | 'resume' | 'next' | 'previous', deviceName?: string): Promise<string> {
    const device = deviceName ? await this.resolveDevice(deviceName) : undefined;
    const query: Record<string, string> = device?.id ? { device_id: device.id } : {};
    if (action === 'pause') await this.api('PUT', '/me/player/pause', { query });
    else if (action === 'resume') await this.api('PUT', '/me/player/play', { query });
    else await this.api('POST', `/me/player/${action}`, { query });
    return device?.name ?? 'the active device';
  }

  async setVolume(percent: number, deviceName?: string): Promise<number> {
    const value = Math.max(0, Math.min(Math.round(percent), this.#settings.get().spotifyMaxVolume));
    const query: Record<string, string> = { volume_percent: String(value) };
    if (deviceName) query.device_id = (await this.resolveDevice(deviceName)).id!;
    await this.api('PUT', '/me/player/volume', { query });
    return value;
  }

  async transfer(deviceName: string, play = true): Promise<string> {
    const device = await this.resolveDevice(deviceName);
    await this.api('PUT', '/me/player', { body: { device_ids: [device.id], play } });
    await this.#capVolume(device);
    return device.name;
  }

  async transferTo(deviceId: string): Promise<string> {
    const device = (await this.devices()).find((d) => d.id === deviceId);
    if (!device) throw new SpotifyError('Device not found', 404);
    await this.api('PUT', '/me/player', { body: { device_ids: [deviceId], play: true } });
    await this.#capVolume(device);
    return device.name;
  }
}

export { trackLabel };
