import type { GatewayModule, ToolDef } from '../registry/registry.ts';
import { SpotifyError, trackLabel } from '../services/spotify.ts';
import type { SpotifyService } from '../services/spotify.ts';

const NOT_CONNECTED = 'Spotify is not connected. Tell the child a grown-up needs to connect it in the robot panel.';

/** Wraps a Spotify call so the LLM gets a sentence it can say instead of a generic failure. */
function guarded(spotify: SpotifyService, run: () => Promise<string>): Promise<string> {
  return (async () => {
    if (!(await spotify.connected())) return NOT_CONNECTED;
    try {
      return await run();
    } catch (err) {
      if (err instanceof SpotifyError) return `Spotify could not do it: ${err.message}.`;
      throw err;
    }
  })();
}

const deviceProp = { type: 'string', maxLength: 60, description: 'Optional Spotify device name.' };

export function spotifyModule(spotify: SpotifyService): GatewayModule {
  const tools: ToolDef[] = [
    {
      name: 'spotify_control',
      description:
        'Pauses, resumes, skips to the next or goes back to the previous song on Spotify. Use for "pausa a música", ' +
        '"próxima", "volta a música", "continua".',
      inputSchema: {
        type: 'object',
        properties: { action: { type: 'string', enum: ['pause', 'resume', 'next', 'previous'] }, device: deviceProp },
        required: ['action'],
        additionalProperties: false,
      },
      summarize: (args) =>
        ({ pause: 'Pausou o Spotify', resume: 'Continuou o Spotify', next: 'Próxima música', previous: 'Música anterior' })[
          String(args.action) as 'pause'
        ] ?? 'Spotify',
      handler: (args) =>
        guarded(spotify, async () => {
          const device = await spotify.control(args.action as 'pause', args.device ? String(args.device) : undefined);
          return `Done (${String(args.action)} on ${device}).`;
        }),
    },
    {
      name: 'spotify_volume',
      description: 'Sets the Spotify volume (0-100). There is a maximum set by the parents.',
      inputSchema: {
        type: 'object',
        properties: { percent: { type: 'integer', minimum: 0, maximum: 100 }, device: deviceProp },
        required: ['percent'],
        additionalProperties: false,
      },
      summarize: (args) => `Volume do Spotify em ${args.percent}%`,
      handler: (args) =>
        guarded(spotify, async () => {
          const value = await spotify.setVolume(Number(args.percent), args.device ? String(args.device) : undefined);
          return `Volume set to ${value}%.`;
        }),
    },
    {
      name: 'spotify_devices',
      description: 'Lists the Spotify devices of the family (speakers, TV, phones) and which one is playing.',
      inputSchema: { type: 'object', properties: {}, additionalProperties: false },
      summarize: () => 'Listou os aparelhos do Spotify',
      handler: () =>
        guarded(spotify, async () => {
          const devices = await spotify.devices();
          if (devices.length === 0) return 'No Spotify device is online right now.';
          return devices.map((d) => `- ${d.name} (${d.type})${d.is_active ? ' [playing here]' : ''}`).join('\n');
        }),
    },
    {
      name: 'spotify_transfer',
      description: 'Moves what Spotify is playing to another device of the family ("muda a música para a sala").',
      inputSchema: {
        type: 'object',
        properties: { device: { ...deviceProp, minLength: 1 } },
        required: ['device'],
        additionalProperties: false,
      },
      summarize: (args) => `Levou a música para "${args.device}"`,
      handler: (args) =>
        guarded(spotify, async () => `Music moved to ${await spotify.transfer(String(args.device))}.`),
    },
    {
      name: 'spotify_now_playing',
      description: 'Says what is playing on Spotify right now and where. Use for "que música é essa?".',
      inputSchema: { type: 'object', properties: {}, additionalProperties: false },
      summarize: () => 'Perguntou o que está tocando',
      handler: () =>
        guarded(spotify, async () => {
          const p = await spotify.playback();
          if (!p?.item) return 'Nothing is playing on Spotify right now.';
          return `${p.is_playing ? 'Playing' : 'Paused'}: ${trackLabel(p.item)} on ${p.device?.name ?? 'a device'}.`;
        }),
    },
  ];
  return { name: 'spotify', tools: () => tools };
}
