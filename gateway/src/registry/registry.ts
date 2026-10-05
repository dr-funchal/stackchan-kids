import { Ajv } from 'ajv';
import type { ValidateFunction } from 'ajv';
import type { Logger } from '../logger.ts';

export interface JsonSchema {
  type: 'object';
  properties?: Record<string, unknown>;
  required?: string[];
  additionalProperties?: boolean;
  [key: string]: unknown;
}

export interface ToolContext {
  signal: AbortSignal;
  log: Logger;
}

export interface ToolDef {
  /** snake_case, unique across modules. */
  name: string;
  /** Written for the LLM: say WHEN to call the tool. */
  description: string;
  inputSchema: JsonSchema;
  /** 'restricted' tools are not exposed unless ALLOW_RESTRICTED_TOOLS=true (children use this device). */
  risk?: 'safe' | 'restricted';
  timeoutMs?: number;
  maxCallsPerMinute?: number;
  handler(args: Record<string, unknown>, ctx: ToolContext): Promise<string>;
}

export interface GatewayModule {
  name: string;
  tools(): ToolDef[];
}

// A type alias (not an interface) so it is assignable to the SDK's index-signature result type.
export type CallResult = {
  content: { type: 'text'; text: string }[];
  isError?: boolean;
};

export interface RegistryOptions {
  log: Logger;
  defaultTimeoutMs: number;
  allowRestricted: boolean;
  now?: () => number;
}

interface Registered {
  def: ToolDef;
  validate: ValidateFunction;
  calls: number[];
}

const NAME_RE = /^[a-z][a-z0-9_]{2,63}$/;
const DEFAULT_MAX_CALLS_PER_MINUTE = 30;

const ok = (text: string): CallResult => ({ content: [{ type: 'text', text }] });
const fail = (text: string): CallResult => ({ content: [{ type: 'text', text }], isError: true });

export class ToolRegistry {
  #tools = new Map<string, Registered>();
  #ajv = new Ajv({ coerceTypes: true, useDefaults: true, strict: true });
  #opts: RegistryOptions;
  #now: () => number;

  constructor(opts: RegistryOptions) {
    this.#opts = opts;
    this.#now = opts.now ?? Date.now;
  }

  register(mod: GatewayModule): void {
    for (const def of mod.tools()) {
      if (!NAME_RE.test(def.name)) throw new Error(`invalid tool name "${def.name}" in module ${mod.name}`);
      if (this.#tools.has(def.name)) throw new Error(`duplicate tool name "${def.name}" in module ${mod.name}`);
      if (def.risk === 'restricted' && !this.#opts.allowRestricted) {
        this.#opts.log.warn('restricted tool not exposed', { tool: def.name, module: mod.name });
        continue;
      }
      // Throws at startup if the schema is malformed, instead of failing on the first child request.
      const validate = this.#ajv.compile(def.inputSchema);
      this.#tools.set(def.name, { def, validate, calls: [] });
      this.#opts.log.info('tool registered', { tool: def.name, module: mod.name });
    }
  }

  list(): { name: string; description: string; inputSchema: JsonSchema }[] {
    return [...this.#tools.values()].map(({ def }) => ({
      name: def.name,
      description: def.description,
      inputSchema: def.inputSchema,
    }));
  }

  get size(): number {
    return this.#tools.size;
  }

  /** Never throws: the LLM always gets a result it can explain to the child. */
  async call(name: string, rawArgs: unknown): Promise<CallResult> {
    const { log } = this.#opts;
    const tool = this.#tools.get(name);
    if (!tool) {
      log.warn('unknown tool', { tool: name });
      return fail(`Unknown tool "${name}".`);
    }

    const args = (rawArgs && typeof rawArgs === 'object' ? { ...rawArgs } : {}) as Record<string, unknown>;
    if (!tool.validate(args)) {
      const reason = this.#ajv.errorsText(tool.validate.errors);
      log.warn('invalid arguments', { tool: name, reason });
      return fail(`Invalid arguments: ${reason}. Fix the arguments and try again.`);
    }

    const now = this.#now();
    tool.calls = tool.calls.filter((t) => now - t < 60_000);
    if (tool.calls.length >= (tool.def.maxCallsPerMinute ?? DEFAULT_MAX_CALLS_PER_MINUTE)) {
      log.warn('rate limited', { tool: name });
      return fail('Too many calls to this tool in a short time. Wait a moment before trying again.');
    }
    tool.calls.push(now);

    const timeoutMs = tool.def.timeoutMs ?? this.#opts.defaultTimeoutMs;
    const controller = new AbortController();
    let timer: NodeJS.Timeout | undefined;
    const started = this.#now();
    try {
      const timeout = new Promise<never>((_, reject) => {
        timer = setTimeout(() => {
          controller.abort();
          reject(new Error(`timed out after ${timeoutMs} ms`));
        }, timeoutMs);
      });
      const text = await Promise.race([tool.def.handler(args, { signal: controller.signal, log }), timeout]);
      // Argument values can contain what a child asked for: only the keys are logged at info level.
      log.info('tool call', { tool: name, ms: this.#now() - started, argKeys: Object.keys(args) });
      log.debug('tool call args', { tool: name, args });
      return ok(text);
    } catch (err) {
      log.error('tool failed', { tool: name, ms: this.#now() - started, error: String(err) });
      return fail('The tool failed. Tell the child you could not do that right now and suggest something else.');
    } finally {
      clearTimeout(timer);
    }
  }
}
