import assert from 'node:assert/strict';
import { mkdtemp, mkdir, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { after, before, test } from 'node:test';
import { silentLogger } from '../src/logger.ts';
import { findStory, loadStories, paginate, storiesModule } from '../src/modules/stories.ts';
import { ToolRegistry } from '../src/registry/registry.ts';

let dataDir: string;
let reg: ToolRegistry;

before(async () => {
  dataDir = await mkdtemp(join(tmpdir(), 'gw-stories-'));
  await mkdir(join(dataDir, 'historias'));
  const long = Array.from({ length: 30 }, (_, i) => `Esta é a frase número ${i + 1} da história.`).join(' ');
  await writeFile(join(dataDir, 'historias', 'dino.txt'), `# O Dinossauro Sonolento\n\n${long}`);
  await writeFile(join(dataDir, 'historias', 'Pão de Queijo.txt'), 'A pequena Ana fez um pão de queijo. Fim.');
  await writeFile(join(dataDir, 'historias', 'ignorado.md'), 'não é txt');
  reg = new ToolRegistry({ log: silentLogger, defaultTimeoutMs: 1000, allowRestricted: false });
  reg.register(storiesModule({ dataDir }));
});
after(() => rm(dataDir, { recursive: true, force: true }));

const text = async (name: string, args: unknown) => (await reg.call(name, args)).content[0]!.text;

test('paginate cuts at sentence ends and loses no text', () => {
  const source = Array.from({ length: 40 }, (_, i) => `Frase ${i}.`).join(' ');
  const pages = paginate(source, 100);
  assert.ok(pages.length > 1);
  for (const p of pages.slice(0, -1)) assert.match(p, /[.!?]$/);
  assert.equal(pages.join(' '), source);
});

test('findStory matches id, title words and themes, ignoring accents and filler words', async () => {
  const all = await loadStories(join(dataDir, 'historias'));
  assert.equal(findStory(all, 'dino')?.id, 'dino');
  assert.equal(findStory(all, 'conta a história do DINOSSAURO sonolento')?.id, 'dino');
  assert.equal(findStory(all, 'pao de queijo')?.id, 'pao-de-queijo');
  assert.equal(findStory(all, 'história'), undefined);
  assert.equal(findStory(all, 'submarino'), undefined);
});

test('search matches a theme; no match falls back to the full list', async () => {
  const out = await text('library_search', { query: 'dinossauro' });
  assert.match(out, /"O Dinossauro Sonolento" \(id: dino,/);
  assert.doesNotMatch(out, /Pão de Queijo/);
  const none = await text('library_search', { query: 'submarino' });
  assert.match(none, /No match in the online library/);
  assert.match(none, /Dinossauro/);
});

test('read by title in one call, rules only on page 1, last page says FIM', async () => {
  const first = await text('library_read_page', { story: 'dinossauro sonolento', page: 1 });
  assert.match(first, /PAGE 1 OF (\d+)/);
  assert.match(first, /READ-ALOUD RULES/);
  const total = Number(/OF (\d+)/.exec(first)![1]);
  assert.ok(total >= 2);
  assert.match(first, /call library_read_page with story "dino"\./);
  const second = await text('library_read_page', { story: 'dino' });
  assert.match(second, /PAGE 2 OF/);
  assert.doesNotMatch(second, /READ-ALOUD RULES/, 'rules are not repeated: the LLM re-reads every page');
  let last = second;
  for (let p = 3; p <= total; p++) last = await text('library_read_page', { story: 'dino' });
  assert.match(last, /FIM/);
  assert.match(await text('library_read_page', { story: 'dino' }), /is over/);
});

test('omitting the page continues the story; jumping ahead is clamped to the next page', async () => {
  const long = Array.from({ length: 80 }, (_, i) => `Frase longa número ${i + 1} desta história de teste.`).join(' ');
  await writeFile(join(dataDir, 'historias', 'longa.txt'), `# A Longa\n\n${long}`);
  assert.match(await text('library_read_page', { story: 'longa' }), /PAGE 1 OF/);
  assert.match(await text('library_read_page', { story: 'longa' }), /PAGE 2 OF/);
  assert.match(await text('library_read_page', { story: 'longa', page: 3 }), /PAGE 3 OF/);
  assert.match(await text('library_read_page', { story: 'longa', page: 5 }), /PAGE 4 OF/, 'page 4 is not skipped');
  assert.match(await text('library_read_page', { story: 'longa', page: 1 }), /PAGE 1 OF/, 'going back is allowed');
});

test('a first line repeating the title is not read twice', async () => {
  await writeFile(join(dataDir, 'historias', 'repetida.txt'), '# A Lua Dançante\n\nA LUA DANÇANTE\n\nEra uma vez a Lua. Fim.');
  const page = await text('library_read_page', { story: 'lua dancante' });
  assert.match(page, /TEXT: Era uma vez/);
});

test('unknown stories and path-like input never escape the library', async () => {
  for (const story of ['../../etc/passwd', '..', 'submarino amarelo']) {
    assert.match(await text('library_read_page', { story }), /not in the online library/);
  }
  assert.equal((await reg.call('library_read_page', { story: '' })).isError, true);
});

test('empty library tells the model to improvise', async () => {
  const empty = new ToolRegistry({ log: silentLogger, defaultTimeoutMs: 1000, allowRestricted: false });
  empty.register(storiesModule({ dataDir: join(dataDir, 'inexistente') }));
  assert.match((await empty.call('library_search', {})).content[0]!.text, /library is empty/);
});
