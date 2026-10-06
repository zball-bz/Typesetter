// In-page invariant audits (testing.md §5.3). One implementation: dev
// diagnostic in the runtime (the devAudit behaviour) AND the assertion body
// of the Playwright tests; the thresholds are shared/audit-constants.mjs.
import { AUDIT } from '../shared/audit-constants.mjs';

// the advance of one space in a CSS font shorthand (canvas: same shaping
// as the DOM for a lone space)
const spaceCache = new Map();
// an element's font as a canvas font string, from the longhands: the
// computed `font` shorthand serializes to "" whenever a longhand it cannot
// express is set (font-feature-settings on a code row, plan P3-22)
function canvasFont(el) {
  const cs = getComputedStyle(el);
  return `${cs.fontStyle} ${cs.fontWeight} ${cs.fontSize} ${cs.fontFamily}`;
}
function spaceAdvance(font) {
  if (!font) return 0;
  let w = spaceCache.get(font);
  if (w === undefined) {
    const ctx = document.createElement('canvas').getContext('2d');
    ctx.font = font;
    w = ctx.measureText(' ').width;
    spaceCache.set(font, w);
  }
  return w;
}

export function auditTypeset(root) {
  const report = {
    lines: 0,
    failures: [],
    rightEdge: { max: 0, mean: 0, n: 0 },
    ok: false,
  };

  for (const line of root.querySelectorAll('.tsr-line')) {
    report.lines++;
    // Collect text rects from in-flow children only: absolutely positioned
    // gutter markers sit outside the line box and are not line fragments
    // (their rect top differs from the text's on mixed-script lines), and
    // zero-height inline-block spacers (tsr-sp) are filtered by height.
    const rects = [];
    for (const el of line.children) {
      if (getComputedStyle(el).position === 'absolute') continue;
      if (el.dataset.syn === 'math') {  // one engine-defined fragment
        const r = el.getBoundingClientRect();
        if (r.width > 0 && r.height > 0) rects.push(r);
        continue;
      }
      const range = document.createRange();
      range.selectNodeContents(el);
      for (const r of range.getClientRects()) {
        if (r.width > 0 && r.height > 0) rects.push(r);
      }
      range.detach?.();
    }

    // line-integrity: all fragments share one vertical band (v2 §7 rule 1).
    // Mixed fonts on a line legitimately differ in rect top/height (baseline
    // aligned, ascents differ) — a real browser re-break stacks fragments,
    // i.e. some fragment's top clears another's bottom.
    if (rects.length) {
      const maxTop = Math.max(...rects.map((r) => r.top));
      const minBottom = Math.min(...rects.map((r) => r.bottom));
      if (maxTop >= minBottom - AUDIT.bandSlackPx) {
        report.failures.push({
          audit: 'line-integrity',
          text: (line.textContent || '').slice(0, 48),
        });
      }
    }

    // right-edge: justified lines (a break's data-join: space or none)
    // end within 1px of the measure. Measured as the flow edge of the last
    // child element (rect.right + margin-right) so letter-spacing overhangs
    // and punct-squeeze margins are accounted exactly.
    // (a data-overfull line holds a run wider than the measure — set at the
    // shrink limit and reported as overfull-line, plan P0-12 — not a defect)
    // (plan P3-07) a break inside a stream joins with a space or nothing;
    // tab / row / para are stream ends, never justified
    const join = line.dataset.join;
    if ((join === 'space' || join === 'none') && line.dataset.ragged === undefined &&
        line.dataset.overfull === undefined && rects.length) {
      const lineRect = line.getBoundingClientRect();
      let contentRight = -Infinity;
      for (const el of line.children) {
        const r = el.getBoundingClientRect();
        if (r.width === 0 && r.height === 0) continue;
        const mr = parseFloat(getComputedStyle(el).marginRight) || 0;
        contentRight = Math.max(contentRight, r.right + mr);
      }
      if (contentRight === -Infinity) contentRight = Math.max(...rects.map((r) => r.right));
      const dev = lineRect.right - contentRight; // >0 short, <0 overflow
      const adev = Math.abs(dev);
      report.rightEdge.n++;
      report.rightEdge.mean += adev;
      if (adev > report.rightEdge.max) report.rightEdge.max = adev;
      if (adev > AUDIT.rightEdgePx) {
        report.failures.push({
          audit: 'right-edge',
          dev: Math.round(dev * 1000) / 1000,
          text: (line.textContent || '').slice(0, 48),
        });
      }
    }
  }

  // overflow: nothing escapes the paragraph box horizontally (a block that
  // scrolls sideways — a table wider than the measure, D-Y09 — holds it)
  for (const para of root.querySelectorAll('.tsr-para')) {
    if (para.scrollWidth > para.clientWidth + AUDIT.overflowPx && !para.querySelector('[data-overfull]') &&
        getComputedStyle(para).overflowX === 'visible') {
      report.failures.push({
        audit: 'overflow',
        by: para.scrollWidth - para.clientWidth,
        pid: para.dataset.pid,
      });
    }
    // line stacking: tops strictly increase — a failed break plan collapses
    // a whole paragraph onto one line (the audit blind spot behind the
    // overprinted-specimen bug)
    // per track (plan P3-07: data-track): the main flow, sidecar rows and
    // caption rows each stack; table cells share their row's top
    const prevTop = new Map();
    for (const l of para.querySelectorAll('.tsr-line')) {
      const track = l.dataset.track ?? '';
      if (track === 'cell') continue;
      const top = parseFloat(l.style.top);
      const prev = prevTop.get(track);
      if (prev !== undefined && !(top > prev)) {
        report.failures.push({ audit: 'line-stacking', pid: para.dataset.pid, track, top });
        break;
      }
      prevTop.set(track, top);
    }
    // and no absurd compression: a line shrinks at most to the breaker's
    // limit (shrinkThreshold 0.37 of its spaces, plan P0-12); beyond that an
    // infeasible line was force-fitted. The bound is relative to the line's
    // space advance (a monospace space is twice a serif one), never stricter
    // than the old absolute -2.5px.
    for (const l of para.querySelectorAll('.tsr-line')) {
      const ws = parseFloat(l.style.wordSpacing || '0');
      if (!(ws < AUDIT.wordSpacingFloorPx)) continue;
      const first = l.querySelector('span');
      const limit = -AUDIT.shrinkShare * spaceAdvance(first ? canvasFont(first) : '') - AUDIT.shrinkSlackPx;
      if (ws < Math.min(AUDIT.wordSpacingFloorPx, limit)) {
        report.failures.push({ audit: 'compression', pid: para.dataset.pid, ws });
        break;
      }
    }
  }

  // (plan P3-19; design T7 S11) the baseline: a text line's sits at its top
  // plus its tallest run's ascent — the contract gives each run its face's
  // content height as line-height — and a code row's centres its face's
  // extents in the row; within a pixel, whatever the host's line-height.
  // Lines of text runs only (objects size their own boxes).
  const g = document.createElement('canvas').getContext('2d');
  const extents = (el) => {
    g.font = canvasFont(el);
    const m = g.measureText('Hg');
    return [m.fontBoundingBoxAscent, m.fontBoundingBoxDescent];
  };
  for (const line of root.querySelectorAll('.tsr-line')) {
    const kids = [...line.children].filter((e) => !e.classList.contains('tsr-marker') && e.dataset.syn !== 'anchor');
    if (!kids.length || !kids.every((e) => e.classList.contains('tsr-r') && !e.classList.contains('tsr-sp'))) continue;
    const probe = document.createElement('span');
    probe.style.cssText = 'display:inline-block;width:0;height:0;vertical-align:baseline';
    line.insertBefore(probe, line.firstChild);
    const actual = probe.getBoundingClientRect().bottom - line.getBoundingClientRect().top;
    probe.remove();
    let expected;
    if (line.classList.contains('tsr-row')) {
      const [a, d] = extents(line);
      expected = (parseFloat(line.style.lineHeight) - (a + d)) / 2 + a;
    } else {
      expected = Math.max(...kids.map((e) => extents(e)[0]));
    }
    if (Math.abs(actual - expected) > AUDIT.baselinePx) {
      report.failures.push({ audit: 'baseline', actual: Math.round(actual * 100) / 100,
                             expected: Math.round(expected * 100) / 100, text: (line.textContent || '').slice(0, 32) });
      break;
    }
  }

  // (plan P3-18; design T4 M8) the render contract: the classes that carry
  // metrics paint as the engine measured them — a theme overriding one
  // (bold that is not 700, italic that is upright, code that wraps its
  // spaces) breaks the measure/render agreement
  const CONTRACT = [
    ['tsr-b', (cs) => parseInt(cs.fontWeight, 10) === 700, 'font-weight'],
    ['tsr-i', (cs, el) => el.classList.contains('tsr-cjk') || cs.fontStyle !== 'normal', 'font-style'],
    ['tsr-pre', (cs) => cs.whiteSpace === 'pre', 'white-space'],
  ];
  for (const [cls, ok, prop] of CONTRACT) {
    const el = root.querySelector(`.tsr-r.${cls}`);
    if (el && !ok(getComputedStyle(el), el)) report.failures.push({ audit: 'contract', cls, prop });
  }

  // anchors: pids unique, line spans inside their paragraph
  const pids = [...root.querySelectorAll('[data-pid]')].map((e) => e.dataset.pid);
  if (new Set(pids).size !== pids.length)
    report.failures.push({ audit: 'anchors', msg: 'duplicate data-pid' });

  if (report.rightEdge.n) report.rightEdge.mean /= report.rightEdge.n;
  report.ok = report.failures.length === 0;
  return report;
}
