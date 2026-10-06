// The RenderResult commit path (plan P3-05; design T7 "RenderResult protocol
// + commit"): the ONE place the typeset view's DOM changes. The worker sends
// a frame — every block's key, source range and positional values, and the
// bodies of only the blocks the shell does not already hold — and commit()
// keeps the blocks it holds (by element reference, checked still in place),
// matches an order-preserving prefix and suffix on key, replaces only the
// middle, and writes the positional attributes (data-pid, data-s0,
// margin-bottom) in place. Engine::renderResult has the frame's layout.

const utf8 = new TextDecoder();

// a key as a map key (16 bytes, lo then hi, as hex)
const hex = (bytes, at) => {
  let s = '';
  for (let i = 0; i < 16; i++) s += bytes[at + i].toString(16).padStart(2, '0');
  return s;
};

// frame (its head and table) and html (the sent blocks, decoded) → { head,
// all (every sent block, in order), allSent, blocks:
// [{ pid, s0, s1, state, hPx, gapPx, gap, key, keyBytes, html (the block as
// the legacy writer spells it, positional attributes included; null: held) }] }
export function decodeResult(frame, html) {
  const u8 = frame instanceof Uint8Array ? frame : new Uint8Array(frame);
  const dv = new DataView(u8.buffer, u8.byteOffset, u8.byteLength);
  if (u8[0] !== 0x54 || u8[1] !== 0x53 || u8[2] !== 0x52 || u8[3] !== 0x52) throw new Error('RenderResult: bad frame');
  const hl = dv.getUint32(4, true);
  const head = JSON.parse(utf8.decode(u8.subarray(8, 8 + hl)));
  let at = 8 + hl;
  const n = dv.getUint32(at, true);
  at += 4;
  // the HTML, sliced per block (offsets are in UTF-16 units)
  const all = html ?? utf8.decode(u8.subarray(at + 56 * n));
  const blocks = new Array(n);
  let allSent = true;
  for (let i = 0; i < n; i++, at += 56) {
    const off = dv.getUint32(at + 48, true), len = dv.getUint32(at + 52, true);
    blocks[i] = {
      pid: dv.getUint32(at, true), s0: dv.getUint32(at + 4, true), s1: dv.getUint32(at + 8, true),
      state: dv.getUint32(at + 12, true), hPx: dv.getFloat64(at + 16, true), gapPx: dv.getFloat64(at + 24, true),
      gap: head.gaps[i] ?? '', key: hex(u8, at + 32), keyBytes: u8.slice(at + 32, at + 48),
      html: len ? all.slice(off, off + len) : null,
    };
    allSent = allSent && len > 0;
  }
  return { head, all, allSent, blocks };
}

// a block as sent, without its positional attributes (its body)
const strip = (html) => {
  const a = html.indexOf(' data-pid="'), z = html.indexOf(' style="');
  let out = a > 0 && z > a ? html.slice(0, a) + html.slice(z) : html;
  const m = out.indexOf(';margin-bottom:');
  if (m > 0) out = out.slice(0, m) + out.slice(out.indexOf('">', m));
  return out;
};
// a block's body with its positional attributes put back: the legacy
// typeset bytes (the engine's writeBlock; unitRenderResult checks it)
export function legacyBlock(body, pid, s0, gap) {
  const cls = '<div class="tsr-para"';
  let out = cls + ` data-pid="${pid}" data-s0="${s0}"` + body.slice(cls.length);
  if (gap) {
    const end = out.indexOf('">');
    out = out.slice(0, end) + ';margin-bottom:' + gap + out.slice(end);
  }
  return out;
}

// a held block's html at a new place (pid, s0, gap)
const reposition = (html, b) => legacyBlock(strip(html), b.pid, b.s0, b.gap);

// A commit target: one container, its root element and the blocks it holds.
export function createSession(container) {
  // all: the blocks' legacy HTML when one frame sent them all (handle.html
  // as it is); moved: the keys whose stored HTML spells an old place; head:
  // the last committed frame's head (anchors, idPrefix)
  return { container, root: null, rootTag: '', blocks: [], bodies: new Map(), generation: 0, all: null,
           moved: new Set(), head: null };
}

