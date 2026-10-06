import { randomBytes } from 'node:crypto';
import { createReadStream, createWriteStream } from 'node:fs';
import { open, rm, stat } from 'node:fs/promises';
import { join } from 'node:path';
import { pipeline } from 'node:stream/promises';
import { Transform } from 'node:stream';
import type { ActivityFeed } from '../activity.ts';
import type { Config } from '../config.ts';
import type { ConnectionInfo } from '../connection.ts';
import type { Logger } from '../logger.ts';
import { GATEWAY_VERSION } from '../mcp-server.ts';
import { MODULES } from '../modules/index.ts';
import { deleteStory, loadStories, saveStory, storiesDir, storyText } from '../modules/stories.ts';
import type { ToolRegistry } from '../registry/registry.ts';
import type { ExternalMcp } from '../services/external-mcp.ts';
import type { MusicLibrary } from '../services/music-library.ts';
import { SpotifyError } from '../services/spotify.ts';
import type { SpotifyService } from '../services/spotify.ts';
import type { WeatherService } from '../services/weather.ts';
import type { Settings, SettingsFile } from '../settings.ts';
import type { Auth } from './auth.ts';
import { MIN_PASSWORD_LENGTH } from './auth.ts';
import { HttpError, readJson, sendJson, setSessionCookie } from './server.ts';
import type { Ctx, Router } from './server.ts';

export interface ApiDeps {
  cfg: Config;
  log: Logger;
  auth: Auth;
  settings: SettingsFile;
  activity: ActivityFeed;
  registry: ToolRegistry;
  music: MusicLibrary;
  spotify: SpotifyService;
  weather: WeatherService;
  external: ExternalMcp;
  connection(): ConnectionInfo;
  applyPolicy(): void;
  startedAt: number;
}

interface Job {
  id: string;
  title: string;
  status: 'processing' | 'done' | 'error';
  error?: string;
  trackId?: string;
  startedAt: string;
}

const str = (v: unknown, max = 200): string => (typeof v === 'string' ? v.trim().slice(0, max) : '');

/** Accept only real audio containers: a text playlist (m3u8...) could make ffmpeg open other files. */
export function sniffAudio(head: Buffer): string | undefined {
  const ascii = (a: number, b: number) => head.toString('latin1', a, b);
  if (ascii(0, 3) === 'ID3') return 'mp3';
  if (ascii(0, 4) === 'OggS') return 'ogg';
  if (ascii(0, 4) === 'fLaC') return 'flac';
  if (ascii(0, 4) === 'RIFF' && ascii(8, 12) === 'WAVE') return 'wav';
  if (ascii(4, 8) === 'ftyp') return 'mp4';
  if (head.readUInt32BE(0) === 0x1a45dfa3) return 'webm';
  if (head[0] === 0xff && (head[1]! & 0xe0) === 0xe0) return 'mp3/aac'; // MPEG audio or ADTS frame sync
  return undefined;
}

function spotifyErrors(err: unknown): never {
  if (err instanceof SpotifyError) throw new HttpError(err.status >= 400 && err.status < 600 ? err.status : 502, err.message);
  throw err;
}

