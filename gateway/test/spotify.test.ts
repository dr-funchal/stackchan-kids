import assert from 'node:assert/strict';
import { randomBytes } from 'node:crypto';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { after, before, test } from 'node:test';
import { silentLogger } from '../src/logger.ts';
import { SpotifyService } from '../src/services/spotify.ts';
import { defaultSettings } from '../src/settings.ts';
import type { Settings } from '../src/settings.ts';
import { JsonFile, SecretBox, SecretStore } from '../src/store.ts';

let dir: string;
before(async () => (dir = await mkdtemp(join(tmpdir(), 'gw-spotify-'))));
after(() => rm(dir, { recursive: true, force: true }));

interface Call {
  method: string;
  url: URL;
  body: unknown;
}

/** A tiny fake of accounts.spotify.com + api.spotify.com. */
function fakeSpotify() {
  const calls: Call[] = [];
  let accessCount = 0;
  let expireNext = false;
  const devices = [
    { id: 'd1', name: 'Echo da Sala', type: 'Speaker', is_active: false, is_restricted: false, volume_percent: 95 },
    { id: 'd2', name: 'Celular da Mãe', type: 'Smartphone', is_active: true, is_restricted: false, volume_percent: 40 },
  ];
  const tracks = [
    { uri: 'spotify:track:explicit1', name: 'Rap Pesado', explicit: true, duration_ms: 1, artists: [{ name: 'X' }] },
    { uri: 'spotify:track:kid1', name: 'Galinha Pintadinha', explicit: false, duration_ms: 1, artists: [{ name: 'GP' }] },
    { uri: 'spotify:track:kid2', name: 'Pintinho Amarelinho', explicit: false, duration_ms: 1, artists: [{ name: 'GP' }] },
  ];
  const json = (status: number, body?: unknown) =>
    new Response(body === undefined ? null : JSON.stringify(body), { status, headers: { 'Content-Type': 'application/json' } });
  const impl = (async (input: string | URL | Request, init?: RequestInit) => {
    const url = new URL(String(input));
    const method = init?.method ?? 'GET';
    let body: unknown = init?.body;
    if (typeof body === 'string' && body.startsWith('{')) body = JSON.parse(body);
    else if (body instanceof URLSearchParams) body = Object.fromEntries(body);
    calls.push({ method, url, body });
    if (url.hostname === 'accounts.spotify.com') {
      accessCount++;
      return json(200, { access_token: `at-${accessCount}`, expires_in: 3600, refresh_token: `rt-${accessCount}` });
    }
    const auth = String((init?.headers as Record<string, string>)?.Authorization ?? '');
    if (expireNext) {
      expireNext = false;
      return json(401, { error: { status: 401, message: 'The access token expired' } });
    }
    assert.match(auth, /^Bearer at-\d+$/);
    const p = url.pathname.replace('/v1', '');
    if (p === '/me/player/devices') return json(200, { devices });
    if (p === '/me/playlists') return json(200, { items: [{ id: 'pl1', uri: 'spotify:playlist:pl1', name: 'Músicas das Crianças' }] });
    if (p === '/playlists/pl1/items') return json(200, { items: [{ item: { explicit: false } }] });
    if (p === '/search') return json(200, { tracks: { items: tracks }, playlists: { items: [null] }, albums: { items: [] } });
    if (p.startsWith('/me/player')) return json(204);
    return json(404, { error: { message: `no route ${p}` } });
  }) as typeof fetch;
  return { impl, calls, devices, expire: () => (expireNext = true) };
}

async function setup() {
  const settings = new JsonFile<Settings>(join(dir, `${randomBytes(4).toString('hex')}.json`), defaultSettings);
  await settings.load();
  await settings.update((s) => (s.spotifyClientId = 'a'.repeat(32)));
  const secrets = new SecretStore(join(dir, `${randomBytes(4).toString('hex')}-s.json`), new SecretBox(randomBytes(32).toString('base64')));
  const fake = fakeSpotify();
  const spotify = new SpotifyService({ settings, secrets, publicUrl: 'https://m5.example.com', log: silentLogger, fetch: fake.impl });
  return { settings, secrets, fake, spotify };
}

