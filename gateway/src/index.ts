import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { ActivityFeed } from './activity.ts';
import { loadConfig } from './config.ts';
import { GatewayConnection } from './connection.ts';
import type { ConnectionInfo } from './connection.ts';
import { createLogger } from './logger.ts';
import { GATEWAY_VERSION } from './mcp-server.ts';
import { MODULES } from './modules/index.ts';
import { ToolRegistry } from './registry/registry.ts';
import { ExternalMcp } from './services/external-mcp.ts';
import { MusicLibrary } from './services/music-library.ts';
import { WeatherService } from './services/weather.ts';
import { defaultSettings } from './settings.ts';
import type { Settings } from './settings.ts';
import { StatusReporter } from './status.ts';
import { JsonFile, SecretBox, SecretStore } from './store.ts';
import { registerApi } from './web/api.ts';
import { Auth } from './web/auth.ts';
import type { AuthState } from './web/auth.ts';
import { Router, startWebServer } from './web/server.ts';

const cfg = loadConfig();
const log = createLogger(cfg.logLevel);
const status = new StatusReporter(cfg.statusFile, log);
const stateDir = join(cfg.dataDir, 'state');
const startedAt = Date.now();

const settings = new JsonFile<Settings>(join(stateDir, 'settings.json'), defaultSettings);
await settings.load();
const secrets = new SecretStore(join(stateDir, 'secrets.json'), cfg.secretsKey ? new SecretBox(cfg.secretsKey) : undefined);
const authFile = new JsonFile<AuthState>(join(stateDir, 'auth.json'), () => ({ sessions: {} }));
await authFile.load();
const auth = new Auth(authFile, { email: cfg.adminEmail, passwordHash: cfg.adminPasswordHash });
const activity = new ActivityFeed(join(stateDir, 'activity.json'));
await activity.load();
const music = new MusicLibrary(cfg.dataDir, log);
await music.load();
const weather = new WeatherService(settings);

const registry = new ToolRegistry({ log, defaultTimeoutMs: cfg.toolTimeoutMs, allowRestricted: cfg.allowRestricted });
const ctx = { dataDir: cfg.dataDir, timezone: cfg.timezone, settings, music, weather };
for (const mod of MODULES) registry.register(mod.build(ctx));

// Panel switches win over the MODULES env default
const applyPolicy = () => {
  const s = settings.get();
  registry.setPolicy({
    disabledSources: MODULES.filter((m) => !(s.modules[m.name] ?? cfg.modules.includes(m.name))).map((m) => m.name),
    disabledTools: s.disabledTools,
  });
};
applyPolicy();

const external = new ExternalMcp({ settings, secrets, registry, log });

registry.onCall((e) =>
  activity.add({ kind: 'tool', title: e.summary ?? e.tool, detail: `${e.tool} · ${e.ms} ms`, ok: e.ok }),
);

if (!cfg.secretsKey) log.warn('SECRETS_KEY not set: credentials of external MCP servers cannot be stored');
if (!auth.enabled) log.warn('ADMIN_EMAIL / ADMIN_PASSWORD_HASH not set: the panel login is disabled');
log.info('stackchan-gateway starting', { version: GATEWAY_VERSION, tools: registry.size });
status.start();

let connection: GatewayConnection | undefined;
let standby: ConnectionInfo = { state: 'standby', since: new Date().toISOString() };

const router = new Router();
const { broadcast } = registerApi(router, {
  cfg,
  log,
  auth,
  settings,
  activity,
  registry,
  music,
  weather,
  external,
  connection: () => connection?.info ?? standby,
  applyPolicy,
  startedAt,
});
const http = await startWebServer({
  port: cfg.httpPort,
  log,
  router,
  publicDir: fileURLToPath(new URL('./web/public/', import.meta.url)),
  version: GATEWAY_VERSION,
  allowedOrigins: [cfg.publicUrl, ...cfg.devOrigins],
  checkSession: (token) => auth.check(token),
});

if (!cfg.enabled) {
  standby = { ...standby, state: 'standby' };
  status.set('standby', 'GATEWAY_ENABLED=false');
  log.warn('GATEWAY_ENABLED=false: idle, not connecting');
} else if (!cfg.endpoint) {
  status.set('standby', 'MCP_ENDPOINT not set');
  log.warn('MCP_ENDPOINT not set: idle. Run scripts/set-endpoint.sh to configure it.');
} else {
  const conn = new GatewayConnection(cfg, registry, log, status);
  connection = conn;
  let lastState = '';
  let refreshing = false; // a reconnect we caused ourselves is not a connection problem
  conn.onChange((info) => {
    broadcast('status', info);
    if (info.state !== lastState && (info.state === 'connected' || lastState === 'connected')) {
      if (refreshing) {
        if (info.state === 'connected') {
          refreshing = false;
          activity.add({ kind: 'system', title: `Ferramentas atualizadas no robô (${registry.size} ativas)`, ok: true });
        }
      } else {
        activity.add({
          kind: 'system',
          title: info.state === 'connected' ? 'Conectado ao xiaozhi.me' : 'Conexão com o xiaozhi.me caiu',
          ok: info.state === 'connected',
        });
      }
    }
    lastState = info.state;
  });
  conn.start();

  // Tools changed in the panel: reconnect so xiaozhi.me lists them again (debounced: several switches at once)
  let timer: NodeJS.Timeout | undefined;
  registry.onChange(() => {
    clearTimeout(timer);
    timer = setTimeout(() => {
      if (conn.info.state === 'connected') {
        log.info('tool list changed, refreshing xiaozhi.me');
        refreshing = true;
        conn.reconnectNow();
      }
    }, 2000);
  });
}

void external.startAll();

const shutdown = async (signal: string) => {
  log.info('shutting down', { signal });
  status.stop();
  http.close();
  await Promise.allSettled([connection?.stop(), external.closeAll(), activity.flush()]);
  process.exit(0);
};
process.on('SIGTERM', () => void shutdown('SIGTERM'));
process.on('SIGINT', () => void shutdown('SIGINT'));
