import { TextStyle, styleKey } from './types';

export class TextMeasurer {
  private canvas: HTMLCanvasElement;
  private ctx: CanvasRenderingContext2D;
  private charCache: Map<string, number> = new Map();
  private bigramDelta: Map<string, number> = new Map();
  private bigramMeasured: Set<string> = new Set();

  constructor() {
    this.canvas = document.createElement('canvas');
    this.ctx = this.canvas.getContext('2d')!;
  }

  private applyStyle(style: TextStyle): void {
    const italic = style.italic ? 'italic ' : '';
    const weight = style.fontWeight;
    const size = style.fontSize;
    const family = style.fontFamily;
    this.ctx.font = `${italic}${weight} ${size}px ${family}`;
  }

  private cacheKey(ch: string, sk: string): string {
    return sk + ':' + ch;
  }

  private measureCharDirect(ch: string, style: TextStyle, sk: string): number {
    const key = this.cacheKey(ch, sk);
    let w = this.charCache.get(key);
    if (w !== undefined) return w;
    this.applyStyle(style);
    w = this.ctx.measureText(ch).width;
    this.charCache.set(key, w);
    return w;
  }

  private measureBigramDelta(a: string, b: string, style: TextStyle, sk: string): number {
    const key = sk + ':' + a + b;
    if (this.bigramMeasured.has(key)) {
      return this.bigramDelta.get(key) || 0;
    }
    this.bigramMeasured.add(key);
    this.applyStyle(style);
    const pairWidth = this.ctx.measureText(a + b).width;
    const sumWidth = this.measureCharDirect(a, style, sk) + this.measureCharDirect(b, style, sk);
    const delta = pairWidth - sumWidth;
    // Only store non-negligible deltas
    if (Math.abs(delta) > 0.01) {
      this.bigramDelta.set(key, delta);
    }
    return delta;
  }

  measureWord(word: string, style: TextStyle): number {
    if (word.length === 0) return 0;
    const sk = styleKey(style);

    // Sum character widths
    let total = this.measureCharDirect(word[0], style, sk);
    for (let i = 1; i < word.length; i++) {
      total += this.measureCharDirect(word[i], style, sk);
      total += this.measureBigramDelta(word[i - 1], word[i], style, sk);
    }
    return total;
  }

  measureChar(ch: string, style: TextStyle): number {
    return this.measureCharDirect(ch, style, styleKey(style));
  }
}
