import { rename, writeFile } from 'node:fs/promises';
import type { Logger } from './logger.ts';

export type State = 'standby' | 'connecting' | 'connected' | 'backoff';

export interface Status {
  state: State;
  since: string;
  ts: string;
  detail?: string;
}

/** Writes a small status file the Docker healthcheck reads; the heartbeat proves the event loop is alive. */
export class StatusReporter {
  #file: string;
  #log: Logger;
  #state: State = 'standby';
  #since = new Date().toISOString();
  #detail: string | undefined;
  #timer: NodeJS.Timeout | undefined;
  #pending: Promise<void> = Promise.resolve(); // writes share one .tmp file, so they must not overlap

  constructor(file: string, log: Logger) {
    this.#file = file;
    this.#log = log;
  }

  get state(): State {
    return this.#state;
  }

  set(state: State, detail?: string): void {
    if (state !== this.#state) this.#since = new Date().toISOString();
    this.#state = state;
    this.#detail = detail;
    void this.#write();
  }

  start(intervalMs = 15000): void {
    void this.#write();
    this.#timer = setInterval(() => void this.#write(), intervalMs);
  }

  stop(): void {
    if (this.#timer) clearInterval(this.#timer);
  }

  #write(): Promise<void> {
    this.#pending = this.#pending.then(() => this.#writeNow());
    return this.#pending;
  }

  async #writeNow(): Promise<void> {
    const status: Status = { state: this.#state, since: this.#since, ts: new Date().toISOString() };
    if (this.#detail) status.detail = this.#detail;
    try {
      const tmp = this.#file + '.tmp';
      await writeFile(tmp, JSON.stringify(status));
      await rename(tmp, this.#file);
    } catch (err) {
      this.#log.warn('could not write status file', { error: String(err) });
    }
  }
}
