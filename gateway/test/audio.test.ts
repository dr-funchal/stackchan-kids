import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtemp, readFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { Readable } from 'node:stream';
import { after, before, test } from 'node:test';
import { parseOpus, readOggPackets, robotSafe, writeOpus } from '../src/audio/ogg.ts';
import { encodePcm } from '../src/audio/transcode.ts';

const has = (cmd: string) => {
  try {
    execFileSync(cmd, ['--version'], { stdio: 'ignore' });
    return true;
  } catch {
    return false;
  }
};
const HAS_OPUSENC = has('opusenc');

let dir: string;
before(async () => (dir = await mkdtemp(join(tmpdir(), 'gw-audio-'))));
after(() => rm(dir, { recursive: true, force: true }));

/** 16 kHz mono s16le tone, deliberately NOT a whole number of 60 ms frames. */
function tone(seconds: number): Buffer {
  const n = Math.round(16000 * seconds) + 123;
  const buf = Buffer.alloc(n * 2);
  for (let i = 0; i < n; i++) {
    const t = i / 16000;
    const s = 0.3 * Math.sin(2 * Math.PI * 440 * t) + 0.1 * Math.sin(2 * Math.PI * 880 * t);
    buf.writeInt16LE(Math.round(s * 32767), i * 2);
  }
  return buf;
}

test('encodes PCM into the exact shape the robot plays (config 11, code 0, 16 kHz mono)', { skip: !HAS_OPUSENC && 'opusenc not installed' }, async () => {
  const out = join(dir, 'tone.ogg');
  const { seconds, bytes } = await encodePcm(Readable.from([tone(3)]), out);
  const file = await readFile(out);
  assert.equal(bytes, file.length);
  const stream = parseOpus(file);
  assert.equal(stream.head[9], 1);
  assert.equal(stream.head.readUInt32LE(12), 16000);
  assert.ok(stream.packets.length >= 50);
  for (const p of stream.packets) {
    assert.equal(p[0]! >> 3, 11, 'TOC config must be 11 (SILK WB 60 ms)');
    assert.equal(p[0]! & 3, 0, 'one frame per packet');
  }
  assert.ok(Math.abs(seconds - 3) < 0.25, `duration ${seconds}`);
});

test('re-muxed Ogg has valid structure and survives a parse round trip', { skip: !HAS_OPUSENC && 'opusenc not installed' }, async () => {
  const out = join(dir, 'rt.ogg');
  await encodePcm(Readable.from([tone(4)]), out);
  const original = parseOpus(await readFile(out));
  const again = parseOpus(writeOpus(original, 7));
  assert.equal(again.packets.length, original.packets.length);
  assert.deepEqual(again.packets[5], original.packets[5]);
  // Pages: BOS first, EOS last
  const buf = writeOpus(original, 7);
  assert.equal(buf[5], 0x02);
  const lastPage = buf.lastIndexOf(Buffer.from('OggS'));
  assert.equal(buf[lastPage + 5], 0x04);
});

test('robotSafe drops odd trailing packets but rejects odd packets in the middle', () => {
  const head = Buffer.alloc(19);
  head.write('OpusHead', 0, 'latin1');
  head[9] = 1;
  head.writeUInt32LE(16000, 12);
  const tags = Buffer.from('OpusTags\0\0\0\0\0\0\0\0', 'latin1');
  const good = Buffer.from([11 << 3, 1, 2, 3]);
  const short20ms = Buffer.from([9 << 3, 1, 2]);
  const celt = Buffer.from([31 << 3, 1, 2]);
  const trimmed = robotSafe({ head, tags, packets: [good, good, short20ms] });
  assert.equal(trimmed.packets.length, 2);
  assert.throws(() => robotSafe({ head, tags, packets: [good, celt, good] }), /config 31/);
  const stereo = Buffer.from(head);
  stereo[9] = 2;
  assert.throws(() => robotSafe({ head: stereo, tags, packets: [good] }), /mono/);
  const rate48 = Buffer.from(head);
  rate48.writeUInt32LE(48000, 12);
  assert.throws(() => robotSafe({ head: rate48, tags, packets: [good] }), /16000/);
});

test('packets longer than 255 bytes use lacing correctly', () => {
  const big = Buffer.alloc(700, 7);
  big[0] = 11 << 3;
  const head = Buffer.alloc(19);
  head.write('OpusHead', 0, 'latin1');
  head[9] = 1;
  head.writeUInt32LE(16000, 12);
  const tags = Buffer.from('OpusTags\0\0\0\0\0\0\0\0', 'latin1');
  const packets = readOggPackets(writeOpus({ head, tags, packets: [big, big] }));
  assert.equal(packets.length, 4);
  assert.deepEqual(packets[2], big);
});

test('garbage is not mistaken for Ogg', () => {
  assert.throws(() => readOggPackets(Buffer.from('#EXTM3U\nfile:///etc/passwd\n')), /not an Ogg page/);
});
