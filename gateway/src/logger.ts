export type Level = 'debug' | 'info' | 'warn' | 'error';

const ORDER: Record<Level, number> = { debug: 10, info: 20, warn: 30, error: 40 };

export interface Logger {
  debug(msg: string, fields?: Record<string, unknown>): void;
  info(msg: string, fields?: Record<string, unknown>): void;
  warn(msg: string, fields?: Record<string, unknown>): void;
  error(msg: string, fields?: Record<string, unknown>): void;
}

// The endpoint token travels in the URL query; ws errors and stack traces can echo the URL.
const TOKEN_RE = /(token=)[^&\s"'\\]+/gi;

export function redact(text: string): string {
  return text.replace(TOKEN_RE, '$1***');
}

export function createLogger(
  level: Level,
  sink: (line: string) => void = (line) => process.stdout.write(line + '\n'),
): Logger {
  const min = ORDER[level];
  const emit = (lvl: Level, msg: string, fields?: Record<string, unknown>) => {
    if (ORDER[lvl] < min) return;
    sink(redact(JSON.stringify({ ts: new Date().toISOString(), level: lvl, msg, ...fields })));
  };
  return {
    debug: (m, f) => emit('debug', m, f),
    info: (m, f) => emit('info', m, f),
    warn: (m, f) => emit('warn', m, f),
    error: (m, f) => emit('error', m, f),
  };
}

export const silentLogger: Logger = {
  debug() {},
  info() {},
  warn() {},
  error() {},
};
