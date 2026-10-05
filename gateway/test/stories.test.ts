import assert from 'node:assert/strict';
import { mkdtemp, mkdir, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { after, before, test } from 'node:test';
import { silentLogger } from '../src/logger.ts';
import { paginate, storiesModule } from '../src/modules/stories.ts';
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

test('list matches a theme ignoring case and accents', async () => {
  const out = await text('story_list', { theme: 'DINOSSAURO' });
  assert.match(out, /id: dino \|/);
  assert.doesNotMatch(out, /pao-de-queijo/);
  assert.match(await text('story_list', { theme: 'pao' }), /id: pao-de-queijo/);
});

test('list falls back to everything when no theme matches', async () => {
  const out = await text('story_list', { theme: 'submarino' });
  assert.match(out, /No story matches/);
  assert.match(out, /id: dino/);
});

test('read pages in order, last page says FIM', async () => {
  const first = await text('story_read_page', { story_id: 'dino' });
  assert.match(first, /PAGE 1 OF (\d+)/);
  assert.match(first, /READ-ALOUD RULES/);
  const total = Number(/OF (\d+)/.exec(first)![1]);
  assert.ok(total >= 2);
  assert.match(first, new RegExp(`page 2, without waiting`));
  assert.match(await text('story_read_page', { story_id: 'dino', page: total }), /FIM/);
  assert.match(await text('story_read_page', { story_id: 'dino', page: total + 1 }), /already over/);
});

test('story_id cannot escape the library directory', async () => {
  for (const id of ['../../etc/passwd', '..', 'dino/../../x', 'a b']) {
    const res = await reg.call('story_read_page', { story_id: id });
    assert.equal(res.isError, true);
  }
  assert.match(await text('story_read_page', { story_id: 'nao-existe' }), /no story with that id/);
});

test('empty library tells the model to improvise', async () => {
  const empty = new ToolRegistry({ log: silentLogger, defaultTimeoutMs: 1000, allowRestricted: false });
  empty.register(storiesModule({ dataDir: join(dataDir, 'inexistente') }));
  assert.match((await empty.call('story_list', {})).content[0]!.text, /library is empty/);
});
