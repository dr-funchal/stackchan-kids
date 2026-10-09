import { randomBytes } from 'node:crypto';
import { lookup } from 'node:dns/promises';
import { isIP } from 'node:net';
import { Client } from '@modelcontextprotocol/sdk/client/index.js';
import { SSEClientTransport } from '@modelcontextprotocol/sdk/client/sse.js';
import { StreamableHTTPClientTransport } from '@modelcontextprotocol/sdk/client/streamableHttp.js';
import type { Logger } from '../logger.ts';
import { GATEWAY_VERSION } from '../mcp-server.ts';
import type { JsonSchema, ToolDef, ToolRegistry } from '../registry/registry.ts';
import type { ExternalServer, SettingsFile } from '../settings.ts';
import type { SecretStore } from '../store.ts';
import { slug } from '../text.ts';

const CONNECT_TIMEOUT_MS = 10_000;

/** Blocks loopback, private, link-local and similar ranges, so the panel cannot be used to reach the VPS internals. */
export function isPrivateAddress(ip: string): boolean {
  if (isIP(ip) === 4) {
    const [a, b] = ip.split('.').map(Number) as [number, number];
    return (
      a === 0 || a === 10 || a === 127 || (a === 100 && b >= 64 && b <= 127) || (a === 169 && b === 254) ||
      (a === 172 && b >= 16 && b <= 31) || (a === 192 && b === 168) || (a === 198 && (b === 18 || b === 19)) || a >= 224
    );
  }
  const v6 = ip.toLowerCase();
  // IPv4-mapped: URL normalizes [::ffff:127.0.0.1] to [::ffff:7f00:1]
  const mapped = /^::ffff:(?:0:)?(.+)$/.exec(v6);
  if (mapped) {
    const rest = mapped[1]!;
    if (isIP(rest) === 4) return isPrivateAddress(rest);
    const hex = /^([0-9a-f]{1,4}):([0-9a-f]{1,4})$/.exec(rest);
    if (!hex) return true;
    const hi = parseInt(hex[1]!, 16);
    const lo = parseInt(hex[2]!, 16);
    return isPrivateAddress(`${hi >> 8}.${hi & 255}.${lo >> 8}.${lo & 255}`);
  }
  return (
    v6 === '::' || v6 === '::1' || /^f[cd]/.test(v6) || /^fe[89ab]/.test(v6) || v6.startsWith('ff') || v6.startsWith('64:ff9b')
  );
}

export async function assertPublicUrl(raw: string): Promise<URL> {
  let url: URL;
  try {
    url = new URL(raw);
  } catch {
    throw new Error('invalid URL');
  }
  if (url.protocol !== 'https:' && url.protocol !== 'http:') throw new Error('only http(s) MCP servers are supported');
  if (url.username || url.password) throw new Error('put credentials in a header, not in the URL');
  const host = url.hostname.replace(/^\[|\]$/g, '');
  const addresses = isIP(host) ? [host] : (await lookup(host, { all: true })).map((a) => a.address);
  if (addresses.length === 0 || addresses.some(isPrivateAddress)) {
    throw new Error('the server must be on the public internet (private and local addresses are blocked)');
  }
  return url;
}

function toolName(server: ExternalServer, tool: string): string {
  return `ext_${slug(server.name, 16).replace(/-/g, '_') || 'srv'}_${slug(tool, 64).replace(/-/g, '_')}`.slice(0, 64);
}

function cleanSchema(schema: unknown): JsonSchema {
  const s = (schema && typeof schema === 'object' ? { ...schema } : {}) as Record<string, unknown>;
  delete s.$schema; // draft 2020-12 meta-schema refs break the default validator
  return { ...s, type: 'object' } as JsonSchema;
}

/**
 * Remote MCP servers the parent connects in the panel (HTTP only: the panel never runs programs on the VPS).
 * Their tools are re-exposed to the robot with an ext_<server>_ prefix, and each one must be switched on by hand.
 */
export class ExternalMcp {
  #settings: SettingsFile;
  #secrets: SecretStore;
  #registry: ToolRegistry;
  #log: Logger;
  #clients = new Map<string, Client>();
  #checkUrl: (url: string) => Promise<URL>;

  constructor(opts: {
    settings: SettingsFile;
    secrets: SecretStore;
    registry: ToolRegistry;
    log: Logger;
    /** Tests only: a local MCP server would be refused by the public-address check. */
    allowPrivateForTests?: boolean;
  }) {
    this.#settings = opts.settings;
    this.#secrets = opts.secrets;
    this.#registry = opts.registry;
    this.#log = opts.log;
    this.#checkUrl = opts.allowPrivateForTests ? async (u) => new URL(u) : assertPublicUrl;
  }

  list(): ExternalServer[] {
    return this.#settings.get().external;
  }

