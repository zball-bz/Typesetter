// Copy rebuild (document-model §9.3, normative): produce CONTENT text from
// the typeset DOM. Walk selected runs in DOM order; skip synthetic runs
// (data-syn: hyphens, markers, resolved refs); between consecutive lines
// insert ' ' or '' per the PREVIOUS line's data-join (absent join = real
// line/unit boundary → newline; different paragraph → blank line). Source
// offsets (data-s) are for anchoring, never for copy.
export function contentTextFromRange(range, root) {
  const parts = [];
  let prevLine = null;
  let prevPara = null;
  for (const line of root.querySelectorAll('.tsr-line')) {
    if (!range.intersectsNode(line)) continue;
    let lineText = '';
    for (const run of line.children) {
      if (run.dataset.syn !== undefined) continue; // synthetic: skip
      for (const tn of run.childNodes) {
        if (tn.nodeType !== Node.TEXT_NODE) continue;
        if (!range.intersectsNode(tn)) continue;
        let text = tn.data;
        if (range.endContainer === tn) text = text.slice(0, range.endOffset);
        if (range.startContainer === tn) text = text.slice(range.startOffset);
        lineText += text;
      }
    }
    if (lineText === '') continue;
    const para = line.closest('.tsr-para');
    if (prevLine !== null) {
      if (para !== prevPara) parts.push('\n\n');
      else {
        const join = prevLine.dataset.join;
        parts.push(join === 'space' ? ' ' : join === 'none' ? '' : '\n');
      }
    }
    parts.push(lineText);
    prevLine = line;
    prevPara = para;
  }
  return parts.join('');
}

// Installs the clipboard interception on a typeset container; returns the
// uninstaller. Required once hyphenation inserts glyphs (v2 §8).
export function installCopy(container) {
  const handler = (e) => {
    const sel = container.ownerDocument.getSelection();
    if (!sel || sel.rangeCount === 0 || sel.isCollapsed) return;
    const text = contentTextFromRange(sel.getRangeAt(0), container);
    if (!text) return; // fall back to native copy (e.g. semantic phase)
    e.clipboardData.setData('text/plain', text);
    e.preventDefault();
  };
  container.addEventListener('copy', handler);
  return () => container.removeEventListener('copy', handler);
}
