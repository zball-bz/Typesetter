// Canvas measurement backend (OffscreenCanvas; v2 §6 backend 1).
// Widths are memoized per font so a persistent measurer answers repeat
// requests without touching the canvas — the editing loop (editor-design.md
// §2) re-requests every word of the document on each keystroke, and all but
// the edited handful are cache hits. The cache must be cleared whenever a
// new FontFace lands in the worker's FontFaceSet: widths measured against a
// fallback face are stale the moment the real face arrives.
const CACHE_CAP = 200000; // words across all fonts; reset wholesale beyond

export class CanvasMeasurer {
  constructor() {
    this.ctx = new OffscreenCanvas(8, 8).getContext('2d');
    if ('textRendering' in this.ctx) this.ctx.textRendering = 'geometricPrecision';
    this.fontKey = '';
    this.clearCache();
  }
  clearCache() {
    this.cache = new Map();    // fontKey → Map(word → px)
    this.vmetCache = new Map(); // fontKey → {ascent, descent}
    this.words = new Map();    // current font's map (kept live across clears)
    if (this.fontKey) this.cache.set(this.fontKey, this.words);
    this.cacheCount = 0;
  }
  setStyle({ family, sizePx, weight, italic }) {
    const font = `${italic ? 'italic ' : ''}${weight} ${sizePx}px ${family}`;
    if (font !== this.fontKey) {
      this.ctx.font = font;
      this.fontKey = font;
    }
    this.words = this.cache.get(font);
    if (!this.words) this.cache.set(font, (this.words = new Map()));
  }
  width(s) {
    const hit = this.words.get(s);
    if (hit !== undefined) return hit;
    const px = this.ctx.measureText(s).width;
    if (++this.cacheCount > CACHE_CAP) this.clearCache();
    else this.words.set(s, px);
    return px;
  }
  vmet() {
    const hit = this.vmetCache.get(this.fontKey);
    if (hit) return hit;
    const m = this.ctx.measureText('Mg');
    const v = {
      ascent: m.fontBoundingBoxAscent ?? this.ctx.measureText('M').actualBoundingBoxAscent * 1.2,
      descent: m.fontBoundingBoxDescent ?? this.ctx.measureText('g').actualBoundingBoxDescent * 1.2,
    };
    this.vmetCache.set(this.fontKey, v);
    return v;
  }
}
