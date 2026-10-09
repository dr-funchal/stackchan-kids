import { mkdir, readdir, readFile, rm, stat, writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import type { GatewayModule } from '../registry/registry.ts';
import { bestMatch, fold, queryWords, slug } from '../text.ts';

// ~160 words per tool call. Every page is a round trip through xiaozhi.me's LLM (the pause between pages), and the
// conversation it re-reads grows with each page, so fewer, larger pages read more smoothly than the robot's 750.
const PAGE_CHARS = 1000;
const SESSION_MS = 30 * 60_000;
const MAX_FILE_BYTES = 256 * 1024;
const MAX_LISTED = 10;

export interface Story {
  id: string;
  title: string;
  pages: string[];
  searchText: string;
  bytes: number;
  updatedAt: string;
}

/** Splits at sentence ends so the narrator never stops mid-sentence. */
export function paginate(body: string, pageChars = PAGE_CHARS): string[] {
  const text = body.replace(/\s+/g, ' ').trim();
  const pages: string[] = [];
  let start = 0;
  while (start < text.length) {
    let end = Math.min(text.length, start + pageChars);
    if (end < text.length) {
      for (let i = end - 1; i > start + pageChars / 2; i--) {
        if ('.!?'.includes(text[i]!)) {
          end = i + 1;
          break;
        }
      }
    }
    pages.push(text.slice(start, end).trim());
    start = end;
  }
  return pages;
}

function parseStoryFile(name: string, raw: string): { id: string; title: string; body: string } {
  let body = raw.replace(/^﻿/, '');
  const base = name.slice(0, -4);
  let title = base;
  const first = body.split('\n', 1)[0]!;
  if (first.startsWith('# ')) {
    title = first.slice(2).trim();
    body = body.slice(first.length);
  }
  // Pasted stories often repeat the title as their first line: the narrator would say it twice
  const lead = body.trimStart().split('\n', 1)[0]!.trim();
  if (lead && fold(lead) === fold(title)) body = body.trimStart().slice(lead.length);
  return { id: slug(base, 100), title, body };
}

/** Reads <dir>/*.txt on every call, so adding a story needs no restart. The id is derived, never a user-given path. */
export async function loadStories(dir: string): Promise<Story[]> {
  let names: string[];
  try {
    names = (await readdir(dir)).filter((n) => n.toLowerCase().endsWith('.txt')).sort();
  } catch {
    return [];
  }
  const stories = new Map<string, Story>();
  for (const name of names) {
    const path = join(dir, name);
    const info = await stat(path);
    if (!info.isFile() || info.size > MAX_FILE_BYTES) continue;
    const { id, title, body } = parseStoryFile(name, await readFile(path, 'utf8'));
    const pages = paginate(body);
    if (!id || pages.length === 0 || stories.has(id)) continue;
    stories.set(id, {
      id,
      title,
      pages,
      searchText: fold(`${title} ${body}`),
      bytes: info.size,
      updatedAt: info.mtime.toISOString(),
    });
  }
  return [...stories.values()];
}

/** Best match by id, then by words in the title (worth more) or the text. Never touches the filesystem with user input. */
export function findStory(stories: Story[], query: string): Story | undefined {
  return stories.find((s) => s.id === query.trim()) ?? bestMatch(stories, query, (s) => s.title, (s) => s.searchText);
}

/** File name for a story id: only ids that loadStories produced are accepted. */
async function fileOf(dir: string, id: string): Promise<string | undefined> {
  let names: string[];
  try {
    names = await readdir(dir);
  } catch {
    return undefined;
  }
  return names.find((n) => n.toLowerCase().endsWith('.txt') && slug(n.slice(0, -4), 100) === id);
}

export async function saveStory(dir: string, input: { id?: string; title: string; text: string }): Promise<string> {
  const title = input.title.trim();
  const text = input.text.replace(/\r\n/g, '\n').trim();
  if (!title || !text) throw new Error('title and text are required');
  if (Buffer.byteLength(text) > MAX_FILE_BYTES - 1024) throw new Error('story too long');
  await mkdir(dir, { recursive: true });
  let name = input.id ? await fileOf(dir, input.id) : undefined;
  if (!name) {
    const base = slug(title, 80) || 'historia';
    name = `${base}.txt`;
    for (let i = 2; await fileOf(dir, slug(name.slice(0, -4), 100)); i++) name = `${base}-${i}.txt`;
  }
  await writeFile(join(dir, name), `# ${title}\n\n${text}\n`, { mode: 0o644 });
  return slug(name.slice(0, -4), 100);
}

export async function deleteStory(dir: string, id: string): Promise<boolean> {
  const name = await fileOf(dir, id);
  if (!name) return false;
  await rm(join(dir, name), { force: true });
  return true;
}

export async function storyText(dir: string, id: string): Promise<string | undefined> {
  const name = await fileOf(dir, id);
  return name ? parseStoryFile(name, await readFile(join(dir, name), 'utf8')).body.trim() : undefined;
}

const RULES =
  'READ-ALOUD RULES: read each page EXACTLY as written, word for word, with an expressive storyteller voice for small ' +
  'children: do not summarize, skip, add or explain anything. If a child interrupts, answer briefly and then continue.';

export const storiesDir = (dataDir: string): string => join(dataDir, 'historias');

export function storiesModule(opts: { dataDir: string; now?: () => number }): GatewayModule {
  const dir = storiesDir(opts.dataDir);
  const now = opts.now ?? Date.now;
  // The story being read and the last page served: lets the LLM just ask for "the next page" and stops it skipping
  let session: { id: string; page: number; at: number } | undefined;

  return {
    name: 'stories',
    tools: () => [
      {
        name: 'library_search',
        description:
          'ONLINE STORY LIBRARY with stories that are NOT in self.story.list. When a child asks for a story by name or ' +
          'theme, search here too before saying it does not exist. Then read it with library_read_page.',
        inputSchema: {
          type: 'object',
          properties: { query: { type: 'string', maxLength: 120, description: 'Title or theme. Empty lists all.' } },
          additionalProperties: false,
        },
        summarize: (args) => (args.query ? `Procurou história: "${args.query}"` : 'Listou as histórias'),
        async handler(args) {
          const all = await loadStories(dir);
          if (all.length === 0) {
            return 'The online library is empty. Make up a short original story yourself and tell it to the child.';
          }
          const words = queryWords(String(args.query ?? ''));
          const matches = words.length ? all.filter((s) => words.some((w) => s.searchText.includes(w))) : all;
          const shown = (matches.length ? matches : all).slice(0, MAX_LISTED);
          const lines = shown.map((s) => `- "${s.title}" (id: ${s.id}, ${s.pages.length} pages)`);
          const note = matches.length
            ? 'Found in the online library. Read one with library_read_page (story = title or id).'
            : 'No match in the online library. These are available; offer one or make up a story yourself.';
          return `${note}\n${lines.join('\n')}`;
        },
      },
      {
        name: 'library_read_page',
        description:
          'Reads the next page of a story from the ONLINE STORY LIBRARY (`story` = title, part of it, or id). Call it ' +
          'again right after reading each page aloud, without asking, until it says FIM.',
        inputSchema: {
          type: 'object',
          properties: {
            story: { type: 'string', minLength: 1, maxLength: 120, description: 'Title, part of the title, or id.' },
            page: { type: 'integer', minimum: 1, maximum: 500, description: 'Optional; omit to get the next page.' },
          },
          required: ['story'],
          additionalProperties: false,
        },
        summarize: (args, result) => {
          const title = /STORY "([^"]+)"/.exec(result ?? '')?.[1] ?? String(args.story);
          const page = /PAGE (\d+)/.exec(result ?? '')?.[1] ?? args.page ?? '?';
          return `Leu a página ${page} de "${title}"`;
        },
        async handler(args) {
          const all = await loadStories(dir);
          const story = findStory(all, String(args.story));
          if (!story) {
            const titles = all.map((s) => `"${s.title}"`).join(', ') || 'none';
            return `That story is not in the online library. Available: ${titles}.`;
          }
          const t = now();
          const current = session && session.id === story.id && t - session.at < SESSION_MS ? session : undefined;
          let page = args.page === undefined ? (current ? current.page + 1 : 1) : Number(args.page);
          // The LLM sometimes jumps ahead (3 -> 5 was seen): never skip a page of the story being read
          if (current && page > current.page + 1) page = current.page + 1;
          const total = story.pages.length;
          if (page > total) {
            session = undefined;
            return `"${story.title}" is over (FIM): it has ${total} pages.`;
          }
          session = { id: story.id, page, at: t };
          const text = story.pages[page - 1]!;
          const end =
            page === total
              ? '[FIM - last page. After reading it, softly ask the children what they liked most.]'
              : `NEXT: right after reading this page aloud, call library_read_page with story "${story.id}".`;
          // The rules go only with the first page: every page stays in the conversation the LLM re-reads
          const header = page === 1
            ? `STORY "${story.title}" - PAGE 1 OF ${total}\n${RULES}`
            : `STORY "${story.title}" - PAGE ${page} OF ${total}`;
          return `${header}\nTEXT: ${text}\n${end}`;
        },
      },
    ],
  };
}