export function registerApi(router: Router, d: ApiDeps): { broadcast: Broadcast } {
  const jobs = new Map<string, Job>();
  const sDir = storiesDir(d.cfg.dataDir);
  const events = new Set<(event: string, data: unknown) => void>();
  const broadcast = (event: string, data: unknown) => {
    for (const send of events) send(event, data);
  };
  d.activity.subscribe((entry) => broadcast('activity', entry));

  /* ---------------------------------- Login --------------------------------- */

  router.on(
    'POST',
    '/api/login',
    async (c) => {
      const body = await readJson(c, 4096);
      const result = await d.auth.login(str(body.email), typeof body.password === 'string' ? body.password : '', {
        ip: c.ip,
        ua: String(c.req.headers['user-agent'] ?? ''),
      });
      if (!result.ok) {
        d.activity.add({ kind: 'auth', title: 'Tentativa de login recusada', detail: c.ip, ok: false });
        if (result.retryAfterSec) c.res.setHeader('Retry-After', String(result.retryAfterSec));
        return sendJson(c, result.retryAfterSec ? 429 : 401, {
          error: result.retryAfterSec ? 'too many attempts' : 'wrong e-mail or password',
          retryAfterSec: result.retryAfterSec,
        });
      }
      setSessionCookie(c, result.token);
      d.activity.add({ kind: 'auth', title: 'Login no painel', detail: c.ip, ok: true });
      sendJson(c, 200, { email: d.auth.email });
    },
    { auth: false },
  );

  router.on('GET', '/api/me', (c) => sendJson(c, 200, { email: d.auth.email, version: GATEWAY_VERSION }));

  router.on('POST', '/api/logout', async (c) => {
    await d.auth.logout(c.token);
    setSessionCookie(c, undefined);
    sendJson(c, 200, { ok: true });
  });

  router.on('GET', '/api/sessions', (c) => sendJson(c, 200, { sessions: d.auth.sessions(c.token) }));

  router.on('POST', '/api/sessions/logout-others', async (c) =>
    sendJson(c, 200, { removed: await d.auth.logoutOthers(c.token) }),
  );

  router.on('POST', '/api/settings/password', async (c) => {
    const body = await readJson(c, 4096);
    const r = await d.auth.changePassword(String(body.current ?? ''), String(body.next ?? ''), c.token);
    if (r === 'wrong') throw new HttpError(400, 'senha atual incorreta');
    if (r === 'weak') throw new HttpError(400, `a nova senha precisa de pelo menos ${MIN_PASSWORD_LENGTH} caracteres`);
    d.activity.add({ kind: 'auth', title: 'Senha do painel alterada', ok: true });
    sendJson(c, 200, { ok: true });
  });

  /* ------------------------------ Status & live ------------------------------ */

  const status = async () => {
    const lastRobot = d.activity.lastOf('tool') ?? d.activity.lastOf('robot');
    const stories = await loadStories(sDir);
    const s = d.settings.get();
    return {
      version: GATEWAY_VERSION,
      uptimeSec: Math.round((Date.now() - d.startedAt) / 1000),
      connection: d.connection(),
      endpointConfigured: Boolean(d.cfg.endpoint),
      robotLastSeen: lastRobot?.ts,
      tools: { exposed: d.registry.size, total: d.registry.catalog().length },
      stories: stories.length,
      music: d.music.list().length,
      spotify: { configured: Boolean(s.spotifyClientId), connected: await d.spotify.connected() },
      homeCity: s.home?.name ?? '',
      external: s.external.filter((e) => e.enabled).length,
    };
  };

  router.on('GET', '/api/status', async (c) => sendJson(c, 200, await status()));

  router.on('GET', '/api/events', (c) => {
    c.res.writeHead(200, {
      'Content-Type': 'text/event-stream; charset=utf-8',
      'Cache-Control': 'no-store',
      'X-Accel-Buffering': 'no',
    });
    const send = (event: string, data: unknown) => c.res.write(`event: ${event}\ndata: ${JSON.stringify(data)}\n\n`);
    send('hello', { version: GATEWAY_VERSION });
    events.add(send);
    const heartbeat = setInterval(() => c.res.write(': ping\n\n'), 15_000);
    c.req.on('close', () => {
      clearInterval(heartbeat);
      events.delete(send);
    });
  });

  router.on('GET', '/api/activity', (c) =>
    sendJson(c, 200, { entries: d.activity.list(Math.min(Number(c.query.get('limit')) || 100, 300)) }),
  );

  router.on('DELETE', '/api/activity', async (c) => {
    await d.activity.clear();
    sendJson(c, 200, { ok: true });
  });

  /* --------------------------------- Stories -------------------------------- */

  router.on('GET', '/api/stories', async (c) => {
    const stories = await loadStories(sDir);
    sendJson(c, 200, {
      stories: stories
        .map((s) => ({ id: s.id, title: s.title, pages: s.pages.length, bytes: s.bytes, updatedAt: s.updatedAt }))
        .sort((a, b) => a.title.localeCompare(b.title, 'pt-BR')),
    });
  });

  router.on('GET', '/api/stories/:id', async (c) => {
    const story = (await loadStories(sDir)).find((s) => s.id === c.params.id);
    if (!story) throw new HttpError(404, 'story not found');
    sendJson(c, 200, { id: story.id, title: story.title, pages: story.pages, text: await storyText(sDir, story.id) });
  });

  const saveStoryRoute = async (c: Ctx, id?: string) => {
    const body = await readJson(c, 300 * 1024);
    try {
      const saved = await saveStory(sDir, { ...(id ? { id } : {}), title: str(body.title, 120), text: String(body.text ?? '') });
      d.activity.add({ kind: 'system', title: `${id ? 'História editada' : 'Nova história'}: ${str(body.title, 120)}`, ok: true });
      sendJson(c, id ? 200 : 201, { id: saved });
    } catch (err) {
      throw new HttpError(400, err instanceof Error ? err.message : String(err));
    }
  };
  router.on('POST', '/api/stories', (c) => saveStoryRoute(c));
  router.on('PUT', '/api/stories/:id', (c) => saveStoryRoute(c, c.params.id));

  router.on('DELETE', '/api/stories/:id', async (c) => {
    if (!(await deleteStory(sDir, c.params.id!))) throw new HttpError(404, 'story not found');
    d.activity.add({ kind: 'system', title: 'História apagada', detail: c.params.id, ok: true });
    sendJson(c, 200, { ok: true });
  });

  /* ---------------------------------- Music --------------------------------- */

  router.on('GET', '/api/music', (c) =>
    sendJson(c, 200, { tracks: d.music.list(), jobs: [...jobs.values()].reverse() }),
  );

  router.on('POST', '/api/music/upload', async (c) => {
    const title = str(c.query.get('title'), 120);
    const artist = str(c.query.get('artist'), 120);
    if (!title) throw new HttpError(400, 'title is required');
    const max = d.cfg.maxUploadMb * 1024 * 1024;
    if (Number(c.req.headers['content-length'] ?? 0) > max) throw new HttpError(413, `file larger than ${d.cfg.maxUploadMb} MB`);
    const tmp = join(d.music.tmpDir, `upload-${randomBytes(8).toString('hex')}`);
    let size = 0;
    const limiter = new Transform({
      transform(chunk: Buffer, _e, done) {
        size += chunk.length;
        done(size > max ? new HttpError(413, `file larger than ${d.cfg.maxUploadMb} MB`) : null, chunk);
      },
    });
    try {
      await pipeline(c.req, limiter, createWriteStream(tmp, { mode: 0o600 }));
      const fh = await open(tmp, 'r');
      const head = Buffer.alloc(16);
      await fh.read(head, 0, 16, 0);
      await fh.close();
      if (size < 1024 || !sniffAudio(head)) throw new HttpError(415, 'unsupported file: send MP3, M4A, OGG, WAV, FLAC or WEBM');
    } catch (err) {
      await rm(tmp, { force: true });
      throw err;
    }
    const job: Job = { id: randomBytes(6).toString('hex'), title, status: 'processing', startedAt: new Date().toISOString() };
    jobs.set(job.id, job);
    for (const old of [...jobs.keys()].slice(0, Math.max(0, jobs.size - 20))) jobs.delete(old);
    broadcast('music', job);
    d.music
      .add(tmp, { title, artist })
      .then((track) => {
        Object.assign(job, { status: 'done', trackId: track.id });
        d.activity.add({ kind: 'system', title: `Música pronta para o robô: ${track.title}`, ok: true });
      })
      .catch((err: unknown) => {
        Object.assign(job, { status: 'error', error: String(err instanceof Error ? err.message : err).slice(0, 300) });
        d.activity.add({ kind: 'error', title: `Falha ao converter "${title}"`, detail: job.error, ok: false });
      })
      .finally(() => broadcast('music', job));
    sendJson(c, 202, { job });
  });

  router.on('PATCH', '/api/music/:id', async (c) => {
    const body = await readJson(c);
    const track = await d.music.update(c.params.id!, {
      ...(body.title !== undefined ? { title: str(body.title, 120) } : {}),
      ...(body.artist !== undefined ? { artist: str(body.artist, 120) } : {}),
    });
    if (!track) throw new HttpError(404, 'track not found');
    sendJson(c, 200, { track });
  });

  router.on('DELETE', '/api/music/:id', async (c) => {
    if (!(await d.music.remove(c.params.id!))) throw new HttpError(404, 'track not found');
    sendJson(c, 200, { ok: true });
  });

  const sendAudio = async (c: Ctx, path: string) => {
    const info = await stat(path).catch(() => undefined);
    if (!info) throw new HttpError(404, 'audio not found');
    c.res.writeHead(200, { 'Content-Type': 'audio/ogg', 'Content-Length': info.size, 'Cache-Control': 'no-store' });
    if (c.method === 'HEAD') return void c.res.end();
    await pipeline(createReadStream(path), c.res);
  };

  router.on('GET', '/api/music/:id/audio', async (c) => {
    if (!d.music.get(c.params.id!)) throw new HttpError(404, 'track not found');
    await sendAudio(c, d.music.path(c.params.id!));
  });

  // The robot's download: no session, the short-lived one-time code is the capability
  router.on(
    'GET',
    '/a/:code',
    async (c) => {
      const track = /^[A-Za-z0-9_-]{16,64}$/.test(c.params.code!) ? d.music.resolveCode(c.params.code!) : undefined;
      if (!track) throw new HttpError(404, 'expired or unknown link');
      d.activity.add({ kind: 'robot', title: `Robô baixou a música "${track.title}"`, ok: true });
      await sendAudio(c, d.music.path(track.id));
    },
    { auth: false },
  );

  /* --------------------------------- Spotify -------------------------------- */

  router.on('GET', '/api/spotify', async (c) => {
    const s = d.settings.get();
    const connected = await d.spotify.connected();
    const base = {
      configured: Boolean(s.spotifyClientId),
      clientId: s.spotifyClientId,
      redirectUri: d.spotify.redirectUri,
      connected,
      kidMode: s.kidMode,
      maxVolume: s.spotifyMaxVolume,
      defaultDevice: s.spotifyDefaultDevice,
    };
    if (!connected) return sendJson(c, 200, base);
    try {
      const [user, devices, playback] = await Promise.all([d.spotify.me(), d.spotify.devices(), d.spotify.playback()]);
      sendJson(c, 200, { ...base, user, devices, playback: playback ?? null });
    } catch (err) {
      sendJson(c, 200, { ...base, error: err instanceof Error ? err.message : String(err) });
    }
  });

  router.on('PUT', '/api/spotify/config', async (c) => {
    const body = await readJson(c);
    const clientId = str(body.clientId, 64);
    if (clientId && !/^[a-f0-9]{32}$/i.test(clientId)) throw new HttpError(400, 'o Client ID tem 32 caracteres (0-9, a-f)');
    if (clientId !== d.settings.get().spotifyClientId) await d.spotify.disconnect();
    await d.settings.update((s) => {
      s.spotifyClientId = clientId;
    });
    sendJson(c, 200, { ok: true });
  });

  router.on('POST', '/api/spotify/connect', (c) => {
    try {
      sendJson(c, 200, { url: d.spotify.authorizeUrl() });
    } catch (err) {
      spotifyErrors(err);
    }
  });

  // Spotify redirects the browser here; the cookie is not sent cross-site (SameSite=Strict), the one-time state is
  router.on(
    'GET',
    '/spotify/callback',
    async (c) => {
      const code = c.query.get('code');
      const state = c.query.get('state') ?? '';
      let result = 'ok';
      if (!code) result = c.query.get('error') ?? 'denied';
      else {
        try {
          await d.spotify.handleCallback(code, state);
          d.activity.add({ kind: 'system', title: 'Spotify conectado', ok: true });
        } catch (err) {
          result = err instanceof Error ? err.message : 'error';
          d.log.warn('spotify callback failed', { error: result });
        }
      }
      c.res.writeHead(302, { Location: `/#/spotify?result=${encodeURIComponent(result)}` });
      c.res.end();
    },
    { auth: false },
  );

  router.on('POST', '/api/spotify/disconnect', async (c) => {
    await d.spotify.disconnect();
    d.activity.add({ kind: 'system', title: 'Spotify desconectado', ok: true });
    sendJson(c, 200, { ok: true });
  });

  router.on('GET', '/api/spotify/search', async (c) => {
    const q = str(c.query.get('q'), 120);
    if (!q) throw new HttpError(400, 'q is required');
    try {
      const r = await d.spotify.search(q);
      const kid = d.settings.get().kidMode;
      sendJson(c, 200, {
        tracks: r.tracks.map((t) => ({
          uri: t.uri,
          name: t.name,
          artists: t.artists.map((a) => a.name).join(', '),
          explicit: t.explicit,
          blocked: kid && t.explicit,
          image: t.album?.images.at(-1)?.url ?? null,
          durationMs: t.duration_ms,
        })),
        playlists: r.playlists.map((p) => ({ uri: p.uri, name: p.name, owner: p.owner?.display_name ?? '', image: p.images?.[0]?.url ?? null })),
        albums: r.albums.map((a) => ({ uri: a.uri, name: a.name, artists: a.artists.map((x) => x.name).join(', '), image: a.images?.at(-1)?.url ?? null })),
      });
    } catch (err) {
      spotifyErrors(err);
    }
  });

  router.on('POST', '/api/spotify/play', async (c) => {
    const body = await readJson(c);
    try {
      const uri = str(body.uri, 100);
      if (!/^spotify:(track|album|playlist|artist):[A-Za-z0-9]+$/.test(uri)) throw new HttpError(400, 'invalid uri');
      const device = await d.spotify.playUri(uri, str(body.deviceId, 64) || undefined);
      d.activity.add({ kind: 'system', title: `Spotify pelo painel: tocando em ${device}`, ok: true });
      sendJson(c, 200, { device });
    } catch (err) {
      spotifyErrors(err);
    }
  });

  router.on('POST', '/api/spotify/control', async (c) => {
    const body = await readJson(c);
    const action = str(body.action, 10);
    if (!['pause', 'resume', 'next', 'previous'].includes(action)) throw new HttpError(400, 'invalid action');
    try {
      await d.spotify.control(action as 'pause');
      sendJson(c, 200, { ok: true });
    } catch (err) {
      spotifyErrors(err);
    }
  });

  router.on('POST', '/api/spotify/volume', async (c) => {
    const body = await readJson(c);
    try {
      sendJson(c, 200, { volume: await d.spotify.setVolume(Number(body.percent) || 0) });
    } catch (err) {
      spotifyErrors(err);
    }
  });

  router.on('POST', '/api/spotify/transfer', async (c) => {
    const body = await readJson(c);
    try {
      const device = await d.spotify.transferTo(str(body.deviceId, 64));
      d.activity.add({ kind: 'system', title: `Spotify levado para ${device}`, ok: true });
      sendJson(c, 200, { device });
    } catch (err) {
      spotifyErrors(err);
    }
  });

  /* ------------------------------ External MCPs ------------------------------ */

  router.on('GET', '/api/mcp', (c) => sendJson(c, 200, { servers: d.external.list() }));

  router.on('POST', '/api/mcp', async (c) => {
    const body = await readJson(c);
    const headers = Array.isArray(body.headers)
      ? (body.headers as unknown[]).slice(0, 5).map((h) => {
          const o = (h ?? {}) as Record<string, unknown>;
          return { name: str(o.name, 64), value: typeof o.value === 'string' ? o.value.slice(0, 2000) : '' };
        })
      : [];
    try {
      const server = await d.external.add({ name: str(body.name, 40), url: str(body.url, 500), headers });
      d.activity.add({ kind: 'system', title: `Servidor MCP conectado: ${server.name}`, ok: !server.lastError });
      sendJson(c, 201, { server });
    } catch (err) {
      throw new HttpError(400, err instanceof Error ? err.message : String(err));
    }
  });

  router.on('POST', '/api/mcp/:id/refresh', async (c) => {
    try {
      sendJson(c, 200, { server: await d.external.refresh(c.params.id!) });
    } catch (err) {
      throw new HttpError(404, err instanceof Error ? err.message : String(err));
    }
  });

  router.on('PATCH', '/api/mcp/:id', async (c) => {
    const body = await readJson(c);
    try {
      const server = await d.external.update(c.params.id!, {
        ...(typeof body.enabled === 'boolean' ? { enabled: body.enabled } : {}),
        ...(Array.isArray(body.enabledTools) ? { enabledTools: (body.enabledTools as unknown[]).map((t) => str(t, 100)) } : {}),
        ...(body.name !== undefined ? { name: str(body.name, 40) } : {}),
      });
      sendJson(c, 200, { server });
    } catch (err) {
      throw new HttpError(404, err instanceof Error ? err.message : String(err));
    }
  });

  router.on('DELETE', '/api/mcp/:id', async (c) => {
    await d.external.remove(c.params.id!);
    sendJson(c, 200, { ok: true });
  });

  /* ---------------------------------- Tools --------------------------------- */

  router.on('GET', '/api/tools', (c) => {
    const s = d.settings.get();
    sendJson(c, 200, {
      modules: MODULES.map((m) => ({
        name: m.name,
        title: m.title,
        description: m.description,
        enabled: s.modules[m.name] ?? d.cfg.modules.includes(m.name),
      })),
      tools: d.registry.catalog(),
      external: s.external.map((e) => ({ id: e.id, name: e.name })),
    });
  });

  router.on('PATCH', '/api/tools', async (c) => {
    const body = await readJson(c);
    await d.settings.update((s) => {
      if (body.modules && typeof body.modules === 'object') {
        for (const [name, on] of Object.entries(body.modules as Record<string, unknown>)) {
          if (MODULES.some((m) => m.name === name) && typeof on === 'boolean') s.modules[name] = on;
        }
      }
      if (Array.isArray(body.disabledTools)) s.disabledTools = (body.disabledTools as unknown[]).map((t) => str(t, 100));
    });
    d.applyPolicy();
    sendJson(c, 200, { ok: true });
  });

  /* -------------------------------- Settings -------------------------------- */

  const publicSettings = (s: Settings) => ({
    homeCity: s.homeCity,
    home: s.home ?? null,
    kidMode: s.kidMode,
    spotifyDefaultDevice: s.spotifyDefaultDevice,
    spotifyMaxVolume: s.spotifyMaxVolume,
    musicDefaultTarget: s.musicDefaultTarget,
    timezone: d.cfg.timezone,
    publicUrl: d.cfg.publicUrl,
    secretsAvailable: Boolean(d.cfg.secretsKey),
  });

  router.on('GET', '/api/settings', (c) => sendJson(c, 200, publicSettings(d.settings.get())));

  router.on('PATCH', '/api/settings', async (c) => {
    const body = await readJson(c);
    if (typeof body.homeCity === 'string') {
      try {
        await d.weather.setHome(str(body.homeCity, 80));
      } catch (err) {
        throw new HttpError(400, `cidade não encontrada: ${str(body.homeCity, 80)}`);
      }
    }
    await d.settings.update((s) => {
      if (typeof body.kidMode === 'boolean') s.kidMode = body.kidMode;
      if (typeof body.spotifyDefaultDevice === 'string') s.spotifyDefaultDevice = str(body.spotifyDefaultDevice, 80);
      if (body.spotifyMaxVolume !== undefined) {
        s.spotifyMaxVolume = Math.max(10, Math.min(100, Math.round(Number(body.spotifyMaxVolume) || 70)));
      }
      if (['auto', 'robot', 'spotify'].includes(String(body.musicDefaultTarget))) {
        s.musicDefaultTarget = body.musicDefaultTarget as Settings['musicDefaultTarget'];
      }
    });
    sendJson(c, 200, publicSettings(d.settings.get()));
  });

  router.on('GET', '/api/weather', async (c) => {
    if (!d.settings.get().home) return sendJson(c, 200, { configured: false });
    try {
      sendJson(c, 200, { configured: true, forecast: await d.weather.forecast() });
    } catch (err) {
      sendJson(c, 200, { configured: true, error: err instanceof Error ? err.message : String(err) });
    }
  });

  return { broadcast };
}

export type Broadcast = (event: string, data: unknown) => void;
