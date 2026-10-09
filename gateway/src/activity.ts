import { JsonFile } from './store.ts';

export type ActivityKind = 'tool' | 'robot' | 'system' | 'auth' | 'error';

export interface ActivityEntry {
  id: number;
  ts: string;
  kind: ActivityKind;
  title: string;
  detail?: string;
  ok?: boolean;
}

const MAX_ENTRIES = 300;
const SAVE_DELAY_MS = 5000;

/** What happened lately, for the panel (live via SSE). Kept on disk so a restart does not wipe the history. */
export class ActivityFeed {
  #file: JsonFile<{ entries: ActivityEntry[] }>;
  #listeners = new Set<(e: ActivityEntry) => void>();
  #saveTimer: NodeJS.Timeout | undefined;
  #nextId = 1;

  constructor(path: string) {
    this.#file = new JsonFile(path, () => ({ entries: [] }));
  }

  async load(): Promise<void> {
    const { entries } = await this.#file.load();
    this.#nextId = (entries.at(-1)?.id ?? 0) + 1;
  }

  add(entry: Omit<ActivityEntry, 'id' | 'ts'>): ActivityEntry {
    const full: ActivityEntry = { id: this.#nextId++, ts: new Date().toISOString(), ...entry };
    const data = this.#file.get();
    data.entries.push(full);
    if (data.entries.length > MAX_ENTRIES) data.entries.splice(0, data.entries.length - MAX_ENTRIES);
    for (const l of this.#listeners) l(full);
    this.#saveTimer ??= setTimeout(() => {
      this.#saveTimer = undefined;
      void this.#file.save();
    }, SAVE_DELAY_MS);
    return full;
  }

  list(limit = 100): ActivityEntry[] {
    return this.#file.get().entries.slice(-limit).reverse();
  }

  lastOf(kind: ActivityKind): ActivityEntry | undefined {
    const entries = this.#file.get().entries;
    for (let i = entries.length - 1; i >= 0; i--) if (entries[i]!.kind === kind) return entries[i];
    return undefined;
  }

  async clear(): Promise<void> {
    await this.#file.update((d) => {
      d.entries = [];
    });
  }

  subscribe(listener: (e: ActivityEntry) => void): () => void {
    this.#listeners.add(listener);
    return () => this.#listeners.delete(listener);
  }

  async flush(): Promise<void> {
    clearTimeout(this.#saveTimer);
    this.#saveTimer = undefined;
    await this.#file.save();
  }
}
