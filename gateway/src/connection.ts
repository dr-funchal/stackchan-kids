import { setTimeout as sleep } from 'node:timers/promises';
import { WebSocket } from 'ws';
import type { Config } from './config.ts';
import type { Logger } from './logger.ts';
import { createMcpServer, WebSocketTransport } from './mcp-server.ts';
import type { ToolRegistry } from './registry/registry.ts';
import type { StatusReporter } from './status.ts';

const PING_EVERY_MS = 20_000;
const PONG_TIMEOUT_MS = 10_000;
const HEALTHY_AFTER_MS = 30_000; // a session this long resets the backoff

export interface ConnectionInfo {
  state: 'standby' | 'connecting' | 'connected' | 'backoff';
  since: string;
  toolsListedAt?: string;
  toolsListed?: number;
}

/**
 * Keeps ONE outbound WebSocket to the xiaozhi.me MCP endpoint and reconnects forever with exponential backoff.
 * Being outbound-only means the VPS exposes no port, and a dead gateway only makes the extra tools disappear:
 * the robot keeps talking to xiaozhi.me as usual.
 */
export class GatewayConnection {
  #cfg: Config;
  #registry: ToolRegistry;
  #log: Logger;
  #status: StatusReporter;
  #stopped = false;
  #abort = new AbortController();
  #ws: WebSocket | undefined;
  #loop: Promise<void> | undefined;
  #skipWait = false;
  #info: ConnectionInfo = { state: 'standby', since: new Date().toISOString() };
  #listeners: ((info: ConnectionInfo) => void)[] = [];

  constructor(cfg: Config, registry: ToolRegistry, log: Logger, status: StatusReporter) {
    this.#cfg = cfg;
    this.#registry = registry;
    this.#log = log;
    this.#status = status;
  }

  start(): void {
    this.#loop = this.#run();
  }

  get info(): ConnectionInfo {
    return this.#info;
  }

  onChange(listener: (info: ConnectionInfo) => void): void {
    this.#listeners.push(listener);
  }

  #set(state: ConnectionInfo['state'], detail?: string): void {
    this.#status.set(state, detail);
    if (state !== this.#info.state) this.#info = { ...this.#info, state, since: new Date().toISOString() };
    this.#notify();
  }

  /** A failing listener (the panel broadcast, the activity feed) must never break the reconnect loop. */
  #notify(): void {
    for (const l of this.#listeners) {
      try {
        l(this.#info);
      } catch (err) {
        this.#log.warn('connection listener failed', { error: String(err) });
      }
    }
  }

  /** Drops the socket and reconnects at once, so xiaozhi.me lists the tools again (after a change in the panel). */
  reconnectNow(): void {
    if (!this.#ws || this.#stopped) return;
    this.#skipWait = true;
    this.#ws.terminate();
  }

  async stop(): Promise<void> {
    this.#stopped = true;
    this.#abort.abort();
    this.#ws?.terminate();
    await this.#loop;
  }

  async #run(): Promise<void> {
    let backoff = this.#cfg.reconnect.initialMs;
    while (!this.#stopped) {
      const started = Date.now();
      this.#set('connecting');
      try {
        await this.#session();
      } catch (err) {
        this.#log.warn('connection failed', { error: String(err) });
      }
      if (this.#stopped) break;
      if (this.#skipWait) {
        this.#skipWait = false;
        continue;
      }
      if (Date.now() - started > HEALTHY_AFTER_MS) backoff = this.#cfg.reconnect.initialMs;
      const wait = Math.round(backoff * (0.8 + Math.random() * 0.4));
      this.#set('backoff', `reconnecting in ${wait} ms`);
      this.#log.info('reconnecting', { inMs: wait });
      try {
        await sleep(wait, undefined, { signal: this.#abort.signal });
      } catch {
        break;
      }
      backoff = Math.min(backoff * 2, this.#cfg.reconnect.maxMs);
    }
  }

  /** Resolves when the socket closes, for whatever reason. */
  #session(): Promise<void> {
    return new Promise((resolve) => {
      const ws = new WebSocket(this.#cfg.endpoint!, { handshakeTimeout: 10_000 });
      this.#ws = ws;
      let pingTimer: NodeJS.Timeout | undefined;
      let pongTimer: NodeJS.Timeout | undefined;

      // An async listener's rejection is not caught by `ws`: it would crash the process, so it ends the session instead
      ws.on('open', () => {
        void (async () => {
          this.#log.info('connected to xiaozhi.me MCP endpoint');
          this.#set('connected');
          const server = createMcpServer(this.#registry, this.#log, (count) => {
            this.#info = { ...this.#info, toolsListedAt: new Date().toISOString(), toolsListed: count };
            this.#notify();
          });
          server.onerror = (err) => this.#log.warn('mcp error', { error: String(err) });
          await server.connect(new WebSocketTransport(ws));
          if (ws.readyState !== WebSocket.OPEN) return; // closed while connecting: no timers to leak

          pingTimer = setInterval(() => {
            if (ws.readyState !== WebSocket.OPEN) return; // ping() on a closing socket throws
            ws.ping();
            clearTimeout(pongTimer);
            pongTimer = setTimeout(() => {
              this.#log.warn('no pong, dropping connection');
              ws.terminate();
            }, PONG_TIMEOUT_MS);
          }, PING_EVERY_MS);
        })().catch((err) => {
          this.#log.warn('session setup failed', { error: String(err) });
          ws.terminate();
        });
      });
      ws.on('pong', () => clearTimeout(pongTimer));
      ws.on('error', (err) => this.#log.warn('socket error', { error: String(err) }));
      ws.on('close', (code) => {
        clearInterval(pingTimer);
        clearTimeout(pongTimer);
        this.#log.info('connection closed', { code });
        resolve();
      });
    });
  }
}
