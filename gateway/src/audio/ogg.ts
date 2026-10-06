import { randomInt } from 'node:crypto';

/**
 * Minimal Ogg Opus reader/writer. The robot's decoder only survives one exact shape (CLAUDE.md): mono, 16 kHz,
 * SILK wideband, one 60 ms frame per packet (TOC config 11, code 0). opusenc always ends with a shorter flush packet,
 * so files are re-muxed keeping only packets of that shape, and anything else in the middle is rejected.
 */

export const ROBOT_TOC_CONFIG = 11; // SILK-only, wideband, 60 ms
const SAMPLES_48K_PER_PACKET = 2880; // 60 ms at 48 kHz (Ogg Opus granule unit)

export interface OpusStream {
  head: Buffer;
  tags: Buffer;
  packets: Buffer[];
}

export function readOggPackets(buf: Buffer): Buffer[] {
  const packets: Buffer[] = [];
  let partial: Buffer[] = [];
  let off = 0;
  while (off < buf.length) {
    if (buf.length - off < 27 || buf.toString('latin1', off, off + 4) !== 'OggS') {
      throw new Error(`not an Ogg page at byte ${off}`);
    }
    const nseg = buf[off + 26]!;
    const table = buf.subarray(off + 27, off + 27 + nseg);
    let p = off + 27 + nseg;
    for (const lace of table) {
      if (p + lace > buf.length) throw new Error('truncated Ogg page');
      partial.push(buf.subarray(p, p + lace));
      p += lace;
      if (lace < 255) {
        packets.push(Buffer.concat(partial));
        partial = [];
      }
    }
    off = p;
  }
  return packets;
}

export function parseOpus(buf: Buffer): OpusStream {
  const [head, tags, ...packets] = readOggPackets(buf);
  if (!head || head.toString('latin1', 0, 8) !== 'OpusHead') throw new Error('missing OpusHead');
  if (!tags || tags.toString('latin1', 0, 8) !== 'OpusTags') throw new Error('missing OpusTags');
  return { head, tags, packets };
}

/** Throws unless the stream is exactly what the robot can play; returns it without trailing odd packets. */
export function robotSafe(stream: OpusStream): OpusStream {
  const channels = stream.head[9];
  const inputRate = stream.head.readUInt32LE(12);
  if (channels !== 1) throw new Error(`expected mono, got ${channels} channels`);
  // The robot configures its decoder from this field
  if (inputRate !== 16000) throw new Error(`expected 16000 Hz in OpusHead, got ${inputRate}`);
  const packets = [...stream.packets];
  while (packets.length && !isRobotPacket(packets.at(-1)!)) packets.pop();
  const bad = packets.findIndex((p) => !isRobotPacket(p));
  if (bad >= 0) {
    const toc = packets[bad]![0]!;
    throw new Error(`packet ${bad} has TOC config ${toc >> 3} code ${toc & 3}; the robot needs config 11 code 0`);
  }
  if (packets.length === 0) throw new Error('no audio');
  return { ...stream, packets };
}

function isRobotPacket(p: Buffer): boolean {
  return p.length > 1 && p[0]! >> 3 === ROBOT_TOC_CONFIG && (p[0]! & 3) === 0;
}

const CRC_TABLE = (() => {
  const t = new Uint32Array(256);
  for (let i = 0; i < 256; i++) {
    let r = i << 24;
    for (let j = 0; j < 8; j++) r = r & 0x80000000 ? (r << 1) ^ 0x04c11db7 : r << 1;
    t[i] = r >>> 0;
  }
  return t;
})();

function oggCrc(page: Buffer): number {
  let crc = 0;
  for (const byte of page) crc = ((crc << 8) ^ CRC_TABLE[((crc >>> 24) ^ byte) & 0xff]!) >>> 0;
  return crc;
}

function page(packets: Buffer[], flags: number, granule: bigint, serial: number, seq: number): Buffer {
  const lacing: number[] = [];
  for (const p of packets) {
    let n = p.length;
    while (n >= 255) {
      lacing.push(255);
      n -= 255;
    }
    lacing.push(n);
  }
  if (lacing.length > 255) throw new Error('too many segments for one Ogg page');
  const header = Buffer.alloc(27 + lacing.length);
  header.write('OggS', 0, 'latin1');
  header[5] = flags;
  header.writeBigUInt64LE(granule, 6);
  header.writeUInt32LE(serial, 14);
  header.writeUInt32LE(seq, 18);
  header[26] = lacing.length;
  Buffer.from(lacing).copy(header, 27);
  const out = Buffer.concat([header, ...packets]);
  out.writeUInt32LE(oggCrc(out), 22);
  return out;
}

export function writeOpus(stream: OpusStream, packetsPerPage = 50): Buffer {
  const serial = randomInt(1, 0x7fffffff);
  const pages = [page([stream.head], 0x02, 0n, serial, 0), page([stream.tags], 0, 0n, serial, 1)];
  let seq = 2;
  for (let i = 0; i < stream.packets.length; i += packetsPerPage) {
    const chunk = stream.packets.slice(i, i + packetsPerPage);
    const last = i + packetsPerPage >= stream.packets.length;
    const granule = BigInt((i + chunk.length) * SAMPLES_48K_PER_PACKET);
    pages.push(page(chunk, last ? 0x04 : 0, granule, serial, seq++));
  }
  return Buffer.concat(pages);
}

export const packetSeconds = (packets: number): number => packets * 0.06;