  async #headers(server: ExternalServer): Promise<Record<string, string>> {
    const headers: Record<string, string> = {};
    for (const name of server.headerNames) {
      const value = await this.#secrets.get(`mcp.${server.id}.${name}`);
      if (value) headers[name] = value;
    }
    return headers;
  }

  async #connect(server: ExternalServer): Promise<Client> {
    await this.#clients.get(server.id)?.close().catch(() => {});
    this.#clients.delete(server.id);
    const url = await this.#checkUrl(server.url);
    const requestInit = { headers: await this.#headers(server) };
    const attempt = async (transport: 'streamable' | 'sse') => {
      const client = new Client({ name: 'stackchan-gateway', version: GATEWAY_VERSION });
      const t =
        transport === 'streamable'
          ? new StreamableHTTPClientTransport(url, { requestInit })
          : new SSEClientTransport(url, { requestInit });
      await client.connect(t, { timeout: CONNECT_TIMEOUT_MS });
      return client;
    };
    let client: Client;
    try {
      client = await attempt('streamable');
    } catch (err) {
      this.#log.info('streamable http failed, trying sse', { server: server.name, error: String(err) });
      client = await attempt('sse');
    }
    client.onerror = (err) => this.#log.warn('external mcp error', { server: server.name, error: String(err) });
    this.#clients.set(server.id, client);
    return client;
  }

  /** Connects, refreshes the discovered tool list and re-registers the enabled tools. */
  async refresh(id: string): Promise<ExternalServer> {
    const server = this.list().find((s) => s.id === id);
    if (!server) throw new Error('server not found');
    try {
      if (!server.enabled) {
        this.#registry.removeSource(`mcp:${id}`);
        await this.#clients.get(id)?.close().catch(() => {});
        this.#clients.delete(id);
        return server;
      }
      const client = await this.#connect(server);
      const { tools } = await client.listTools(undefined, { timeout: CONNECT_TIMEOUT_MS });
      await this.#settings.update(() => {
        server.discovered = tools.map((t) => ({ name: t.name, description: t.description ?? '' }));
        server.enabledTools = server.enabledTools.filter((n) => tools.some((t) => t.name === n));
        server.lastCheckedAt = new Date().toISOString();
        delete server.lastError;
      });
      const defs: ToolDef[] = tools
        .filter((t) => server.enabledTools.includes(t.name))
        .map((t) => ({
          name: toolName(server, t.name),
          description: `[${server.name}] ${t.description ?? t.name}`.slice(0, 1000),
          inputSchema: cleanSchema(t.inputSchema),
          summarize: () => `${server.name}: ${t.name}`,
          handler: async (args, { signal }) => this.#call(server, t.name, args, signal),
        }));
      this.#registry.setSource(`mcp:${id}`, defs);
    } catch (err) {
      this.#registry.removeSource(`mcp:${id}`);
      await this.#settings.update(() => {
        server.lastError = String(err instanceof Error ? err.message : err).slice(0, 300);
        server.lastCheckedAt = new Date().toISOString();
      });
      this.#log.warn('external mcp unavailable', { server: server.name, error: server.lastError });
    }
    return server;
  }

  async #call(server: ExternalServer, tool: string, args: Record<string, unknown>, signal: AbortSignal) {
    let client = this.#clients.get(server.id) ?? (await this.#connect(server));
    let result;
    try {
      result = await client.callTool({ name: tool, arguments: args }, undefined, { signal });
    } catch (err) {
      if (signal.aborted) throw err;
      client = await this.#connect(server); // the remote side may have dropped the session: one retry
      result = await client.callTool({ name: tool, arguments: args }, undefined, { signal });
    }
    const content = (result.content ?? []) as { type: string; text?: string }[];
    const text = content.map((c) => (c.type === 'text' ? c.text : `[${c.type} omitted]`)).join('\n').slice(0, 8000);
    if (result.isError) throw new Error(text || 'external tool error');
    return text || 'Done.';
  }

  async add(input: { name: string; url: string; headers: { name: string; value: string }[] }) {
    await this.#checkUrl(input.url);
    const headers = input.headers.filter((h) => h.name.trim() && h.value);
    for (const h of headers) {
      if (!/^[A-Za-z0-9-]{1,64}$/.test(h.name.trim())) throw new Error(`invalid header name: ${h.name}`);
    }
    const server: ExternalServer = {
      id: randomBytes(6).toString('hex'),
      name: input.name.trim().slice(0, 40) || new URL(input.url).hostname,
      url: input.url.trim(),
      enabled: true,
      headerNames: headers.map((h) => h.name.trim()),
      enabledTools: [],
      discovered: [],
    };
    for (const h of headers) await this.#secrets.set(`mcp.${server.id}.${h.name.trim()}`, h.value);
    await this.#settings.update((s) => s.external.push(server));
    return this.refresh(server.id);
  }

  async update(id: string, patch: { enabled?: boolean; enabledTools?: string[]; name?: string }) {
    const server = this.list().find((s) => s.id === id);
    if (!server) throw new Error('server not found');
    await this.#settings.update(() => {
      if (patch.enabled !== undefined) server.enabled = patch.enabled;
      if (patch.name?.trim()) server.name = patch.name.trim().slice(0, 40);
      if (patch.enabledTools) {
        server.enabledTools = patch.enabledTools.filter((n) => server.discovered.some((t) => t.name === n));
      }
    });
    return this.refresh(id);
  }

  async remove(id: string): Promise<void> {
    this.#registry.removeSource(`mcp:${id}`);
    await this.#clients.get(id)?.close().catch(() => {});
    this.#clients.delete(id);
    await this.#secrets.deletePrefix(`mcp.${id}.`);
    await this.#settings.update((s) => {
      s.external = s.external.filter((e) => e.id !== id);
    });
  }

  async startAll(): Promise<void> {
    await Promise.all(this.list().map((s) => this.refresh(s.id)));
  }

  async closeAll(): Promise<void> {
    await Promise.all([...this.#clients.values()].map((c) => c.close().catch(() => {})));
  }
}
