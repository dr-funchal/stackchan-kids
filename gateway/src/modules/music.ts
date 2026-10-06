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
        name: 'play_music',
        description:
          'Plays music whenever someone asks to play or put on a song, artist, album or playlist ("toca Galinha ' +
          'Pintadinha", "coloca música na sala"). `where` is optional: "robot" for the robot\'s own speaker, or the ' +
          'name of a Spotify device of the family (e.g. "sala", "celular", "TV"). Without `where` it chooses by itself. ' +
          'Always follow the instructions in the result.',
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
      {
        name: 'music_list',
        description:
          'Lists the songs in the robot\'s own music library (they play on the robot\'s speaker with play_music). Call ' +
          'when someone asks which songs the robot knows or has.',
        inputSchema: {
          type: 'object',
          properties: { query: { type: 'string', maxLength: 120, description: 'Optional filter.' } },
          additionalProperties: false,
        },
        summarize: () => 'Listou as músicas do robô',
        async handler(args) {
          const tracks = library.search(String(args.query ?? '')).slice(0, 20);
          if (tracks.length === 0) return 'The robot\'s music library has no matching songs.';
          return `Songs on the robot (${tracks.length}):\n` +
            tracks.map((t) => `- "${t.title}"${t.artist ? ` (${t.artist})` : ''}`).join('\n');
        },
      },
    ],
  };
}
