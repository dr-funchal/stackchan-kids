import { createCipheriv, createDecipheriv, randomBytes } from 'node:crypto';
import { mkdir, readFile, rename, writeFile } from 'node:fs/promises';
import { dirname } from 'node:path';

/** A JSON document on disk, written atomically (tmp + rename) and serialized so writes never interleave. */
export class JsonFile<T> {
  #path: string;
  #fallback: () => T;
  #value: T | undefined;
  #pending: Promise<void> = Promise.resolve();

  constructor(path: string, fallback: () => T) {
    this.#path = path;
    this.#fallback = fallback;
  }

  async load(): Promise<T> {
    if (this.#value !== undefined) return this.#value;
    try {
      this.#value = { ...this.#fallback(), ...JSON.parse(await readFile(this.#path, 'utf8')) } as T;
    } catch {
      this.#value = this.#fallback();
    }
    return this.#value;
  }

  get(): T {
    if (this.#value === undefined) throw new Error(`${this.#path} not loaded`);
    return this.#value;
  }

  async update(mutate: (value: T) => void): Promise<T> {
    const value = await this.load();
    mutate(value);
    await this.save();
    return value;
  }

  save(): Promise<void> {
    const snapshot = JSON.stringify(this.#value, null, 2);
    this.#pending = this.#pending.then(async () => {
      await mkdir(dirname(this.#path), { recursive: true });
      const tmp = `${this.#path}.tmp`;
      await writeFile(tmp, snapshot, { mode: 0o600 });
      await rename(tmp, this.#path);
    });
    return this.#pending;
  }
}

/**
 * Secrets at rest (Spotify tokens, API keys of external MCP servers): AES-256-GCM with SECRETS_KEY from .env, so a
 * copy of the data folder alone does not leak them.
 */
export class SecretBox {
  #key: Buffer;

  constructor(base64Key: string) {
    const key = Buffer.from(base64Key, 'base64');
    if (key.length !== 32) throw new Error('SECRETS_KEY must be 32 random bytes in base64 (openssl rand -base64 32)');
    this.#key = key;
  }

  seal(plain: string): string {
    const iv = randomBytes(12);
    const cipher = createCipheriv('aes-256-gcm', this.#key, iv);
    const data = Buffer.concat([cipher.update(plain, 'utf8'), cipher.final()]);
    return ['v1', iv.toString('base64'), cipher.getAuthTag().toString('base64'), data.toString('base64')].join('.');
  }

  open(sealed: string): string {
    const [version, iv, tag, data] = sealed.split('.');
    if (version !== 'v1' || !iv || !tag || !data) throw new Error('unknown secret format');
    const decipher = createDecipheriv('aes-256-gcm', this.#key, Buffer.from(iv, 'base64'));
    decipher.setAuthTag(Buffer.from(tag, 'base64'));
    return Buffer.concat([decipher.update(Buffer.from(data, 'base64')), decipher.final()]).toString('utf8');
  }
}

/** Named secrets in one encrypted file. Values never leave the server through the API. */
export class SecretStore {
  #file: JsonFile<Record<string, string>>;
  #box: SecretBox | undefined;

  constructor(path: string, box: SecretBox | undefined) {
    this.#file = new JsonFile(path, () => ({}));
    this.#box = box;
  }

  get available(): boolean {
    return this.#box !== undefined;
  }

  async get(name: string): Promise<string | undefined> {
    const sealed = (await this.#file.load())[name];
    if (!sealed || !this.#box) return undefined;
    try {
      return this.#box.open(sealed);
    } catch {
      return undefined;
    }
  }

  async set(name: string, value: string | undefined): Promise<void> {
    if (!this.#box) throw new Error('SECRETS_KEY is not configured');
    const box = this.#box;
    await this.#file.update((all) => {
      if (value === undefined) delete all[name];
      else all[name] = box.seal(value);
    });
  }

  async deletePrefix(prefix: string): Promise<void> {
    await this.#file.update((all) => {
      for (const key of Object.keys(all)) if (key.startsWith(prefix)) delete all[key];
    });
  }
}
