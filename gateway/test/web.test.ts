import assert from 'node:assert/strict';
import { randomBytes } from 'node:crypto';
import { mkdir, mkdtemp, rm, writeFile } from 'node:fs/promises';
import type { Server } from 'node:http';
import type { AddressInfo } from 'node:net';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { after, before, test } from 'node:test';
import { ActivityFeed } from '../src/activity.ts';
import { loadConfig } from '../src/config.ts';
import { silentLogger } from '../src/logger.ts';
import { MODULES } from '../src/modules/index.ts';
import { ToolRegistry } from '../src/registry/registry.ts';
import { ExternalMcp } from '../src/services/external-mcp.ts';
import { MusicLibrary } from '../src/services/music-library.ts';
import { SpotifyService } from '../src/services/spotify.ts';
import { WeatherService } from '../src/services/weather.ts';
import { defaultSettings } from '../src/settings.ts';
import type { Settings } from '../src/settings.ts';
import { JsonFile, SecretBox, SecretStore } from '../src/store.ts';
import { registerApi, sniffAudio } from '../src/web/api.ts';
import { Auth, hashPassword } from '../src/web/auth.ts';
import type { AuthState } from '../src/web/auth.ts';
import { Router, startWebServer } from '../src/web/server.ts';

const ORIGIN = 'https://m5.example.com';
let dir: string;
let server: Server;
let base: string;
let music: MusicLibrary;

before(async () => {
  dir = await mkdtemp(join(tmpdir(), 'gw-web-'));
  await mkdir(join(dir, 'historias'));
  await writeFile(join(dir, 'historias', 'dino.txt'), '# O Dinossauro\n\nEra uma vez um dinossauro. Fim.');
  const cfg = loadConfig({ DATA_DIR: dir, PUBLIC_URL: ORIGIN, ADMIN_EMAIL: 'pai@example.com' });
  const settings = new JsonFile<Settings>(join(dir, 'state', 'settings.json'), defaultSettings);
  await settings.load();
  const secrets = new SecretStore(join(dir, 'state', 'secrets.json'), new SecretBox(randomBytes(32).toString('base64')));
  const authFile = new JsonFile<AuthState>(join(dir, 'state', 'auth.json'), () => ({ sessions: {} }));
  await authFile.load();
  const auth = new Auth(authFile, { email: cfg.adminEmail, passwordHash: await hashPassword('senha-certa-123') });
  const activity = new ActivityFeed(join(dir, 'state', 'activity.json'));
  await activity.load();
  music = new MusicLibrary(dir, silentLogger);
  await music.load();
  const spotify = new SpotifyService({ settings, secrets, publicUrl: ORIGIN, log: silentLogger });
  const weather = new WeatherService(settings);
  const registry = new ToolRegistry({ log: silentLogger, defaultTimeoutMs: 2000, allowRestricted: false });
  for (const m of MODULES) registry.register(m.build({ dataDir: dir, timezone: cfg.timezone, settings, music, spotify, weather }));
  const external = new ExternalMcp({ settings, secrets, registry, log: silentLogger });
  const router = new Router();
  registerApi(router, {
    cfg, log: silentLogger, auth, settings, activity, registry, music, spotify, weather, external,
    connection: () => ({ state: 'connected', since: new Date().toISOString() }),
    applyPolicy: () => {},
    startedAt: Date.now(),
  });
  server = await startWebServer({
    port: 0,
    log: silentLogger,
    router,
    publicDir: fileURLToPath(new URL('../src/web/public/', import.meta.url)),
    version: 'test',
    allowedOrigins: [ORIGIN],
    checkSession: (t) => auth.check(t),
  });
  base = `http://127.0.0.1:${(server.address() as AddressInfo).port}`;
});

after(async () => {
  server.close();
  await rm(dir, { recursive: true, force: true });
});

let cookie = '';
const req = (method: string, path: string, body?: unknown, headers: Record<string, string> = {}) =>
  fetch(base + path, {
    method,
    headers: {
      ...(body !== undefined ? { 'Content-Type': 'application/json' } : {}),
      ...(method !== 'GET' ? { Origin: ORIGIN } : {}),
      ...(cookie ? { Cookie: cookie } : {}),
      ...headers,
    },
    body: body === undefined ? undefined : typeof body === 'string' ? body : JSON.stringify(body),
    redirect: 'manual',
  });

test('panel shell is public, with a strict CSP and anti-framing headers', async () => {
  const res = await fetch(`${base}/`);
  assert.equal(res.status, 200);
  assert.match(res.headers.get('content-security-policy')!, /script-src 'self'/);
  assert.doesNotMatch(res.headers.get('content-security-policy')!, /unsafe-inline/);
  assert.equal(res.headers.get('x-frame-options'), 'DENY');
  assert.match(await res.text(), /app\.js\?v=test/);
  assert.equal((await fetch(`${base}/app.js`)).status, 200);
  assert.equal((await fetch(`${base}/../../etc/passwd`)).status, 404);
  assert.equal((await fetch(`${base}/health`)).status, 200);
});

