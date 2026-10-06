// Copy (document-model §9.3, normative; plan P3-07, design T7 "ContentText
// projection + CopyPolicy"): CONTENT text from the typeset DOM. The engine
// decides what copy takes of each run and encodes it; this is the DOM
// mirror, a plain concatenator:
// - a run without data-syn is text (its selected part);
// - data-copy replaces a run, once per data-copy-group in a block (a
//   formula's source too, plan P3-26: every part of it in one group);
// - any other data-syn run is omitted (hyphens, markers, spacers, a
//   footnote marker, a backlink, error text);
// - after each line its separator, data-join: space ' ', none '', tab, row
//   '\n', para '\n\n'; absent: a line boundary '\n'. A change of block (a
//   .tsr-para on screen, a .tsr-band's data-b on paged sheets) is a
//   paragraph break;
// - a line without items (a blank code row, an empty cell) is '' and its
//   separator; a line whose items are all omitted adds nothing, not even
//   its separator;
// - a sidecar row (data-track="sidecar") copies only when the selection
//   holds nothing else: across code rows only the code is copied (D-R03).
// Source offsets (data-s) are for anchoring, never for copy.

const SEP = { space: ' ', none: '', tab: '\t', row: '\n', para: '\n\n' };
const sepOf = (line) => SEP[line.dataset.join] ?? '\n';

// the block a line belongs to (its identity across a sheet cut: data-b)
function blockOf(line) {
  const b = line.closest('.tsr-para, .tsr-band');
  if (!b) return null;
  return b.classList.contains('tsr-band') ? `b${b.dataset.b}` : b;
}

// the selected part of a text node
function selected(range, tn) {
  if (!range.intersectsNode(tn)) return '';
  let text = tn.data;
  if (range.endContainer === tn) text = text.slice(0, range.endOffset);
  if (range.startContainer === tn) text = text.slice(range.startOffset);
  return text;
}

// a line's items (its runs, not the gutter marker or an empty anchor), and
// what copy takes of them in the range: null when every item is omitted;
// first / last: the copy group its first and last kept items belong to (a
// replaced node cut across lines joins them with nothing)
function lineText(line, range, groups, block) {
  let text = '', items = 0, kept = 0, first, last;
  for (const run of line.children) {
    const syn = run.dataset.syn;
    if (syn === 'anchor' || run.classList.contains('tsr-marker')) continue;
    items++;
    if (syn === undefined) {  // text
      if (!kept++) first = null;
      last = null;
      for (const tn of run.childNodes) if (tn.nodeType === Node.TEXT_NODE) text += selected(range, tn);
      continue;
    }
    const copy = run.dataset.copy;
    if (copy === undefined) continue;  // omitted
    const g = run.dataset.copyGroup ?? null;
    if (!kept++) first = g;
    last = g;
    if (!range.intersectsNode(run)) continue;
    if (g !== null) {  // once per group (a node cut across runs or lines)
      let seen = groups.get(block);
      if (!seen) groups.set(block, (seen = new Set()));
      if (seen.has(g)) continue;
      seen.add(g);
    }
    text += copy;
  }
  if (items > 0 && kept === 0) return null;
  return { text, first: first ?? null, last: last ?? null };
}

// The content text of a range in a view (a commit root, a print root),
// or null when the range holds no typeset line — not ours: native copy
// applies (a semantic page with marked generated text excepted, below).
export function contentTextFromRange(range, root) {
  let lines = [...root.querySelectorAll('.tsr-line')].filter((l) => range.intersectsNode(l));
  if (!lines.length) return semanticText(range, root);
  if (lines.some((l) => l.dataset.track !== 'sidecar')) lines = lines.filter((l) => l.dataset.track !== 'sidecar');
  const groups = new Map();
  let out = '', prev = null, prevBlock = null, prevLast = null;
  for (const line of lines) {
    const block = blockOf(line);
    const t = lineText(line, range, groups, block);
    if (t === null) continue;
    if (prev !== null) {
      if (block !== prevBlock) out += '\n\n';
      else if (prevLast === null || prevLast !== t.first) out += sepOf(prev);  // (inside a group: nothing)
    }
    out += t.text;
    prev = line;
    prevBlock = block;
    prevLast = t.last;
  }
  return out;
}

// The semantic page (D-R06): its generated text says what copy takes as the
// typeset view does (data-syn, data-copy). A range holding any is copied as
// the browser would copy it without them; else native copy (null).
function semanticText(range, root) {
  if (!root.querySelector?.('[data-syn], [data-copy]')) return null;
  const frag = range.cloneContents();
  const marked = frag.querySelectorAll('[data-syn], [data-copy]');
  if (!marked.length) return null;
  for (const el of marked) {
    if (!frag.contains(el)) continue;  // inside one already removed
    if (el.dataset.copy !== undefined) el.replaceWith(el.dataset.copy);
    else el.remove();
  }
  // the browser's plain text of what is left (rendered, off screen)
  const box = document.createElement('div');
  box.style.cssText = 'position:fixed;left:-99999px;top:0;white-space:normal';
  box.appendChild(frag);
  document.body.appendChild(box);
  const text = box.innerText;
  box.remove();
  return text;
}

// Installs the clipboard interception on a container (the core copy
// contract: replaceable, never absent); returns the uninstaller. Required
// once hyphenation inserts glyphs (v2 §8).
export function installCopy(container) {
  const handler = (e) => {
    const sel = container.ownerDocument.getSelection();
    if (!sel || sel.rangeCount === 0 || sel.isCollapsed) return;
    const text = contentTextFromRange(sel.getRangeAt(0), container);
    if (text === null) return;  // not ours: native copy
    e.clipboardData.setData('text/plain', text);
    e.preventDefault();
  };
  container.addEventListener('copy', handler);
  return () => container.removeEventListener('copy', handler);
}
