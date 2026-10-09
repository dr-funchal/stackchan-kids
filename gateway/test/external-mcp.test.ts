import assert from 'node:assert/strict';
import { randomBytes } from 'node:crypto';
import { mkdtemp, rm } from 'node:fs/promises';
import { createServer } from 'node:http';
import type { Server } from 'node:http';
import type { AddressInfo } from 'node:net';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { after, before, test } from 'node:test';
import { McpServer } from '@modelcontextprotocol/sdk/server/mcp.js';
import { StreamableHTTPServerTransport } from '@modelcontextprotocol/sdk/server/streamableHttp.js';
import { z } from 'zod';
import { silentLogger } from '../src/logger.ts';
import { ToolRegistry } from '../src/registry/registry.ts';
import { ExternalMcp, assertPublicUrl, isPrivateAddress } from '../src/services/external-mcp.ts';
import { defaultSettings } from '../src/settings.ts';
import type { Settings } from '../src/settings.ts';
import { JsonFile, SecretBox, SecretStore } from '../src/store.ts';

let dir: string;
let http: Server;
let url: string;

before(async () => {
  dir = await mkdtemp(join(tmpdir(), 'gw-ext-'));
  // A stateless MCP server that requires an API key header, like a typical hosted MCP
  http = createServer(async (req, res) => {
    if (req.headers['x-api-key'] !== 's3cret') {
      res.writeHead(401).end();
      return;
    }
    const server = new McpServer({ name: 'casa', version: '1.0.0' });
    server.registerTool('echo', { description: 'Repeats the text', inputSchema: { text: z.string() } }, async ({ text }) => ({
      content: [{ type: 'text', text: `eco: ${text}` }],
    }));
    server.registerTool('delete_everything', { description: 'Dangerous', inputSchema: {} }, async () => ({
      content: [{ type: 'text', text: 'boom' }],
    }));
    const transport = new StreamableHTTPServerTransport({ sessionIdGenerator: undefined });
    res.on('close', () => {
      void transport.close();
      void server.close();
    });
    await server.connect(transport);
    await transport.handleRequest(req, res);
  });
  await new Promise<void>((r) => http.listen(0, '127.0.0.1', r));
  url = `http://127.0.0.1:${(http.address() as AddressInfo).port}/mcp`;
});

after(async () => {
  http.close();
  await rm(dir, { recursive: true, force: true });
});

async function setup() {
  const settings = new JsonFile<Settings>(join(dir, `${randomBytes(4).toString('hex')}.json`), defaultSettings);
  await settings.load();
  const secrets = new SecretStore(join(dir, `${randomBytes(4).toString('hex')}-s.json`), new SecretBox(randomBytes(32).toString('base64')));
  const registry = new ToolRegistry({ log: silentLogger, defaultTimeoutMs: 5000, allowRestricted: false });
  const ext = new ExternalMcp({ settings, secrets, registry, log: silentLogger, allowPrivateForTests: true });
  return { settings, secrets, registry, ext };
}

test('private, local and credentialed URLs are refused', async () => {
  for (const bad of [
    'http://127.0.0.1/mcp',
    'http://localhost/mcp',
    'http://10.1.2.3/mcp',
    'http://192.168.0.10/mcp',
    'http://172.17.0.1:40163/',
    'http://169.254.169.254/latest/meta-data',
    'http://[::1]/mcp',
    'http://[::ffff:127.0.0.1]/mcp',
    'file:///etc/passwd',
    'https://user:pw@example.com/mcp',
  ]) {
    await assert.rejects(assertPublicUrl(bad), Error, bad);
  }
  assert.equal(isPrivateAddress('8.8.8.8'), false);
  assert.equal(isPrivateAddress('100.64.0.1'), true);
  assert.equal(isPrivateAddress('fd00::1'), true);
  assert.equal(isPrivateAddress('::ffff:7f00:1'), true);
  assert.equal(isPrivateAddress('::ffff:808:808'), false);
  assert.equal(isPrivateAddress('2606:4700::1111'), false);
});

test('discovers tools with an encrypted auth header; nothing is exposed until switched on', async () => {
  const { ext, registry, settings } = await setup();
  const server = await ext.add({ name: 'Casa', url, headers: [{ name: 'x-api-key', value: 's3cret' }] });
  assert.equal(server.lastError, undefined);
  assert.deepEqual(server.discovered.map((t) => t.name).sort(), ['delete_everything', 'echo']);
  assert.equal(registry.list().length, 0);
  assert.ok(!JSON.stringify(settings.get()).includes('s3cret'), 'header value is not in settings');

  await ext.update(server.id, { enabledTools: ['echo', 'not-a-tool'] });
  assert.deepEqual(registry.list().map((t) => t.name), ['ext_casa_echo']);
  const res = await registry.call('ext_casa_echo', { text: 'oi' });
  assert.equal(res.content[0]!.text, 'eco: oi');

  await ext.update(server.id, { enabled: false });
  assert.equal(registry.list().length, 0);
  await ext.remove(server.id);
  assert.equal(settings.get().external.length, 0);
  await ext.closeAll();
});

test('a wrong key is reported as an error and exposes nothing', async () => {
  const { ext, registry } = await setup();
  const server = await ext.add({ name: 'Errado', url, headers: [{ name: 'x-api-key', value: 'nope' }] });
  assert.ok(server.lastError);
  assert.equal(registry.list().length, 0);
  await ext.closeAll();
});
