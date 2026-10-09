import { createHash } from 'node:crypto';
import { readdir, readFile } from 'node:fs/promises';
import { createServer } from 'node:http';
import type { IncomingMessage, Server, ServerResponse } from 'node:http';
import { extname, join } from 'node:path';
import type { Logger } from '../logger.ts';

export class HttpError extends Error {
  status: number;
  constructor(status: number, message: string) {
    super(message);
    this.status = status;
  }
}

export interface Ctx {
  req: IncomingMessage;
  res: ServerResponse;
  method: string;
  path: string;
  query: URLSearchParams;
  params: Record<string, string>;
  ip: string;
  token: string | undefined;
}

type Handler = (c: Ctx) => Promise<void> | void;

interface Route {
  method: string;
  re: RegExp;
  keys: string[];
  handler: Handler;
  auth: boolean;
}

export const SESSION_COOKIE = '__Host-stackchan';

export class Router {
  #routes: Route[] = [];

  /** Patterns like /api/stories/:id. Routes need a session unless { auth: false }. */
  on(method: string, pattern: string, handler: Handler, opts: { auth?: boolean } = {}): void {
    const keys: string[] = [];
    const re = new RegExp(
      '^' +
        pattern.replace(/[.*+?^${}()|[\]\\]/g, '\\$&').replace(/\/:(\w+)/g, (_m, key: string) => {
          keys.push(key);
          return '/([^/]+)';
        }) +
        '$',
    );
    this.#routes.push({ method, re, keys, handler, auth: opts.auth ?? true });
  }

  match(method: string, path: string): { route: Route; params: Record<string, string> } | 'method' | undefined {
    let pathMatched = false;
    for (const route of this.#routes) {
      const m = route.re.exec(path);
      if (!m) continue;
      pathMatched = true;
      if (route.method !== method && !(method === 'HEAD' && route.method === 'GET')) continue;
      const params: Record<string, string> = {};
      route.keys.forEach((k, i) => (params[k] = decodeURIComponent(m[i + 1]!)));
      return { route, params };
    }
    return pathMatched ? 'method' : undefined;
  }
}

export function sendJson(c: Ctx, status: number, body: unknown): void {
  const data = JSON.stringify(body);
  c.res.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8', 'Content-Length': Buffer.byteLength(data) });
  c.res.end(c.method === 'HEAD' ? undefined : data);
}

export async function readJson<T = Record<string, unknown>>(c: Ctx, limit = 256 * 1024): Promise<T> {
  if (!String(c.req.headers['content-type'] ?? '').startsWith('application/json')) {
    throw new HttpError(415, 'expected application/json');
  }
  const chunks: Buffer[] = [];
  let size = 0;
  for await (const chunk of c.req) {
    size += (chunk as Buffer).length;
    if (size > limit) throw new HttpError(413, 'request too large');
    chunks.push(chunk as Buffer);
  }
  try {
    const value = JSON.parse(Buffer.concat(chunks).toString('utf8') || '{}');
    if (!value || typeof value !== 'object' || Array.isArray(value)) throw new Error();
    return value as T;
  } catch {
    throw new HttpError(400, 'invalid JSON');
  }
}

function cookies(req: IncomingMessage): Record<string, string> {
  const out: Record<string, string> = {};
  for (const part of String(req.headers.cookie ?? '').split(';')) {
    const i = part.indexOf('=');
    if (i > 0) out[part.slice(0, i).trim()] = decodeURIComponent(part.slice(i + 1).trim());
  }
  return out;
}

export function setSessionCookie(c: Ctx, token: string | undefined): void {
  const base = `${SESSION_COOKIE}=${token ?? ''}; Path=/; HttpOnly; Secure; SameSite=Strict`;
  c.res.setHeader('Set-Cookie', token ? `${base}; Max-Age=${30 * 24 * 3600}` : `${base}; Max-Age=0`);
}

const TYPES: Record<string, string> = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.webmanifest': 'application/manifest+json',
  '.png': 'image/png',
};

const CSP = [
  "default-src 'self'",
  "script-src 'self'",
  "style-src 'self'",
  "img-src 'self' data:",
  "media-src 'self' blob:",
  "connect-src 'self'",
  "font-src 'self'",
  "frame-ancestors 'none'",
  "base-uri 'none'",
  "form-action 'self'",
  "object-src 'none'",
].join('; ');

interface StaticFile {
  body: Buffer;
  type: string;
  etag: string;
}

