import assert from 'node:assert/strict';
import { once } from 'node:events';
import type { AddressInfo } from 'node:net';
import { test } from 'node:test';
import { WebSocketServer } from 'ws';
import type { WebSocket } from 'ws';
import type { Config } from '../src/config.ts';
import { GatewayConnection } from '../src/connection.ts';
import { createLogger } from '../src/logger.ts';
import { buildModules } from '../src/modules/index.ts';
import { ToolRegistry } from '../src/registry/registry.ts';
import { StatusReporter } from '../src/status.ts';

const TOKEN = 'super-secret-test-token';

/** Plays the xiaozhi.me side: talks JSON-RPC over the socket and matches replies by id. */
function cloudSide(ws: WebSocket) {
  let nextId = 1;
  const pending = new Map<number, (result: any) => void>();
  ws.on('message', (data) => {
    const msg = JSON.parse(data.toString());
    pending.get(msg.id)?.(msg);
  });
  return {
    request: (method: string, params: unknown = {}) =>
      new Promise<any>((resolve) => {
        const id = nextId++;
        pending.set(id, resolve);
        ws.send(JSON.stringify({ jsonrpc: '2.0', id, method, params }));
      }),
    notify: (method: string) => ws.send(JSON.stringify({ jsonrpc: '2.0', method })),
  };
}

test('speaks MCP to the cloud endpoint, reconnects after a drop, and never logs the token', { timeout: 15_000 }, async () => {
  const wss = new WebSocketServer({ port: 0, host: '127.0.0.1' });
  await once(wss, 'listening');
  const port = (wss.address() as AddressInfo).port;
  const lines: string[] = [];
  const log = createLogger('debug', (l) => lines.push(l));

  const cfg: Config = {
    endpoint: `ws://127.0.0.1:${port}/mcp/?token=${TOKEN}`,
    enabled: true,
    modules: ['diagnostics', 'stories'],
    dataDir: '/nonexistent',
    timezone: 'America/Sao_Paulo',
    logLevel: 'debug',
    allowRestricted: false,
    toolTimeoutMs: 1000,
    httpPort: 8080,
    reconnect: { initialMs: 50, maxMs: 200 },
    statusFile: `/tmp/gw-test-status-${process.pid}.json`,
  };
  const registry = new ToolRegistry({ log, defaultTimeoutMs: 1000, allowRestricted: false });
  for (const m of buildModules(cfg)) registry.register(m);
  const status = new StatusReporter(cfg.statusFile, log);
  const gateway = new GatewayConnection(cfg, registry, log, status);

  const sessions: Promise<void>[] = [];
  let connections = 0;
  const firstDone = new Promise<void>((resolveFirst, rejectFirst) => {
    wss.on('connection', (ws) => {
      connections++;
      if (connections === 2) {
        sessions.push(Promise.resolve());
        return;
      }
      const cloud = cloudSide(ws);
      sessions.push(
        (async () => {
          const init = await cloud.request('initialize', {
            protocolVersion: '2024-11-05',
            capabilities: {},
            clientInfo: { name: 'fake-xiaozhi', version: '1' },
          });
          assert.equal(init.result.serverInfo.name, 'stackchan-gateway');
          cloud.notify('notifications/initialized');

          const list = await cloud.request('tools/list');
          const names = list.result.tools.map((t: { name: string }) => t.name);
          assert.deepEqual(names.sort(), ['gateway_get_time', 'gateway_secret_word', 'story_list', 'story_read_page']);

          const call = await cloud.request('tools/call', { name: 'gateway_secret_word', arguments: {} });
          assert.match(call.result.content[0].text, /secret word is: [a-z]+-\d\d/);

          const bad = await cloud.request('tools/call', { name: 'story_read_page', arguments: { story_id: '../x' } });
          assert.equal(bad.result.isError, true);

          ws.terminate(); // the cloud drops us: the gateway must come back on its own
          resolveFirst();
        })().catch((err) => {
          rejectFirst(err); // an assertion in the fake cloud must fail the test, not hang it
          throw err;
        }),
      );
    });
  });

  gateway.start();
  try {
    await firstDone;
  } catch (err) {
    await gateway.stop();
    wss.close();
    throw err;
  }
  await Promise.all(sessions);
  const deadline = Date.now() + 3000;
  while (connections < 2 && Date.now() < deadline) await new Promise((r) => setTimeout(r, 25));
  assert.equal(connections, 2, 'gateway should reconnect after the cloud drops the socket');

  await gateway.stop();
  await new Promise<void>((resolve) => wss.close(() => resolve()));
  assert.ok(lines.length > 0);
  assert.ok(!lines.some((l) => l.includes(TOKEN)), 'token leaked into the logs');
});
