import { readdir, readFile, stat } from 'node:fs/promises';
import { join } from 'node:path';
import type { GatewayModule } from '../registry/registry.ts';

const PAGE_CHARS = 750; // ~120 words: one breath of narration per tool call, same size the robot uses for SD stories
const MAX_FILE_BYTES = 256 * 1024;
const MAX_LISTED = 10;

export interface Story {
  id: string;
  title: string;
  pages: string[];
  searchText: string;
}

export function fold(text: string): string {
  return text.normalize('NFD').replace(/\p{Diacritic}/gu, '').toLowerCase();
}

function slug(text: string): string {
  return fold(text)
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-+|-+$/g, '');
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
    let raw = (await readFile(path, 'utf8')).replace(/^﻿/, '');
    const base = name.slice(0, -4);
    let title = base;
    const first = raw.split('\n', 1)[0]!;
    if (first.startsWith('# ')) {
      title = first.slice(2).trim();
      raw = raw.slice(first.length);
    }
    const id = slug(base);
    const pages = paginate(raw);
    if (!id || pages.length === 0 || stories.has(id)) continue;
    stories.set(id, { id, title, pages, searchText: fold(`${title} ${raw}`) });
  }
  return [...stories.values()];
}

const RULES =
  'READ-ALOUD RULES: read the text exactly as written, word for word, in an expressive storyteller voice for small ' +
  'children; do not summarize, skip, add or explain anything. If a child interrupts, answer briefly and then continue.';

export function storiesModule(opts: { dataDir: string }): GatewayModule {
  const dir = join(opts.dataDir, 'historias');
  return {
    name: 'stories',
    tools: () => [
      {
        name: 'story_list',
        description:
          'Lists children stories available in the gateway library. Call this when a child asks for a story, optionally ' +
          'with a theme (for example "dinossauro" or "dormir"). Then pick one and read it with story_read_page.',
        inputSchema: {
          type: 'object',
          properties: { theme: { type: 'string', maxLength: 80, description: 'Optional theme or keyword.' } },
          additionalProperties: false,
        },
        async handler(args) {
          const all = await loadStories(dir);
          const words = fold(String(args.theme ?? ''))
            .split(/[^a-z0-9]+/)
            .filter((w) => w.length >= 3);
          const matches = words.length ? all.filter((s) => words.some((w) => s.searchText.includes(w))) : all;
          const shown = (matches.length ? matches : all).slice(0, MAX_LISTED);
          if (shown.length === 0) {
            return 'The story library is empty. Make up a short original story yourself and tell it to the child.';
          }
          const lines = shown.map((s) => `- id: ${s.id} | title: ${s.title} | pages: ${s.pages.length}`);
          const note = matches.length
            ? 'Pick one and call story_read_page with its id.'
            : 'No story matches that theme. Offer one of these, or make up a short original story yourself.';
          return `${note}\n${lines.join('\n')}`;
        },
      },
      {
        name: 'story_read_page',
        description:
          'Returns one page of a library story to read aloud. Start with page 1, then call again with the next page number ' +
          'right after finishing each page, until the text says FIM.',
        inputSchema: {
          type: 'object',
          properties: {
            story_id: { type: 'string', pattern: '^[a-z0-9-]{1,100}$', description: 'The id from story_list.' },
            page: { type: 'integer', minimum: 1, maximum: 500, default: 1 },
          },
          required: ['story_id'],
          additionalProperties: false,
        },
        async handler(args) {
          const story = (await loadStories(dir)).find((s) => s.id === args.story_id);
          if (!story) return 'There is no story with that id. Call story_list to see the available stories.';
          const page = Number(args.page);
          if (page > story.pages.length) {
            return `This story only has ${story.pages.length} pages. It is already over (FIM).`;
          }
          const header = `STORY "${story.title}" - PAGE ${page} OF ${story.pages.length}`;
          const text = story.pages[page - 1]!;
          const next =
            page === story.pages.length
              ? '[FIM - this was the last page. When you finish reading, softly ask the children what they liked most.]'
              : `After reading, call story_read_page with story_id "${story.id}" and page ${page + 1}, without waiting.`;
          return `${header}\n${RULES}\nTEXT: ${text}\n${next}`;
        },
      },
    ],
  };
}