// (plan P3-06) nodes the shell owns inside a container (a behaviour's
// overlay) carry data-tsr-shell: a whole-view swap keeps them
const shellOwned = (n) => n.nodeType === 1 && n.hasAttribute('data-tsr-shell');
function replaceView(container, root, html) {
  if (root && root.parentNode === container) root.remove();
  else for (const n of [...container.childNodes]) if (!shellOwned(n)) n.remove();
  container.insertAdjacentHTML('afterbegin', html);
  return container.firstElementChild;
}

// the keys a session holds, for the next request (16 bytes each)
export function heldKeys(session) {
  const out = new Uint8Array(16 * session.blocks.length);
  session.blocks.forEach((b, i) => out.set(b.keyBytes, 16 * i));
  return out;
}

// the session's document as the legacy HTML string (handle.html)
export function sessionHtml(session) {
  if (session.all !== null) return session.rootTag + '\n' + session.all + '</div>\n';
  let s = session.rootTag + '\n';
  for (const b of session.blocks) {
    const h = session.bodies.get(b.key);
    s += session.moved.has(b.key) ? reposition(h, b) : h;
  }
  return s + '</div>\n';
}

// a frame that names a key the session does not hold (a result computed
// against keys it dropped): the caller asks again holding nothing
export class StaleKeys extends Error {}

// commit(session, result) → { ranges: [{ oldPids, newPids }], kept, rebuilt,
// ignored }. An older generation than the last committed is ignored (a
// coalesced request answered twice).
// (plan P3-19; design T7 S11) the container's content-height factors on the
// root: the contract's --tsr-lh-* (a run's line box is its content area)
const LH_VARS = { body: '--tsr-lh-body', cjk: '--tsr-lh-cjk', mono: '--tsr-lh-mono', monoCjk: '--tsr-lh-monocjk' };
function applyContainer(session, head) {
  const lh = head?.container?.lh;
  if (!lh || !session.root) return;
  for (const [k, v] of Object.entries(LH_VARS))
    if (typeof lh[k] === 'number') session.root.style.setProperty(v, String(lh[k]));
}

