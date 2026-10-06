import type { Level } from './logger.ts';

export interface Config {
  /** xiaozhi.me agent MCP endpoint (wss://api.xiaozhi.me/mcp/?token=...). Secret: never log it. */
  endpoint: string | undefined;
  /** Master switch. false = stay idle without connecting (rollback without removing the container). */
  enabled: boolean;
  modules: string[];
  dataDir: string;
  timezone: string;
  logLevel: Level;
  allowRestricted: boolean;
  toolTimeoutMs: number;
  httpPort: number;
  /** Public origin of the panel, e.g. https://m5.pulpfy.com (OAuth redirect, CSRF origin check, audio links). */
  publicUrl: string;
  /** Extra origins allowed to call the API (local development only). */
  devOrigins: string[];
  adminEmail: string;
  /** scrypt:N:r:p:salt:hash (see scripts/hash-password.ts). The plain password is never stored. */
  adminPasswordHash: string;
  /** 32 random bytes in base64: encrypts Spotify tokens and API keys at rest. */
  secretsKey: string | undefined;
  maxUploadMb: number;
  reconnect: { initialMs: number; maxMs: number };
  statusFile: string;
}

const LEVELS: Level[] = ['debug', 'info', 'warn', 'error'];

function bool(value: string | undefined, fallback: boolean, name: string): boolean {
  if (value === undefined || value === '') return fallback;
  if (/^(true|1|yes)$/i.test(value)) return true;
  if (/^(false|0|no)$/i.test(value)) return false;
  throw new Error(`${name} must be true or false (got "${value}")`);
}

function int(value: string | undefined, fallback: number, name: string): number {
  if (value === undefined || value === '') return fallback;
  const n = Number(value);
  if (!Number.isInteger(n) || n <= 0) throw new Error(`${name} must be a positive integer (got "${value}")`);
  return n;
}

function checkEndpoint(raw: string): string {
  let url: URL;
  try {
    url = new URL(raw);
  } catch {
    throw new Error('MCP_ENDPOINT is not a valid URL (value hidden: it contains a token)');
  }
  const loopback = ['localhost', '127.0.0.1', '[::1]'].includes(url.hostname);
  if (url.protocol !== 'wss:' && !(url.protocol === 'ws:' && loopback)) {
    throw new Error('MCP_ENDPOINT must use wss:// (ws:// is only accepted for localhost)');
  }
  return raw;
}

export function loadConfig(env: NodeJS.ProcessEnv = process.env): Config {
  const logLevel = (env.LOG_LEVEL || 'info').toLowerCase() as Level;
  if (!LEVELS.includes(logLevel)) throw new Error(`LOG_LEVEL must be one of ${LEVELS.join(', ')}`);

  const endpoint = env.MCP_ENDPOINT?.trim();
  return {
    endpoint: endpoint ? checkEndpoint(endpoint) : undefined,
    enabled: bool(env.GATEWAY_ENABLED, true, 'GATEWAY_ENABLED'),
    modules: (env.MODULES ?? 'diagnostics,stories,music,spotify,weather')
      .split(',')
      .map((m) => m.trim())
      .filter(Boolean),
    dataDir: env.DATA_DIR || '/app/data',
    timezone: env.GATEWAY_TIMEZONE || 'America/Sao_Paulo',
    logLevel,
    allowRestricted: bool(env.ALLOW_RESTRICTED_TOOLS, false, 'ALLOW_RESTRICTED_TOOLS'),
    toolTimeoutMs: int(env.TOOL_TIMEOUT_MS, 8000, 'TOOL_TIMEOUT_MS'),
    httpPort: int(env.HTTP_PORT, 8080, 'HTTP_PORT'),
    publicUrl: new URL(env.PUBLIC_URL || 'https://m5.pulpfy.com').origin,
    devOrigins: (env.DEV_ORIGINS ?? '')
      .split(',')
      .map((o) => o.trim())
      .filter(Boolean),
    adminEmail: (env.ADMIN_EMAIL ?? '').trim().toLowerCase(),
    adminPasswordHash: (env.ADMIN_PASSWORD_HASH ?? '').trim(),
    secretsKey: env.SECRETS_KEY?.trim() || undefined,
    maxUploadMb: int(env.MAX_UPLOAD_MB, 60, 'MAX_UPLOAD_MB'),
    reconnect: {
      initialMs: int(env.RECONNECT_INITIAL_MS, 1000, 'RECONNECT_INITIAL_MS'),
      maxMs: int(env.RECONNECT_MAX_MS, 60000, 'RECONNECT_MAX_MS'),
    },
    statusFile: env.STATUS_FILE || '/tmp/gateway-status.json',
  };
}
