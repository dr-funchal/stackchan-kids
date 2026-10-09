import type { GatewayModule } from '../registry/registry.ts';
import type { MusicLibrary, Track } from '../services/music-library.ts';

export function robotInstructions(track: Track, code: string): string {
  return (
    `"${track.title}" is ready for the robot's own speaker. Now call the robot tool self.gateway.play_audio with ` +
    `code "${code}" and name "${track.id}". Then tell the child in one short sentence that the song starts as soon as ` +
    'you stop talking, and that talking to you stops it. If the tool self.gateway.play_audio does not exist, say the ' +
    'robot needs a software update to play music.'
  );
}

/** Songs from the family library (uploaded in the panel), played on the robot's own speaker. */
export function musicModule(opts: { library: MusicLibrary }): GatewayModule {
  const { library } = opts;
  return {
    name: 'music',
    tools: () => [
      {
        // Not play_music: xiaozhi.me has a built-in tool with that name ("Duplicate tool names" alert on the robot)
        name: 'family_music',
        description:
          'Plays a song from the family music library on the robot\'s speaker ("toca Galinha Pintadinha"). ' +
          'Follow the instructions in the result.',
        inputSchema: {
          type: 'object',
          properties: {
            query: { type: 'string', minLength: 1, maxLength: 120, description: 'Song or artist.' },
          },
          required: ['query'],
          additionalProperties: false,
        },
        maxCallsPerMinute: 12,
        summarize: (args, result) =>
          /ready for the robot/.test(result ?? '') ? `Tocar "${args.query}" no robô` : `Música não encontrada: "${args.query}"`,
        async handler(args) {
          const query = String(args.query);
          const track = library.find(query);
          if (track) return robotInstructions(track, library.createCode(track.id));
          const some = library.list().slice(0, 8).map((t) => `"${t.title}"`).join(', ');
          return some
            ? `"${query}" is not in the family music library. It has: ${some}. Offer one of these.`
            : 'The family music library is empty. Tell the child a grown-up can add songs in the robot panel.';
        },
      },
    ],
  };
}
