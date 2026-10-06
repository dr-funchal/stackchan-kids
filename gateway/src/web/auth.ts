import { createHash, randomBytes, scrypt, timingSafeEqual } from 'node:crypto';
import type { ScryptOptions } from 'node:crypto';
import { JsonFile } from '../store.ts';

const SCRYPT = { N: 32768, r: 8, p: 1 };
const SESSION_TTL_MS = 30 * 24 * 3600_000;
const IP_MAX_FAILURES = 5;
const IP_WINDOW_MS = 15 * 60_000;
const GLOBAL_MAX_FAILURES = 30; // per hour, from any address: slows down distributed guessing
const LOCK_MS = 15 * 60_000;
export const MIN_PASSWORD_LENGTH = 10;

function derive(password: string, salt: Buffer, opts: ScryptOptions & { N: number }): Promise<Buffer> {
  return new Promise((resolve, reject) =>
    scrypt(password, salt, 32, { ...opts, maxmem: 128 * opts.N * (opts.r ?? 8) * 2 }, (err, key) =>
      err ? reject(err) : resolve(key),
    ),
  );
}

export async function hashPassword(password: string): Promise<string> {
  const salt = randomBytes(16);
  const key = await derive(password, salt, SCRYPT);
  // ':' separators: '$' would be expanded by shells and by Docker Compose when reading .env
  return ['scrypt', SCRYPT.N, SCRYPT.r, SCRYPT.p, salt.toString('base64'), key.toString('base64')].join(':');
}

export async function verifyPassword(password: string, stored: string): Promise<boolean> {
  const [kind, n, r, p, salt, hash] = stored.split(':');
  if (kind !== 'scrypt' || !salt || !hash) return false;
  const expected = Buffer.from(hash, 'base64');
  const key = await derive(password, Buffer.from(salt, 'base64'), { N: Number(n), r: Number(r), p: Number(p) });
  return key.length === expected.length && timingSafeEqual(key, expected);
}

// A real hash, so a wrong e-mail costs the same time as a wrong password (no account probing by timing)
const DUMMY_HASH = hashPassword(randomBytes(16).toString('hex'));

interface SessionRecord {
  created: string;
  expires: number;
  lastSeen: number;
  ip: string;
  ua: string;
}

export interface AuthState {
  /** Set when the password is changed in the panel; overrides ADMIN_PASSWORD_HASH. */
  passwordHash?: string;
  sessions: Record<string, SessionRecord>;
}

export type LoginResult = { ok: true; token: string } | { ok: false; retryAfterSec?: number };

const tokenId = (token: string) => createHash('sha256').update(token).digest('base64url');

/** Single-user login for the panel: scrypt password, opaque session tokens stored only as hashes, lockouts. */
export class Auth {
  #file: JsonFile<AuthState>;
  #email: string;
  #envHash: string;
  #now: () => number;
  #failures = new Map<string, { count: number; first: number; lockedUntil: number }>();
  #global: number[] = [];
  #globalLockedUntil = 0;

  constructor(file: JsonFile<AuthState>, opts: { email: string; passwordHash: string; now?: () => number }) {
    this.#file = file;
    this.#email = opts.email.toLowerCase();
    this.#envHash = opts.passwordHash;
    this.#now = opts.now ?? Date.now;
  }

  get email(): string {
    return this.#email;
  }

  get enabled(): boolean {
    return Boolean(this.#email && (this.#file.get().passwordHash || this.#envHash));
  }

  #hash(): string {
    return this.#file.get().passwordHash || this.#envHash;
  }

  #locked(ip: string): number {
    const now = this.#now();
    const until = Math.max(this.#failures.get(ip)?.lockedUntil ?? 0, this.#globalLockedUntil);
    return until > now ? Math.ceil((until - now) / 1000) : 0;
  }

  #fail(ip: string): void {
    const now = this.#now();
    const f = this.#failures.get(ip);
    const entry = f && now - f.first < IP_WINDOW_MS ? f : { count: 0, first: now, lockedUntil: 0 };
    entry.count++;
    if (entry.count >= IP_MAX_FAILURES) entry.lockedUntil = now + LOCK_MS;
    this.#failures.set(ip, entry);
    this.#global = this.#global.filter((t) => now - t < 3600_000);
    this.#global.push(now);
    if (this.#global.length >= GLOBAL_MAX_FAILURES) this.#globalLockedUntil = now + LOCK_MS;
  }

  async login(email: string, password: string, meta: { ip: string; ua: string }): Promise<LoginResult> {
    const wait = this.#locked(meta.ip);
    if (wait) return { ok: false, retryAfterSec: wait };
    const emailOk = this.enabled && email.trim().toLowerCase() === this.#email;
    const passOk = await verifyPassword(password.slice(0, 256), emailOk ? this.#hash() : await DUMMY_HASH);
    if (!emailOk || !passOk) {
      this.#fail(meta.ip);
      const after = this.#locked(meta.ip);
      return after ? { ok: false, retryAfterSec: after } : { ok: false };
    }
    this.#failures.delete(meta.ip);
    const token = randomBytes(32).toString('base64url');
    const now = this.#now();
    await this.#file.update((s) => {
      for (const [id, rec] of Object.entries(s.sessions)) if (rec.expires < now) delete s.sessions[id];
      s.sessions[tokenId(token)] = {
        created: new Date(now).toISOString(),
        expires: now + SESSION_TTL_MS,
        lastSeen: now,
        ip: meta.ip,
        ua: meta.ua.slice(0, 160),
      };
    });
    return { ok: true, token };
  }

  /** Valid session? Slides the expiry (saved at most once an hour per session). */
  check(token: string | undefined): boolean {
    if (!token || !this.enabled) return false;
    const rec = this.#file.get().sessions[tokenId(token)];
    const now = this.#now();
    if (!rec || rec.expires < now) return false;
    if (now - rec.lastSeen > 3600_000) {
      rec.lastSeen = now;
      rec.expires = now + SESSION_TTL_MS;
      void this.#file.save();
    }
    return true;
  }

  async logout(token: string | undefined): Promise<void> {
    if (!token) return;
    await this.#file.update((s) => {
      delete s.sessions[tokenId(token)];
    });
  }

  async logoutOthers(token: string | undefined): Promise<number> {
    const keep = token ? tokenId(token) : '';
    let removed = 0;
    await this.#file.update((s) => {
      for (const id of Object.keys(s.sessions)) {
        if (id !== keep) {
          delete s.sessions[id];
          removed++;
        }
      }
    });
    return removed;
  }

  sessions(current: string | undefined): { created: string; lastSeen: string; ip: string; ua: string; current: boolean }[] {
    const cur = current ? tokenId(current) : '';
    return Object.entries(this.#file.get().sessions)
      .filter(([, r]) => r.expires > this.#now())
      .map(([id, r]) => ({ created: r.created, lastSeen: new Date(r.lastSeen).toISOString(), ip: r.ip, ua: r.ua, current: id === cur }))
      .sort((a, b) => b.lastSeen.localeCompare(a.lastSeen));
  }

  async changePassword(current: string, next: string, keepToken: string | undefined): Promise<'ok' | 'wrong' | 'weak'> {
    if (!(await verifyPassword(current.slice(0, 256), this.#hash()))) return 'wrong';
    if (next.length < MIN_PASSWORD_LENGTH || next.length > 256) return 'weak';
    const hash = await hashPassword(next);
    await this.#file.update((s) => {
      s.passwordHash = hash;
    });
    await this.logoutOthers(keepToken);
    return 'ok';
  }
}
