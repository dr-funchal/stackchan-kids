import type { Config } from '../config.ts';
import type { GatewayModule } from '../registry/registry.ts';
import { diagnosticsModule } from './diagnostics.ts';
import { storiesModule } from './stories.ts';

/**
 * Module catalog. To add a service (weather, home automation...), create src/modules/<name>.ts that returns a
 * GatewayModule, add one line here, and list it in MODULES. The registry and the connection never change.
 */
const FACTORIES: Record<string, (cfg: Config) => GatewayModule> = {
  diagnostics: (cfg) => diagnosticsModule({ timezone: cfg.timezone }),
  stories: (cfg) => storiesModule({ dataDir: cfg.dataDir }),
};

export function buildModules(cfg: Config): GatewayModule[] {
  return cfg.modules.map((name) => {
    const factory = FACTORIES[name];
    if (!factory) throw new Error(`unknown module "${name}" (available: ${Object.keys(FACTORIES).join(', ')})`);
    return factory(cfg);
  });
}
