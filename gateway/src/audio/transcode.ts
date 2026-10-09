import { spawn } from 'node:child_process';
import { readFile, rm, writeFile } from 'node:fs/promises';
import { Transform } from 'node:stream';
import type { Readable } from 'node:stream';
import { packetSeconds, parseOpus, robotSafe, writeOpus } from './ogg.ts';

const FRAME_BYTES = 960 * 2; // 60 ms of 16 kHz mono s16le
export const MAX_SECONDS = 15 * 60;

// Flags from CLAUDE.md: SILK wideband, voice signal, 60 ms frames. Verified by robotSafe() after encoding anyway.
const OPUSENC_ARGS = [
  '--quiet',
  '--raw',
  '--raw-rate', '16000',
  '--raw-chan', '1',
  '--raw-bits', '16',
  '--framesize', '60',
  '--bitrate', '24',
  '--set-ctl-int', '4024=3001',
  '--set-ctl-int', '4008=1103',
  '--set-ctl-int', '4004=1103',
];

// Small speaker: cut the bass it cannot play (it only distorts) and even out loudness between songs.
const FFMPEG_FILTER = 'highpass=f=120,loudnorm=I=-16:TP=-1.5:LRA=11';

export interface Encoded {
  seconds: number;
  bytes: number;
}

/** Pads the PCM stream to whole 60 ms frames. */
function framePadder(): Transform {
  let total = 0;
  return new Transform({
    transform(chunk: Buffer, _enc, done) {
      total += chunk.length;
      done(null, chunk);
    },
    flush(done) {
      const rest = total % FRAME_BYTES;
      done(null, rest ? Buffer.alloc(FRAME_BYTES - rest) : undefined);
    },
  });
}

function run(cmd: string, args: string[], timeoutMs: number) {
  const child = spawn(cmd, args, { stdio: ['pipe', 'pipe', 'pipe'] });
  let stderr = '';
  child.stderr.on('data', (d: Buffer) => {
    if (stderr.length < 2000) stderr += d.toString();
  });
  const timer = setTimeout(() => child.kill('SIGKILL'), timeoutMs);
  const done = new Promise<void>((resolve, reject) => {
    child.on('error', (err) => reject(new Error(`${cmd}: ${err.message}`)));
    child.on('close', (code, signal) => {
      clearTimeout(timer);
      if (code === 0) resolve();
      else reject(new Error(`${cmd} failed (${signal ?? code}): ${stderr.trim().split('\n').at(-1) ?? ''}`));
    });
  });
  return { child, done };
}

/** 16 kHz mono s16le PCM -> robot-safe Ogg Opus file. */
export async function encodePcm(pcm: Readable, output: string, timeoutMs = 180_000): Promise<Encoded> {
  const tmp = `${output}.enc.tmp`;
  const enc = run('opusenc', [...OPUSENC_ARGS, '-', tmp], timeoutMs);
  enc.child.stdin.on('error', () => {}); // opusenc exiting early surfaces through `done`
  pcm.pipe(framePadder()).pipe(enc.child.stdin);
  try {
    await enc.done;
    const stream = robotSafe(parseOpus(await readFile(tmp)));
    const ogg = writeOpus(stream);
    await writeFile(output, ogg);
    return { seconds: packetSeconds(stream.packets.length), bytes: ogg.length };
  } finally {
    await rm(tmp, { force: true });
  }
}

/** Any audio file ffmpeg understands -> robot-safe Ogg Opus. */
export async function transcodeToRobot(input: string, output: string, timeoutMs = 180_000): Promise<Encoded> {
  const dec = run(
    'ffmpeg',
    ['-hide_banner', '-loglevel', 'error', '-nostdin', '-i', input, '-vn', '-af', FFMPEG_FILTER,
      '-ac', '1', '-ar', '16000', '-t', String(MAX_SECONDS), '-f', 's16le', '-'],
    timeoutMs,
  );
  dec.child.stdin.end();
  const [encoded] = await Promise.all([encodePcm(dec.child.stdout, output, timeoutMs), dec.done]);
  return encoded;
}
