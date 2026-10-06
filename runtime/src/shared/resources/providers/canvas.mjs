// Built-in providers of text measurement (plan P3-21; design T9 A2): widths
// and vertical metrics measured by the worker's canvas under the request's
// metric key — the font string built from the key (its stack, size,
// weight, style), never from ambient state. Its memo dedups one round only:
// the engine's Session keeps the answers across documents.
import { CanvasMeasurer } from '../../../worker/canvas_measure.mjs';

// a metric key → the canvas font (phase 1: the stack, size, weight, style)
const styleOf = (mk) => ({ family: mk.stack, sizePx: mk.sizePx, weight: mk.weight, italic: mk.italic });

export function canvasProviders(measurer = new CanvasMeasurer()) {
  return {
    textWidth: {
      resolve(rows, { req, tm }) {
        const seen = new Map();  // per round: keys that share a canvas font
        if (tm) tm.words = (tm.words ?? 0) + rows.length;
        return rows.map((r) => {
          measurer.setStyle(styleOf(req.mks[r.mk]));
          const k = measurer.fontKey + '\0' + r.text;
          let px = seen.get(k);
          if (px === undefined) seen.set(k, (px = measurer.width(r.text)));
          return { resId: r.resId, px };
        });
      },
    },
    fontVmet: {
      resolve(rows, { req }) {
        return rows.map((r) => {
          measurer.setStyle(styleOf(req.mks[r.mk]));
          const { ascent, descent } = measurer.vmet();
          return { resId: r.resId, asc: ascent, desc: descent };
        });
      },
    },
  };
}
