import { hyphenateSync } from 'hyphen/en';

const SOFT_HYPHEN = '\u00AD';

export async function initHyphenation(): Promise<void> {
  // Static import — nothing to init. Kept async for future lazy-loading.
}

export function getHyphenationPoints(word: string): string[] {
  try {
    const hyphenated = hyphenateSync(word);
    const parts = hyphenated.split(SOFT_HYPHEN);
    if (parts.length > 1 && !(window as any).__hyphenLogged) {
      console.log('[hyphen] sample:', word, '->', parts.join('·'));
      (window as any).__hyphenLogged = true;
    }
    return parts;
  } catch (e) {
    if (!(window as any).__hyphenErrLogged) {
      console.error('[hyphen] error:', e);
      (window as any).__hyphenErrLogged = true;
    }
    return [word];
  }
}
