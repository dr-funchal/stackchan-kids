import { loadConfig } from './config.ts';
import { GatewayConnection } from './connection.ts';
import { startHttpServer } from './http.ts';
import { createLogger } from './logger.ts';
import { GATEWAY_VERSION } from './mcp-server.ts';
import { buildModules } from './modules/index.ts';
import { ToolRegistry } from './registry/registry.ts';
import { StatusReporter } from './status.ts';

const cfg = loadConfig();
const log = createLogger(cfg.logLevel);
const status = new StatusReporter(cfg.statusFile, log);

const registry = new ToolRegistry({
  log,
  defaultTimeoutMs: cfg.toolTimeoutMs,
  allowRestricted: cfg.allowRestricted,
});
for (const mod of buildModules(cfg)) registry.register(mod);

log.info('stackchan-gateway starting', { version: GATEWAY_VERSION, tools: registry.size, modules: cfg.modules });
status.start();
const http = await startHttpServer(cfg.httpPort, log);

let connection: GatewayConnection | undefined;
if (!cfg.enabled) {
  status.set('standby', 'GATEWAY_ENABLED=false');
  log.warn('GATEWAY_ENABLED=false: idle, not connecting');
} else if (!cfg.endpoint) {
  status.set('standby', 'MCP_ENDPOINT not set');
  log.warn('MCP_ENDPOINT not set: idle. Run scripts/set-endpoint.sh to configure it.');
} else {
  connection = new GatewayConnection(cfg, registry, log, status);
  connection.start();
}

const shutdown = async (signal: string) => {
  log.info('shutting down', { signal });
  status.stop();
  http.close();
  await connection?.stop();
  process.exit(0);
};
process.on('SIGTERM', () => void shutdown('SIGTERM'));
process.on('SIGINT', () => void shutdown('SIGINT'));
