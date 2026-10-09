import type { GatewayModule } from '../registry/registry.ts';
import type { MusicLibrary, Track } from '../services/music-library.ts';
import { SpotifyError } from '../services/spotify.ts';
import type { SpotifyService } from '../services/spotify.ts';
import type { SettingsFile } from '../settings.ts';
import { fold } from '../text.ts';

const ROBOT_WORDS = /\b(robo|robot|stack|stackchan|stack-chan|aqui|voce|alto ?falante|caixinha do robo)\b/;

export function robotInstructions(track: Track, code: string): string {
  return (
    `"${track.title}" is ready for the ROBOT's own speaker. Now call the robot tool self.gateway.play_audio with ` +
    `code "${code}" and name "${track.id}". Then tell the child in one short sentence that the song starts as soon as ` +
    'you stop talking, and that talking to you stops it. If the tool self.gateway.play_audio does not exist, say the ' +
    'robot needs a software update to play music on its own speaker.'
  );
}

export function musicModule(opts: {
  library: MusicLibrary;
  spotify: SpotifyService;
  settings: SettingsFile;
}): GatewayModule {
  const { library, spotify, settings } = opts;

  const onRobot = (query: string): string | undefined => {
    const track = library.find(query);
    return track ? robotInstructions(track, library.createCode(track.id)) : undefined;
  };

  const onSpotify = async (query: string, device?: string): Promise<string> => {
    try {
      const r = await spotify.playQuery(query, device);
      return `Now playing ${r.kind} "${r.title}" on the Spotify device "${r.device}". Tell the child briefly, in one sentence.`;
    } catch (err) {
      if (err instanceof SpotifyError) return `Could not play on Spotify: ${err.message}. Tell the child kindly.`;
      throw err;
    }
  };

  const notFound = (query: string) => {
    const some = library.list().slice(0, 8).map((t) => `"${t.title}"`).join(', ');
    return (
      `"${query}" is not in the robot's music library` +
      (some ? ` (it has: ${some})` : ' (it is empty)') +
      ' and Spotify is not connected. Tell the child kindly and offer one of the available songs.'
    );
  };

  return {
    name: 'music',
    tools: () => [
      {
        // Not play_music: xiaozhi.me has a built-in tool with that name ("Duplicate tool names" alert on the robot)
        name: 'family_music',
        description:
          'Plays a song, artist, album or playlist ("toca Galinha Pintadinha"). Optional `where`: "robot" or a Spotify ' +
          'device name ("sala", "TV"). Follow the instructions in the result.',
        inputSchema: {
          type: 'object',
          properties: {
            query: { type: 'string', minLength: 1, maxLength: 120, description: 'Song, artist, album or playlist.' },
            where: { type: 'string', maxLength: 60, description: 'Optional: "robot" or a Spotify device name.' },
          },
          required: ['query'],
          additionalProperties: false,
        },
        timeoutMs: 15_000,
        maxCallsPerMinute: 12,
        summarize: (args, result) => {
          const target = /ROBOT's own speaker/.test(result ?? '')
            ? 'no robô'
            : (/device "([^"]+)"/.exec(result ?? '')?.[1] ?? 'Spotify');
          return `Tocar "${args.query}" (${target})`;
        },
        async handler(args) {
          const query = String(args.query);
          const where = args.where ? String(args.where) : '';
          const connected = await spotify.connected();
          if (where) {
            if (ROBOT_WORDS.test(fold(where))) {
              return onRobot(query) ?? `"${query}" is not in the robot's own music library. ` +
                (connected ? 'Offer to play it on Spotify instead.' : notFound(query));
            }
            return connected ? onSpotify(query, where) : notFound(query);
          }
          const target = settings.get().musicDefaultTarget;
          if (target === 'spotify' && connected) return onSpotify(query);
          const robot = onRobot(query);
          if (robot) return robot;
          return connected ? onSpotify(query) : notFound(query);
        },
      },
    ],
  };
}
