import { Server } from '@modelcontextprotocol/sdk/server/index.js';
import type { Transport } from '@modelcontextprotocol/sdk/shared/transport.js';
import {
  CallToolRequestSchema,
  JSONRPCMessageSchema,
  ListToolsRequestSchema,
} from '@modelcontextprotocol/sdk/types.js';
import type { JSONRPCMessage } from '@modelcontextprotocol/sdk/types.js';
import type { WebSocket } from 'ws';
import type { Logger } from './logger.ts';
import type { ToolRegistry } from './registry/registry.ts';

export const GATEWAY_VERSION = '0.1.0';

/** One JSON-RPC message per WebSocket text frame, which is how the xiaozhi.me MCP endpoint speaks. */
export class WebSocketTransport implements Transport {
  onclose?: () => void;
  onerror?: (error: Error) => void;
  onmessage?: (message: JSONRPCMessage) => void;
  #ws: WebSocket;

  constructor(ws: WebSocket) {
    this.#ws = ws;
  }

  // Listeners are attached synchronously: the cloud sends `initialize` right after the socket opens.
  async start(): Promise<void> {
    this.#ws.on('message', (data) => {
      try {
        this.onmessage?.(JSONRPCMessageSchema.parse(JSON.parse(data.toString())));
      } catch (err) {
        this.onerror?.(err instanceof Error ? err : new Error(String(err)));
      }
    });
    this.#ws.on('error', (err) => this.onerror?.(err));
    this.#ws.on('close', () => this.onclose?.());
  }

  async send(message: JSONRPCMessage): Promise<void> {
    if (this.#ws.readyState !== this.#ws.OPEN) throw new Error('WebSocket is not open');
    this.#ws.send(JSON.stringify(message));
  }

  async close(): Promise<void> {
    this.#ws.close();
  }
}

export function createMcpServer(registry: ToolRegistry, log: Logger, onListed?: (count: number) => void): Server {
  const server = new Server({ name: 'stackchan-gateway', version: GATEWAY_VERSION }, { capabilities: { tools: {} } });
  // Logged so we can tell when the cloud picked up new tool descriptions.
  server.setRequestHandler(ListToolsRequestSchema, async () => {
    const tools = registry.list();
    log.info('tools listed', { count: tools.length });
    onListed?.(tools.length);
    return { tools };
  });
  server.setRequestHandler(CallToolRequestSchema, async (request) =>
    registry.call(request.params.name, request.params.arguments),
  );
  return server;
}