async function loadStatic(dir: string, version: string): Promise<Map<string, StaticFile>> {
  const files = new Map<string, StaticFile>();
  for (const name of await readdir(dir)) {
    const type = TYPES[extname(name)];
    if (!type) continue;
    let body = await readFile(join(dir, name));
    // Cache busting: index.html references assets with ?v=__VERSION__
    if (name === 'index.html') body = Buffer.from(body.toString('utf8').replaceAll('__VERSION__', version));
    files.set(`/${name}`, { body, type, etag: `"${createHash('sha1').update(body).digest('base64url')}"` });
  }
  files.set('/', files.get('/index.html')!);
  return files;
}

export interface WebServerOptions {
  port: number;
  log: Logger;
  router: Router;
  publicDir: string;
  version: string;
  allowedOrigins: string[];
  checkSession(token: string | undefined): boolean;
}

/**
 * Plain node:http: static panel files, JSON API and /a/ audio for the robot. Behind nginx (TLS) on the VPS; the port
 * is published on 127.0.0.1 only.
 */
export async function startWebServer(opts: WebServerOptions): Promise<Server> {
  const files = await loadStatic(opts.publicDir, opts.version);
  const { log, router } = opts;

  const server = createServer(async (req, res) => {
    const url = new URL(req.url ?? '/', 'http://local');
    const c: Ctx = {
      req,
      res,
      method: req.method ?? 'GET',
      path: url.pathname,
      query: url.searchParams,
      params: {},
      // Only nginx can reach the port, so its X-Real-IP is trustworthy
      ip: String(req.headers['x-real-ip'] ?? req.socket.remoteAddress ?? '?'),
      token: cookies(req)[SESSION_COOKIE],
    };
    res.setHeader('X-Content-Type-Options', 'nosniff');
    res.setHeader('Referrer-Policy', 'no-referrer');
    res.setHeader('X-Frame-Options', 'DENY');
    res.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
    res.setHeader('Permissions-Policy', 'camera=(), microphone=(), geolocation=()');
    try {
      if (c.path === '/health' && (c.method === 'GET' || c.method === 'HEAD')) {
        res.setHeader('Cache-Control', 'no-store');
        return sendJson(c, 200, { ok: true });
      }

      const found = router.match(c.method, c.path);
      if (found === 'method') throw new HttpError(405, 'method not allowed');
      if (found) {
        c.params = found.params;
        res.setHeader('Cache-Control', 'no-store');
        if (found.route.auth && !opts.checkSession(c.token)) throw new HttpError(401, 'login required');
        // CSRF: the session cookie is SameSite=Strict, and state changes must come from the panel's own origin
        if (!['GET', 'HEAD'].includes(c.method)) {
          const origin = String(req.headers.origin ?? '');
          if (!opts.allowedOrigins.includes(origin)) throw new HttpError(403, 'bad origin');
        }
        await found.route.handler(c);
        return;
      }

      const file = (c.method === 'GET' || c.method === 'HEAD') && !c.path.startsWith('/api/') ? files.get(c.path) : undefined;
      if (!file) throw new HttpError(404, 'not found');
      res.setHeader('Content-Type', file.type);
      res.setHeader('ETag', file.etag);
      res.setHeader('Cache-Control', file.type.startsWith('text/html') ? 'no-cache' : 'public, max-age=86400');
      if (file.type.startsWith('text/html')) res.setHeader('Content-Security-Policy', CSP);
      if (req.headers['if-none-match'] === file.etag) {
        res.writeHead(304);
        res.end();
        return;
      }
      res.writeHead(200, { 'Content-Length': file.body.length });
      res.end(c.method === 'HEAD' ? undefined : file.body);
    } catch (err) {
      const status = err instanceof HttpError ? err.status : 500;
      if (status >= 500) log.error('http error', { path: c.path, error: String(err) });
      if (res.headersSent) {
        res.destroy();
        return;
      }
      const message = err instanceof HttpError ? err.message : 'internal error';
      if (c.path.startsWith('/api/') || c.path.startsWith('/a/') || c.method !== 'GET') {
        sendJson(c, status, { error: message });
      } else {
        res.writeHead(status, { 'Content-Type': 'text/plain; charset=utf-8' });
        res.end(status === 404 ? 'not found' : message);
      }
    }
  });
  server.headersTimeout = 10_000;
  server.requestTimeout = 0; // uploads and SSE run long; nginx enforces its own timeouts
  server.keepAliveTimeout = 30_000;

  return new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(opts.port, () => {
      log.info('http listening', { port: opts.port });
      resolve(server);
    });
  });
}