async function connect(spotify: SpotifyService) {
  const url = new URL(spotify.authorizeUrl());
  await spotify.handleCallback('the-code', url.searchParams.get('state')!);
}

test('authorize URL uses PKCE S256, the exact redirect and only playback/playlist scopes', async () => {
  const { spotify } = await setup();
  const url = new URL(spotify.authorizeUrl());
  assert.equal(url.origin, 'https://accounts.spotify.com');
  assert.equal(url.searchParams.get('code_challenge_method'), 'S256');
  assert.equal(url.searchParams.get('redirect_uri'), 'https://m5.example.com/spotify/callback');
  assert.ok(!url.searchParams.has('client_secret'));
  assert.doesNotMatch(url.searchParams.get('scope')!, /modify-private|library-modify|user-read-email/);
});

test('callback state is single use; tokens are stored encrypted and refreshed on 401', async () => {
  const { spotify, fake, secrets } = await setup();
  const state = new URL(spotify.authorizeUrl()).searchParams.get('state')!;
  await spotify.handleCallback('c', state);
  await assert.rejects(spotify.handleCallback('c', state), /expired/);
  assert.equal(await spotify.connected(), true);
  assert.equal(await secrets.get('spotify.refresh_token'), 'rt-1');
  const exchange = fake.calls.find((c) => c.url.hostname === 'accounts.spotify.com')!;
  assert.equal((exchange.body as Record<string, string>).code_verifier!.length > 40, true);
  fake.expire();
  const devices = await spotify.devices();
  assert.equal(devices.length, 2);
  assert.equal(await secrets.get('spotify.refresh_token'), 'rt-2', 'rotated refresh token kept');
});

test('kid mode never queues explicit tracks; volume is capped on the target device', async () => {
  const { spotify, fake, settings } = await setup();
  await connect(spotify);
  await settings.update((s) => {
    s.kidMode = true;
    s.spotifyMaxVolume = 60;
  });
  const r = await spotify.playQuery('galinha pintadinha', 'sala');
  assert.equal(r.device, 'Echo da Sala');
  const play = fake.calls.find((c) => c.method === 'PUT' && c.url.pathname === '/v1/me/player/play')!;
  assert.equal(play.url.searchParams.get('device_id'), 'd1');
  assert.deepEqual((play.body as { uris: string[] }).uris, ['spotify:track:kid1', 'spotify:track:kid2']);
  const vol = fake.calls.find((c) => c.url.pathname === '/v1/me/player/volume')!;
  assert.equal(vol.url.searchParams.get('volume_percent'), '60');
});

test('without kid mode explicit tracks are allowed', async () => {
  const { spotify, fake, settings } = await setup();
  await connect(spotify);
  await settings.update((s) => (s.kidMode = false));
  await spotify.playQuery('rap');
  const play = fake.calls.find((c) => c.url.pathname === '/v1/me/player/play')!;
  assert.equal((play.body as { uris: string[] }).uris[0], 'spotify:track:explicit1');
  assert.equal(play.url.searchParams.get('device_id'), 'd2', 'active device when none named');
});

test('"playlist" requests prefer the family playlists; default device from settings', async () => {
  const { spotify, fake, settings } = await setup();
  await connect(spotify);
  await settings.update((s) => (s.spotifyDefaultDevice = 'echo'));
  const r = await spotify.playQuery('a playlist das crianças');
  assert.equal(r.kind, 'playlist');
  assert.equal(r.device, 'Echo da Sala');
  const play = fake.calls.find((c) => c.url.pathname === '/v1/me/player/play')!;
  assert.equal((play.body as { context_uri: string }).context_uri, 'spotify:playlist:pl1');
});

test('volume requests above the parental maximum are clamped', async () => {
  const { spotify, settings } = await setup();
  await connect(spotify);
  await settings.update((s) => (s.spotifyMaxVolume = 50));
  assert.equal(await spotify.setVolume(100), 50);
  assert.equal(await spotify.setVolume(-5), 0);
});

test('unknown device names are reported with the list of available ones', async () => {
  const { spotify } = await setup();
  await connect(spotify);
  await assert.rejects(spotify.playQuery('x', 'geladeira'), /Echo da Sala/);
});
