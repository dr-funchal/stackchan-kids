import type { GatewayModule } from '../registry/registry.ts';
import { SpotifyError, trackLabel } from '../services/spotify.ts';
import type { SpotifyService } from '../services/spotify.ts';

const NOT_CONNECTED = 'Spotify is not connected. Tell the child a grown-up needs to connect it in the robot panel.';

const LABELS: Record<string, string> = {
  pause: 'Pausou o Spotify',
  resume: 'Continuou o Spotify',
  next: 'Próxima música',
  previous: 'Música anterior',
  volume: 'Volume do Spotify',
  devices: 'Listou os aparelhos do Spotify',
  transfer: 'Levou a música para outro aparelho',
  now_playing: 'Perguntou o que está tocando',
};

/**
 * One tool for every Spotify control (instead of five): each tool's description rides along with every turn of the
 * xiaozhi.me LLM, so fewer, shorter tools keep the robot's answers fast.
 */
export function spotifyModule(spotify: SpotifyService): GatewayModule {
  return {
    name: 'spotify',
    tools: () => [
      {
        name: 'spotify',
        description:
          'Controls Spotify on the family devices: pause, resume, next, previous, volume (percent), devices (list), ' +
          'transfer (to `device`), now_playing. To play something new use play_music.',
        inputSchema: {
          type: 'object',
          properties: {
            action: {
              type: 'string',
              enum: ['pause', 'resume', 'next', 'previous', 'volume', 'devices', 'transfer', 'now_playing'],
            },
            device: { type: 'string', maxLength: 60, description: 'Device name (for transfer, or to target one).' },
            percent: { type: 'integer', minimum: 0, maximum: 100, description: 'For volume.' },
          },
          required: ['action'],
          additionalProperties: false,
        },
        summarize: (args) =>
          args.action === 'volume' ? `Volume do Spotify em ${args.percent}%` : (LABELS[String(args.action)] ?? 'Spotify'),
        async handler(args) {
          if (!(await spotify.connected())) return NOT_CONNECTED;
          const action = String(args.action);
          const device = args.device ? String(args.device) : undefined;
          try {
            switch (action) {
              case 'pause':
              case 'resume':
              case 'next':
              case 'previous':
                return `Done (${action} on ${await spotify.control(action, device)}).`;
              case 'volume':
                if (args.percent === undefined) return 'Say which volume (0-100).';
                return `Volume set to ${await spotify.setVolume(Number(args.percent), device)}%.`;
              case 'devices': {
                const devices = await spotify.devices();
                if (devices.length === 0) return 'No Spotify device is online right now.';
                return devices.map((d) => `- ${d.name} (${d.type})${d.is_active ? ' [playing here]' : ''}`).join('\n');
              }
              case 'transfer':
                if (!device) return 'Say which device to move the music to.';
                return `Music moved to ${await spotify.transfer(device)}.`;
              default: {
                const p = await spotify.playback();
                if (!p?.item) return 'Nothing is playing on Spotify right now.';
                return `${p.is_playing ? 'Playing' : 'Paused'}: ${trackLabel(p.item)} on ${p.device?.name ?? 'a device'}.`;
              }
            }
          } catch (err) {
            if (err instanceof SpotifyError) return `Spotify could not do it: ${err.message}.`;
            throw err;
          }
        },
      },
    ],
  };
}
