import assert from 'node:assert/strict';
import type { AddressInfo } from 'node:net';
import { after, before, test } from 'node:test';
import type { Server } from 'node:http';
import { startHttpServer } from '../src/http.ts';
import { silentLogger } from '../src/logger.ts';

let server: Server;
let base: string;

before(async () => {
  server = await startHttpServer(0, silentLogger);
  base = `http://127.0.0.1:${(server.address() as AddressInfo).port}`;
});
after(() => server.close());

test('/health answers ok without internals', async () => {
  const res = await fetch(`${base}/health`);
  assert.equal(res.status, 200);
  assert.deepEqual(await res.json(), { ok: true });
  assert.equal(res.headers.get('cache-control'), 'no-store');
});

test('anything else is 404', async () => {
  for (const [path, method] of [
    ['/', 'GET'],
    ['/health', 'POST'],
    ['/health?x=1', 'GET'],
    ['/../etc/passwd', 'GET'],
  ] as const) {
    const res = await fetch(`${base}${path}`, { method });
    assert.equal(res.status, 404, `${method} ${path}`);
    await res.text();
  }
});
