import assert from 'node:assert/strict';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { after, before, test } from 'node:test';
import { JsonFile } from '../src/store.ts';
import { Auth, hashPassword, verifyPassword } from '../src/web/auth.ts';
import type { AuthState } from '../src/web/auth.ts';

let dir: string;
before(async () => (dir = await mkdtemp(join(tmpdir(), 'gw-auth-'))));
after(() => rm(dir, { recursive: true, force: true }));

async function makeAuth(now = () => Date.now(), name = String(Math.random())) {
  const file = new JsonFile<AuthState>(join(dir, `${name}.json`), () => ({ sessions: {} }));
  await file.load();
  return new Auth(file, { email: 'Pai@Example.com', passwordHash: await hashPassword('senha-de-teste-1'), now });
}
const meta = (ip = '1.2.3.4') => ({ ip, ua: 'test' });

test('hash is shell- and compose-safe and verifies only the right password', async () => {
  const h = await hashPassword('abc12345');
  assert.ok(!h.includes('$'), 'no $ in the hash (would be expanded in .env)');
  assert.equal(h.split(':').length, 6);
  assert.equal(await verifyPassword('abc12345', h), true);
  assert.equal(await verifyPassword('abc12346', h), false);
  assert.equal(await verifyPassword('abc12345', 'garbage'), false);
});

test('login is case-insensitive on e-mail, issues a session, logout ends it', async () => {
  const auth = await makeAuth();
  const r = await auth.login('pai@example.COM ', 'senha-de-teste-1', meta());
  assert.equal(r.ok, true);
  const token = r.ok ? r.token : '';
  assert.equal(auth.check(token), true);
  assert.equal(auth.check(`${token}x`), false);
  await auth.logout(token);
  assert.equal(auth.check(token), false);
});

test('wrong e-mail and wrong password look the same, and 5 failures lock that address', async () => {
  const auth = await makeAuth();
  assert.deepEqual(await auth.login('outro@example.com', 'senha-de-teste-1', meta()), { ok: false });
  for (let i = 0; i < 3; i++) assert.equal((await auth.login('pai@example.com', 'errada', meta())).ok, false);
  const locked = await auth.login('pai@example.com', 'errada', meta());
  assert.equal(locked.ok, false);
  assert.ok(!locked.ok && locked.retryAfterSec && locked.retryAfterSec > 0);
  // even the right password is refused while locked
  const right = await auth.login('pai@example.com', 'senha-de-teste-1', meta());
  assert.equal(right.ok, false);
  // another address is not affected
  assert.equal((await auth.login('pai@example.com', 'senha-de-teste-1', meta('5.6.7.8'))).ok, true);
});

test('lock expires after 15 minutes', async () => {
  let t = 1_000_000;
  const auth = await makeAuth(() => t);
  for (let i = 0; i < 5; i++) await auth.login('pai@example.com', 'errada', meta());
  assert.equal((await auth.login('pai@example.com', 'senha-de-teste-1', meta())).ok, false);
  t += 15 * 60_000 + 1000;
  assert.equal((await auth.login('pai@example.com', 'senha-de-teste-1', meta())).ok, true);
});

test('changing the password requires the current one, enforces length and ends other sessions', async () => {
  const auth = await makeAuth();
  const a = await auth.login('pai@example.com', 'senha-de-teste-1', meta());
  const b = await auth.login('pai@example.com', 'senha-de-teste-1', meta('9.9.9.9'));
  const ta = a.ok ? a.token : '';
  const tb = b.ok ? b.token : '';
  assert.equal(await auth.changePassword('errada', 'nova-senha-longa', ta), 'wrong');
  assert.equal(await auth.changePassword('senha-de-teste-1', 'curta', ta), 'weak');
  assert.equal(await auth.changePassword('senha-de-teste-1', 'nova-senha-longa', ta), 'ok');
  assert.equal(auth.check(ta), true);
  assert.equal(auth.check(tb), false);
  assert.equal((await auth.login('pai@example.com', 'senha-de-teste-1', meta('7.7.7.7'))).ok, false);
  assert.equal((await auth.login('pai@example.com', 'nova-senha-longa', meta('7.7.7.7'))).ok, true);
});

test('sessions are stored only as hashes', async () => {
  const name = 'hashed';
  const auth = await makeAuth(undefined, name);
  const r = await auth.login('pai@example.com', 'senha-de-teste-1', meta());
  const token = r.ok ? r.token : '';
  const { readFile } = await import('node:fs/promises');
  await new Promise((res) => setTimeout(res, 50));
  const raw = await readFile(join(dir, `${name}.json`), 'utf8');
  assert.ok(!raw.includes(token));
});
