import { readFileSync } from 'node:fs';

// Docker HEALTHCHECK: healthy while the heartbeat in the status file is fresh.
const file = process.env.STATUS_FILE || '/tmp/gateway-status.json';
try {
  const status = JSON.parse(readFileSync(file, 'utf8')) as { ts: string };
  const age = Date.now() - new Date(status.ts).getTime();
  process.exit(age < 60_000 ? 0 : 1);
} catch {
  process.exit(1);
}
