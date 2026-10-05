import { createServer } from 'node:http';
import type { Server } from 'node:http';
import type { Logger } from './logger.ts';

/**
 * Public HTTP side, published only on 127.0.0.1 and reached through nginx at https://m5.pulpfy.com.
 * Today it only answers /health; phase 6 adds short-lived audio URLs here.
 */
export function startHttpServer(port: number, log: Logger): Promise<Server> {
  const server = createServer((req, res) => {
    res.setHeader('Cache-Control', 'no-store');
    res.setHeader('X-Content-Type-Options', 'nosniff');
    if ((req.method === 'GET' || req.method === 'HEAD') && req.url === '/health') {
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(req.method === 'HEAD' ? undefined : '{"ok":true}');
      return;
    }
    res.writeHead(404, { 'Content-Type': 'text/plain' });
    res.end('not found');
  });
  server.headersTimeout = 5_000;
  server.requestTimeout = 10_000;
  return new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(port, () => {
      log.info('http listening', { port });
      resolve(server);
    });
  });
}
