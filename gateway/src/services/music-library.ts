import { randomBytes } from 'node:crypto';
import { mkdir, rename, rm } from 'node:fs/promises';
import { join } from 'node:path';
import { transcodeToRobot } from '../audio/transcode.ts';
import type { Logger } from '../logger.ts';
import { JsonFile } from '../store.ts';
import { bestMatch, queryWords, slug, matchScore } from '../text.ts';

export interface Track {
  id: string;
  title: string;
  artist: string;
  seconds: number;
  bytes: number;
  addedAt: string;
}

const CODE_TTL_MS = 15 * 60_000;
const CODE_MAX_USES = 3; // the robot may retry a download

/**
 * Songs for the robot's own speaker. Uploads are converted once, at upload time, into the only format the robot
 * plays safely; the robot then fetches them through short-lived one-time links (no credentials in the firmware).
 */
export class MusicLibrary {
  #dir: string;
  #tmpDir: string;
  #log: Logger;
  #index: JsonFile<{ tracks: Track[] }>;
  #codes = new Map<string, { id: string; expires: number; uses: number }>();
  #queue: Promise<unknown> = Promise.resolve(); // one conversion at a time: the VPS is shared

  constructor(dataDir: string, log: Logger) {
    this.#dir = join(dataDir, 'musicas');
    this.#tmpDir = join(dataDir, 'tmp');
    this.#log = log;
    this.#index = new JsonFile(join(this.#dir, 'library.json'), () => ({ tracks: [] }));
  }

  async load(): Promise<void> {
    await mkdir(this.#dir, { recursive: true });
    await mkdir(this.#tmpDir, { recursive: true });
    await this.#index.load();
  }

  get tmpDir(): string {
    return this.#tmpDir;
  }

  list(): Track[] {
    return [...this.#index.get().tracks].sort((a, b) => a.title.localeCompare(b.title, 'pt-BR'));
  }

  get(id: string): Track | undefined {
    return this.#index.get().tracks.find((t) => t.id === id);
  }

  search(query: string): Track[] {
    const words = queryWords(query);
    if (words.length === 0) return this.list();
    return this.list()
      .map((t) => ({ t, score: matchScore(words, t.title, t.artist) }))
      .filter((x) => x.score > 0)
      .sort((a, b) => b.score - a.score)
      .map((x) => x.t);
  }

  find(query: string): Track | undefined {
    return this.get(query.trim()) ?? bestMatch(this.list(), query, (t) => t.title, (t) => t.artist);
  }

  path(id: string): string {
    return join(this.#dir, `${id}.ogg`);
  }

  /** Converts `source` (any format ffmpeg reads) and adds it. The source file is removed. */
  add(source: string, meta: { title: string; artist: string }): Promise<Track> {
    const job = this.#queue.then(async () => {
      const id = `${slug(meta.title, 40) || 'musica'}-${randomBytes(3).toString('hex')}`;
      const tmpOut = join(this.#tmpDir, `${id}.ogg`);
      try {
        const started = Date.now();
        const { seconds, bytes } = await transcodeToRobot(source, tmpOut);
        await rename(tmpOut, this.path(id));
        const track: Track = {
          id,
          title: meta.title.trim(),
          artist: meta.artist.trim(),
          seconds: Math.round(seconds),
          bytes,
          addedAt: new Date().toISOString(),
        };
        await this.#index.update((d) => d.tracks.push(track));
        this.#log.info('music added', { id, seconds: track.seconds, bytes, ms: Date.now() - started });
        return track;
      } finally {
        await rm(source, { force: true });
        await rm(tmpOut, { force: true });
      }
    });
    this.#queue = job.catch(() => undefined);
    return job;
  }

  async update(id: string, meta: { title?: string; artist?: string }): Promise<Track | undefined> {
    let found: Track | undefined;
    await this.#index.update((d) => {
      found = d.tracks.find((t) => t.id === id);
      if (!found) return;
      if (meta.title?.trim()) found.title = meta.title.trim();
      if (meta.artist !== undefined) found.artist = meta.artist.trim();
    });
    return found;
  }

  async remove(id: string): Promise<boolean> {
    let removed = false;
    await this.#index.update((d) => {
      const before = d.tracks.length;
      d.tracks = d.tracks.filter((t) => t.id !== id);
      removed = d.tracks.length !== before;
    });
    if (removed) await rm(this.path(id), { force: true });
    return removed;
  }

  /** A short capability code the robot turns into https://<gateway>/a/<code>. */
  createCode(id: string, now = Date.now()): string {
    for (const [code, entry] of this.#codes) if (entry.expires < now) this.#codes.delete(code);
    const code = randomBytes(15).toString('base64url');
    this.#codes.set(code, { id, expires: now + CODE_TTL_MS, uses: 0 });
    return code;
  }

  resolveCode(code: string, now = Date.now()): Track | undefined {
    const entry = this.#codes.get(code);
    if (!entry || entry.expires < now || entry.uses >= CODE_MAX_USES) return undefined;
    entry.uses++;
    return this.get(entry.id);
  }
}
