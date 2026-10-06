import { randomInt } from 'node:crypto';
import type { GatewayModule } from '../registry/registry.ts';

const WORDS = [
  'abacaxi',
  'girassol',
  'foguete',
  'pinguim',
  'jabuticaba',
  'tartaruga',
  'vagalume',
  'biscoito',
  'nuvem',
  'carrossel',
];

const EMPTY_ARGS = { type: 'object', properties: {}, additionalProperties: false } as const;

/**
 * Phase-3 smoke test. The secret word is random per call and only exists in the gateway log, so if the robot says it
 * the request really went robot -> xiaozhi.me -> gateway (the LLM cannot guess it, unlike the time of day).
 */
export function diagnosticsModule(opts: { timezone: string }): GatewayModule {
  return {
    name: 'diagnostics',
    tools: () => [
      {
        name: 'gateway_secret_word',
        description:
          'Returns a secret word from the Stack-Chan gateway. Call this ONLY when the user asks for the gateway secret word ' +
          'or asks to test the gateway. Then say the word exactly as returned.',
        inputSchema: { ...EMPTY_ARGS },
        maxCallsPerMinute: 10,
        summarize: (_args, result) => `Palavra secreta: ${/is: ([^.]+)\./.exec(result ?? '')?.[1] ?? '?'}`,
        async handler(_args, { log }) {
          const word = `${WORDS[randomInt(WORDS.length)]}-${randomInt(10, 100)}`;
          log.info('secret word issued', { word });
          return `The gateway secret word is: ${word}. Say it exactly like that.`;
        },
      },
      {
        name: 'gateway_get_time',
        description:
          'Returns the current date and time from the Stack-Chan gateway. Call this when the user asks what time it is ' +
          'or what day it is today.',
        inputSchema: { ...EMPTY_ARGS },
        summarize: () => 'Perguntou a hora',
        async handler() {
          const text = new Intl.DateTimeFormat('pt-BR', {
            dateStyle: 'full',
            timeStyle: 'short',
            timeZone: opts.timezone,
          }).format(new Date());
          return `Current date and time (${opts.timezone}): ${text}.`;
        },
      },
    ],
  };
}
