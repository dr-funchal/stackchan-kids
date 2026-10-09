/** Lowercase without accents: "Pão" -> "pao". */
export function fold(text: string): string {
  return text.normalize('NFD').replace(/\p{Diacritic}/gu, '').toLowerCase();
}

export function slug(text: string, max = 60): string {
  return fold(text)
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-+|-+$/g, '')
    .slice(0, max)
    .replace(/-+$/g, '');
}

// Words children (and the LLM) put around the actual name: "conta a história do dinossauro", "toca a música da..."
const STOPWORDS = new Set([
  'historia', 'historinha', 'musica', 'musicas', 'cancao', 'conta', 'contar', 'toca', 'tocar', 'coloca', 'bota',
  'uma', 'umas', 'sobre', 'para', 'com', 'que', 'dos', 'das', 'pra', 'por', 'favor', 'the', 'and',
]);

export function queryWords(text: string): string[] {
  return fold(text)
    .split(/[^a-z0-9]+/)
    .filter((w) => w.length >= 3 && !STOPWORDS.has(w));
}

/** Words in the title count 3, anywhere else 1. 0 = no match. */
export function matchScore(words: string[], title: string, other = ''): number {
  const t = fold(title);
  const o = fold(other);
  return words.reduce((n, w) => n + (t.includes(w) ? 3 : o.includes(w) ? 1 : 0), 0);
}

export function bestMatch<T>(items: T[], query: string, title: (t: T) => string, other?: (t: T) => string) {
  const words = queryWords(query);
  if (words.length === 0) return undefined;
  let best: T | undefined;
  let bestScore = 0;
  for (const item of items) {
    const score = matchScore(words, title(item), other?.(item));
    if (score > bestScore) {
      best = item;
      bestScore = score;
    }
  }
  return best;
}