export function commit(session, result) {
  const r = commitFrame(session, result);
  if (!r.ignored) applyContainer(session, session.head);
  return r;
}
function commitFrame(session, result) {
  const { head, blocks } = result;
  if (head.generation <= session.generation)
    return { ranges: [], kept: session.blocks.length, rebuilt: false, ignored: true };
  for (const b of blocks)
    if (b.html === null && !session.bodies.has(b.key)) throw new StaleKeys(`RenderResult: key ${b.key} not held`);
  const htmlOf = (b) => b.html ?? reposition(session.bodies.get(b.key), b);
  const old = session.blocks;
  const root = session.root;
  // the blocks held are still the root's children, in order (anything else:
  // rebuild the whole view)
  let intact = !!root && root.isConnected && session.rootTag === head.root && root.parentNode === session.container &&
    root.children.length === old.length;
  for (let i = 0; intact && i < old.length; i++) intact = root.children[i] === old[i].el;
  const bodies = new Map();
  for (const b of blocks) bodies.set(b.key, b.html ?? session.bodies.get(b.key));
  // the order-preserving prefix and suffix on key; the middle is replaced
  let pre = 0, suf = 0;
  if (intact) {
    while (pre < old.length && pre < blocks.length && old[pre].key === blocks[pre].key) pre++;
    while (suf < old.length - pre && suf < blocks.length - pre &&
           old[old.length - 1 - suf].key === blocks[blocks.length - 1 - suf].key) suf++;
  }
  // nothing held in place, or nothing kept (a relayout): one swap of the view
  // the held blocks this frame did not send: their stored html spells the
  // place they were sent at
  const moved = new Set();
  for (const b of blocks) if (b.html === null) moved.add(b.key);
  if (!intact || pre + suf === 0) {
    let html = head.root + '\n';
    if (result.allSent) html += result.all;  // every block, in order: the legacy body as it is
    else for (const b of blocks) html += htmlOf(b);
    session.root = replaceView(session.container, root, html + '</div>\n');
    session.rootTag = head.root;
    session.head = head;
    const kids = session.root.children;
    for (let i = 0; i < blocks.length; i++) {
      blocks[i].el = kids[i];
      blocks[i].html = undefined;
    }
    session.blocks = blocks;
    session.bodies = bodies;
    session.generation = head.generation;
    session.all = result.allSent ? result.all : null;
    session.moved = moved;
    return { ranges: [{ oldPids: old.map((b) => b.pid), newPids: blocks.map((b) => b.pid) }], kept: 0, rebuilt: true };
  }
  const next = new Array(blocks.length);
  const place = (i, el, was) => {  // a kept block's positional values, written only when they moved
    const b = blocks[i], o = el.dataset;
    if (session.moved.has(b.key) || was.pid !== b.pid || was.s0 !== b.s0 || was.gap !== b.gap) moved.add(b.key);
    if (was.pid !== b.pid) o.pid = String(b.pid);
    if (was.s0 !== b.s0) o.s0 = String(b.s0);
    if (was.gap !== b.gap) {
      const st = el.getAttribute('style') ?? '';
      const m = st.indexOf(';margin-bottom:');
      el.setAttribute('style', (m >= 0 ? st.slice(0, m) : st) + (b.gap ? ';margin-bottom:' + b.gap : ''));
    }
    b.el = el;
    b.html = undefined;
    next[i] = b;
  };
  for (let i = 0; i < pre; i++) place(i, old[i].el, old[i]);
  for (let k = 0; k < suf; k++) place(blocks.length - suf + k, old[old.length - suf + k].el, old[old.length - suf + k]);
  if (old.length - suf > pre) {  // the old middle at once (a Range: no per-node removal cost)
    const range = document.createRange();
    range.setStartBefore(old[pre].el);
    range.setEndAfter(old[old.length - suf - 1].el);
    range.deleteContents();
  }
  let mid = '';
  for (let i = pre; i < blocks.length - suf; i++) mid += htmlOf(blocks[i]);
  if (mid) {
    const ref = suf ? next[blocks.length - suf].el : null;
    if (ref) ref.insertAdjacentHTML('beforebegin', mid);
    else root.insertAdjacentHTML('beforeend', mid);
    const kids = root.children;
    for (let i = pre; i < blocks.length - suf; i++) {
      blocks[i].el = kids[i];
      blocks[i].html = undefined;
      next[i] = blocks[i];
    }
  }
  session.blocks = next;
  session.bodies = bodies;
  session.generation = head.generation;
  session.all = null;
  session.moved = moved;
  session.head = head;
  const replaced = old.length - pre - suf > 0 || blocks.length - pre - suf > 0;
  return {
    ranges: replaced ? [{ oldPids: old.slice(pre, old.length - suf).map((b) => b.pid),
                          newPids: blocks.slice(pre, blocks.length - suf).map((b) => b.pid) }] : [],
    kept: pre + suf, rebuilt: false,
  };
}

// handle.offsetAt(node, offset): the source byte of a DOM position — its
// block's base plus its nearest source-anchored element's start — or null
export function offsetAt(session, node) {
  const el = node?.nodeType === 1 ? node : node?.parentElement;
  if (!el) return null;
  const block = session.blocks.find((b) => b.el === el || b.el.contains(el));
  if (!block) return null;
  const anchored = el.closest('[data-s]');
  const rel = anchored && block.el.contains(anchored) ? +anchored.dataset.s : 0;
  return block.s0 + rel;
}

// handle.elementsAt(byte): the elements that show a source byte — the lines
// of its block whose source range holds it, else the block
export function elementsAt(session, byte) {
  const block = session.blocks.find((b) => b.s0 <= byte && byte < Math.max(b.s1, b.s0 + 1)) ??
    [...session.blocks].reverse().find((b) => b.s0 <= byte);
  if (!block) return [];
  const rel = byte - block.s0;
  const lines = [...block.el.querySelectorAll(':scope > [data-s][data-e]')]
    .filter((l) => +l.dataset.s <= rel && rel < +l.dataset.e);
  return lines.length ? lines : [block.el];
}
