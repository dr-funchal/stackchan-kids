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
  /** One line for the activity feed of the panel, e.g. 'Página 2 de "O Dinossauro"'. */
  summarize?(args: Record<string, unknown>, result?: string): string;
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

export interface ToolInfo {
  name: string;
  description: string;
  source: string;
  enabled: boolean;
}

export interface CallEvent {
  tool: string;
  source: string;
  ok: boolean;
  ms: number;
  summary?: string;
}

interface Registered {
  def: ToolDef;
  source: string;
  validate: ValidateFunction;
  calls: number[];
}

const NAME_RE = /^[a-z][a-z0-9_]{2,63}$/;
const DEFAULT_MAX_CALLS_PER_MINUTE = 30;

const ok = (text: string): CallResult => ({ content: [{ type: 'text', text }] });
const fail = (text: string): CallResult => ({ content: [{ type: 'text', text }], isError: true });

/**
 * Every tool the gateway can offer, grouped by source (a module or an external MCP server). The panel turns
 * sources and single tools on and off; only enabled tools are listed to xiaozhi.me and callable.
 */
export class ToolRegistry {
  #tools = new Map<string, Registered>();
  // strict off and formats ignored: schemas from external MCP servers use keywords we do not need to enforce.
  #ajv = new Ajv({ coerceTypes: true, useDefaults: true, strict: false, validateFormats: false });
  #opts: RegistryOptions;
  #now: () => number;
  #disabledSources = new Set<string>();
  #disabledTools = new Set<string>();
  #callListeners: ((e: CallEvent) => void)[] = [];
  #changeListeners: (() => void)[] = [];

  constructor(opts: RegistryOptions) {
    this.#opts = opts;
    this.#now = opts.now ?? Date.now;
  }

  register(mod: GatewayModule): void {
    this.setSource(mod.name, mod.tools());
  }

  /** Replaces every tool of `source` at once (validated before anything changes). */
  setSource(source: string, defs: ToolDef[]): void {
    const next = new Map<string, Registered>();
    for (const def of defs) {
      if (!NAME_RE.test(def.name)) throw new Error(`invalid tool name "${def.name}" in ${source}`);
      const other = this.#tools.get(def.name);
      if (next.has(def.name) || (other && other.source !== source)) {
        throw new Error(`duplicate tool name "${def.name}" in ${source}`);
      }
      if (def.risk === 'restricted' && !this.#opts.allowRestricted) {
        this.#opts.log.warn('restricted tool not exposed', { tool: def.name, source });
        continue;
      }
      // Throws if the schema is malformed, instead of failing on the first child request.
      next.set(def.name, { def, source, validate: this.#ajv.compile(def.inputSchema), calls: [] });
    }
    for (const [name, tool] of this.#tools) if (tool.source === source) this.#tools.delete(name);
    for (const [name, tool] of next) this.#tools.set(name, tool);
    this.#opts.log.info('tools registered', { source, tools: [...next.keys()] });
    this.#changed();
  }

  removeSource(source: string): void {
    let removed = false;
    for (const [name, tool] of this.#tools) {
      if (tool.source === source) {
        this.#tools.delete(name);
        removed = true;
      }
    }
    if (removed) this.#changed();
  }

  setPolicy(policy: { disabledSources: Iterable<string>; disabledTools: Iterable<string> }): void {
    this.#disabledSources = new Set(policy.disabledSources);
    this.#disabledTools = new Set(policy.disabledTools);
    this.#changed();
  }

  #enabled(tool: Registered): boolean {
    return !this.#disabledSources.has(tool.source) && !this.#disabledTools.has(tool.def.name);
  }

  /** Everything registered, for the panel. */
  catalog(): ToolInfo[] {
    return [...this.#tools.values()].map((t) => ({
      name: t.def.name,
      description: t.def.description,
      source: t.source,
      enabled: this.#enabled(t),
    }));
  }

  /** What xiaozhi.me sees. */
  list(): { name: string; description: string; inputSchema: JsonSchema }[] {
    return [...this.#tools.values()]
      .filter((t) => this.#enabled(t))
      .map(({ def }) => ({ name: def.name, description: def.description, inputSchema: def.inputSchema }));
  }

  get size(): number {
    return this.list().length;
  }

  onCall(listener: (e: CallEvent) => void): void {
    this.#callListeners.push(listener);
  }

  onChange(listener: () => void): void {
    this.#changeListeners.push(listener);
  }

  #changed(): void {
    for (const l of this.#changeListeners) l();
  }

  /** Never throws: the LLM always gets a result it can explain to the child. */
  async call(name: string, rawArgs: unknown): Promise<CallResult> {
    const { log } = this.#opts;
    const tool = this.#tools.get(name);
    if (!tool || !this.#enabled(tool)) {
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
    const emit = (okResult: boolean, result?: string) => {
      let summary: string | undefined;
      try {
        summary = tool.def.summarize?.(args, result);
      } catch {
        // a summary is cosmetic
      }
      const event: CallEvent = { tool: name, source: tool.source, ok: okResult, ms: this.#now() - started };
      if (summary) event.summary = summary;
      for (const l of this.#callListeners) l(event);
    };
    try {
      const timeout = new Promise<never>((_, reject) => {
        timer = setTimeout(() => {
          controller.abort();
          reject(new Error(`timed out after ${timeoutMs} ms`));
        }, timeoutMs);
      });
      const text = await Promise.race([tool.def.handler(args, { signal: controller.signal, log }), timeout]);
      // Argument values can contain what a child asked for: only the keys go to the server log.
      log.info('tool call', { tool: name, ms: this.#now() - started, argKeys: Object.keys(args) });
      log.debug('tool call args', { tool: name, args });
      emit(true, text);
      return ok(text);
    } catch (err) {
      log.error('tool failed', { tool: name, ms: this.#now() - started, error: String(err) });
      emit(false);
      return fail('The tool failed. Tell the child you could not do that right now and suggest something else.');
    } finally {
      clearTimeout(timer);
    }
  }
}
