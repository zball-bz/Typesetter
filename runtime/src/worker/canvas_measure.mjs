// Canvas measurement backend (OffscreenCanvas; v2 §6 backend 1). It keeps
// no cache (plan P1-21): answers persist in the engine's Session, keyed by
// the complete metric key, so a request holds only what no earlier document
// was answered, and a round dedups its own rows (worker.mjs).
export class CanvasMeasurer {
  constructor() {
    this.ctx = new OffscreenCanvas(8, 8).getContext('2d');
    if ('textRendering' in this.ctx) this.ctx.textRendering = 'geometricPrecision';
    this.fontKey = '';
  }
  setStyle({ family, sizePx, weight, italic }) {
    const font = `${italic ? 'italic ' : ''}${weight} ${sizePx}px ${family}`;
    if (font !== this.fontKey) {
      this.ctx.font = font;
      this.fontKey = font;
    }
  }
  width(s) {
    return this.ctx.measureText(s).width;
  }
  vmet() {
    const m = this.ctx.measureText('Mg');
    return {
      ascent: m.fontBoundingBoxAscent ?? this.ctx.measureText('M').actualBoundingBoxAscent * 1.2,
      descent: m.fontBoundingBoxDescent ?? this.ctx.measureText('g').actualBoundingBoxDescent * 1.2,
    };
  }
}
