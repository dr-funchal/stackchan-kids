import assert from 'node:assert/strict';
import { test } from 'node:test';
import { silentLogger } from '../src/logger.ts';
import { ToolRegistry } from '../src/registry/registry.ts';
import type { ToolDef } from '../src/registry/registry.ts';

const echo = (extra: Partial<ToolDef> = {}): ToolDef => ({
  name: 'echo_tool',
  description: 'echo',
  inputSchema: {
    type: 'object',
    properties: { text: { type: 'string' }, times: { type: 'integer', minimum: 1, maximum: 3, default: 1 } },
    required: ['text'],
    additionalProperties: false,
  },
  handler: async (args) => String(args.text).repeat(Number(args.times)),
  ...extra,
});

const make = (opts: Partial<ConstructorParameters<typeof ToolRegistry>[0]> = {}) =>
  new ToolRegistry({ log: silentLogger, defaultTimeoutMs: 200, allowRestricted: false, ...opts });

test('calls a tool, applies defaults and coerces types', async () => {
  const reg = make();
  reg.register({ name: 'm', tools: () => [echo()] });
  assert.equal((await reg.call('echo_tool', { text: 'ab' })).content[0]!.text, 'ab');
  assert.equal((await reg.call('echo_tool', { text: 'ab', times: '2' })).content[0]!.text, 'abab');
});

test('rejects invalid arguments without running the handler', async () => {
  const reg = make();
  let ran = false;
  reg.register({
    name: 'm',
    tools: () => [echo({ handler: async () => ((ran = true), 'x') })],
  });
  for (const args of [{}, { text: 'a', times: 9 }, { text: 'a', extra: 1 }]) {
    const res = await reg.call('echo_tool', args);
    assert.equal(res.isError, true);
    assert.match(res.content[0]!.text, /Invalid arguments/);
  }
  assert.equal(ran, false);
});

test('unknown tool is an error result, not a throw', async () => {
  const res = await make().call('nope_tool', {});
  assert.equal(res.isError, true);
});

test('handler timeout aborts the signal and returns a friendly error', async () => {
  const reg = make({ defaultTimeoutMs: 30 });
  let aborted = false;
  reg.register({
    name: 'm',
    tools: () => [
      echo({
        handler: (_a, { signal }) =>
          new Promise((resolve) => {
            signal.addEventListener('abort', () => (aborted = true));
            setTimeout(() => resolve('late'), 500).unref();
          }),
      }),
    ],
  });
  const res = await reg.call('echo_tool', { text: 'a' });
  assert.equal(res.isError, true);
  assert.match(res.content[0]!.text, /failed/);
  assert.equal(aborted, true);
});

test('handler exceptions never leak details to the model', async () => {
  const reg = make();
  reg.register({
    name: 'm',
    tools: () => [echo({ handler: async () => Promise.reject(new Error('secret db password')) })],
  });
  const res = await reg.call('echo_tool', { text: 'a' });
  assert.equal(res.isError, true);
  assert.doesNotMatch(res.content[0]!.text, /secret/);
});

test('rate limit per tool, window slides after a minute', async () => {
  let t = 0;
  const reg = make({ now: () => t });
  reg.register({ name: 'm', tools: () => [echo({ maxCallsPerMinute: 2 })] });
  assert.equal((await reg.call('echo_tool', { text: 'a' })).isError, undefined);
  assert.equal((await reg.call('echo_tool', { text: 'a' })).isError, undefined);
  assert.equal((await reg.call('echo_tool', { text: 'a' })).isError, true);
  t = 61_000;
  assert.equal((await reg.call('echo_tool', { text: 'a' })).isError, undefined);
});

test('restricted tools are hidden unless explicitly allowed', () => {
  const restricted = { name: 'm', tools: () => [echo({ risk: 'restricted' as const })] };
  const hidden = make();
  hidden.register(restricted);
  assert.equal(hidden.list().length, 0);
  const allowed = make({ allowRestricted: true });
  allowed.register(restricted);
  assert.equal(allowed.list().length, 1);
});

test('duplicate and badly named tools fail at startup', () => {
  const reg = make();
  reg.register({ name: 'a', tools: () => [echo()] });
  assert.throws(() => reg.register({ name: 'b', tools: () => [echo()] }), /duplicate/);
  assert.throws(() => reg.register({ name: 'c', tools: () => [echo({ name: 'Bad.Name' })] }), /invalid tool name/);
});
