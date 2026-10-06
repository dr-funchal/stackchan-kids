import { mkdir, readdir, readFile, rm, stat, writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import type { GatewayModule } from '../registry/registry.ts';
import { bestMatch, fold, queryWords, slug } from '../text.ts';

const PAGE_CHARS = 750; // ~120 words: one breath of narration per tool call, same size the robot uses for SD stories
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
  'READ-ALOUD RULES: read the page EXACTLY as written, word for word, with an expressive storyteller voice for small ' +
  'children: do not summarize, skip, add or explain anything. If a child interrupts, answer briefly and then continue.';

export const storiesDir = (dataDir: string): string => join(dataDir, 'historias');

export function storiesModule(opts: { dataDir: string }): GatewayModule {
  const dir = storiesDir(opts.dataDir);
  return {
    name: 'stories',
    tools: () => [
      {
        name: 'library_search',
        description:
          'ONLINE STORY LIBRARY: extra children stories that are NOT in the robot\'s own list (self.story.list). Whenever ' +
          'a child asks for a story by name or theme, search here too, especially when the robot\'s list does not have ' +
          'it. NEVER tell the child a story does not exist before searching here. Then read it with library_read_page.',
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
            ? 'Stories found in the online library. Read one with library_read_page (story = its title or id, page = 1).'
            : 'No story matches that in the online library. These are available; offer one or make up a story yourself.';
          return `${note}\n${lines.join('\n')}`;
        },
      },
      {
        name: 'library_read_page',
        description:
          'Reads one page of a story from the ONLINE STORY LIBRARY (stories that are not in self.story.list). `story` can ' +
          'be the title, part of it, or the id (e.g. "dinossauro sonolento"). Start with page 1. Right after reading a ' +
          'page aloud, call this again with the next page number, without waiting or asking, until it says FIM.',
        inputSchema: {
          type: 'object',
          properties: {
            story: { type: 'string', minLength: 1, maxLength: 120, description: 'Title, part of the title, or id.' },
            page: { type: 'integer', minimum: 1, maximum: 500, default: 1 },
          },
          required: ['story'],
          additionalProperties: false,
        },
        summarize: (args, result) => {
          const title = /STORY "([^"]+)"/.exec(result ?? '')?.[1] ?? String(args.story);
          return `Leu a página ${args.page} de "${title}"`;
        },
        async handler(args) {
          const all = await loadStories(dir);
          const story = findStory(all, String(args.story));
          if (!story) {
            const titles = all.map((s) => `"${s.title}"`).join(', ') || 'none';
            return `That story is not in the online library. Available: ${titles}.`;
          }
          const page = Number(args.page);
          if (page > story.pages.length) {
            return `"${story.title}" only has ${story.pages.length} pages. It is already over (FIM).`;
          }
          const header = `STORY "${story.title}" - PAGE ${page} OF ${story.pages.length}`;
          const text = story.pages[page - 1]!;
          const next =
            page === story.pages.length
              ? '[FIM - this was the last page. When you finish reading, softly ask the children what they liked most.]'
              : `NEXT: as soon as you finish reading this page aloud, call library_read_page with story "${story.id}" and ` +
                `page ${page + 1}. Do not stop, do not ask if they want more.`;
          return `${header}\n${RULES}\nTEXT: ${text}\n${next}`;
        },
      },
    ],
  };
}