test('API needs a session; login checks origin and password and sets a strict cookie', async () => {
  assert.equal((await req('GET', '/api/status')).status, 401);
  assert.equal((await req('POST', '/api/login', { email: 'pai@example.com', password: 'senha-certa-123' }, { Origin: 'https://evil.example' })).status, 403);
  assert.equal((await req('POST', '/api/login', { email: 'pai@example.com', password: 'errada' })).status, 401);
  const res = await req('POST', '/api/login', { email: 'pai@example.com', password: 'senha-certa-123' });
  assert.equal(res.status, 200);
  const set = res.headers.get('set-cookie')!;
  assert.match(set, /^__Host-stackchan=/);
  for (const flag of ['HttpOnly', 'Secure', 'SameSite=Strict', 'Path=/']) assert.ok(set.includes(flag), flag);
  cookie = set.split(';')[0]!;
  const status = await (await req('GET', '/api/status')).json();
  assert.equal(status.stories, 1);
  assert.equal(status.tools.exposed, 12);
});

test('state changes without the panel origin are refused (CSRF)', async () => {
  const res = await fetch(`${base}/api/stories`, {
    method: 'POST',
    headers: { Cookie: cookie, 'Content-Type': 'application/json' },
    body: JSON.stringify({ title: 'x', text: 'y' }),
  });
  assert.equal(res.status, 403);
});

test('stories CRUD through the API', async () => {
  const created = await req('POST', '/api/stories', { title: 'A Girafa', text: 'Era uma vez uma girafa. Fim.' });
  assert.equal(created.status, 201);
  const { id } = await created.json();
  const list = await (await req('GET', '/api/stories')).json();
  assert.ok(list.stories.some((s: { id: string }) => s.id === id));
  assert.equal((await req('PUT', `/api/stories/${id}`, { title: 'A Girafa Curiosa', text: 'Outra versão. Fim.' })).status, 200);
  const one = await (await req('GET', `/api/stories/${id}`)).json();
  assert.equal(one.title, 'A Girafa Curiosa');
  assert.equal((await req('DELETE', `/api/stories/${id}`)).status, 200);
  assert.equal((await req('GET', `/api/stories/${id}`)).status, 404);
  assert.equal((await req('POST', '/api/stories', { title: '', text: 'x' })).status, 400);
});

test('uploads that are not audio are refused before reaching ffmpeg', async () => {
  const playlist = '#EXTM3U\n#EXTINF:1,\nfile:///etc/passwd\n'.repeat(60);
  const res = await req('POST', '/api/music/upload?title=x', playlist, { 'Content-Type': 'application/octet-stream' });
  assert.equal(res.status, 415);
  assert.equal(sniffAudio(Buffer.from('ID3\x04\0\0\0\0\0\0\0\0\0\0\0\0', 'latin1')), 'mp3');
  assert.equal(sniffAudio(Buffer.from('\0\0\0\x20ftypM4A \0\0\0\0', 'latin1')), 'mp4');
  assert.equal(sniffAudio(Buffer.from('<html><body>\0\0\0\0', 'latin1')), undefined);
});

test('robot audio links are one-time capabilities', async () => {
  // a fake converted track, registered the way the library stores it
  await writeFile(join(dir, 'musicas', 'galinha-abc.ogg'), Buffer.from('OggS fake'));
  await writeFile(
    join(dir, 'musicas', 'library.json'),
    JSON.stringify({ tracks: [{ id: 'galinha-abc', title: 'Galinha', artist: '', seconds: 1, bytes: 9, addedAt: '' }] }),
  );
  const fresh = new MusicLibrary(dir, silentLogger);
  await fresh.load();
  assert.equal(fresh.find('toca a galinha')?.id, 'galinha-abc');
  const code = fresh.createCode('galinha-abc', 1000);
  assert.ok(/^[A-Za-z0-9_-]{20}$/.test(code));
  assert.ok(fresh.resolveCode(code, 2000));
  assert.ok(fresh.resolveCode(code, 3000));
  assert.ok(fresh.resolveCode(code, 4000));
  assert.equal(fresh.resolveCode(code, 5000), undefined, 'max 3 uses');
  const late = fresh.createCode('galinha-abc', 0);
  assert.equal(fresh.resolveCode(late, 16 * 60_000), undefined, 'expires after 15 min');
  assert.equal((await req('GET', '/a/not-a-valid-code-at-all')).status, 404);
  assert.equal((await req('GET', `/a/${'x'.repeat(20)}`)).status, 404);
});

test('tools and settings round trip', async () => {
  const tools = await (await req('GET', '/api/tools')).json();
  assert.equal(tools.modules.length, 5);
  assert.equal((await req('PATCH', '/api/settings', { kidMode: false, spotifyMaxVolume: 500, musicDefaultTarget: 'robot' })).status, 200);
  const s = await (await req('GET', '/api/settings')).json();
  assert.equal(s.kidMode, false);
  assert.equal(s.spotifyMaxVolume, 100);
  assert.equal(s.musicDefaultTarget, 'robot');
  const sp = await (await req('PUT', '/api/spotify/config', { clientId: 'not-hex' })).json();
  assert.match(sp.error, /32/);
});

test('logout invalidates the session', async () => {
  assert.equal((await req('POST', '/api/logout', {})).status, 200);
  assert.equal((await req('GET', '/api/status')).status, 401);
});
