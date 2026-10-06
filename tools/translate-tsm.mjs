#!/usr/bin/env node
// Machine-translate a .tsm tree (EN → zh) through an OpenAI-compatible API
// (default: z.ai GLM), with HARD structural protection: code blocks, display
// math, inline math/code spans, #!figure parameter lines, link URLs and
// footnote markers are masked to placeholder tokens BEFORE the model sees
// the text and restored verbatim afterwards, then validated byte-exactly
// against the source. The model only ever sees prose.
//
//   export ZAI_API_KEY=...
//   node tools/translate-tsm.mjs --src examples/real-world/pbr-en \
//        --dst examples/real-world/pbr-zh [--model glm-5.3] [--only Shapes/]
//        [--concurrency 3] [--dry [--show]] [--check] [--force]
//
// Files already present in --dst are skipped (resume-friendly). Output is
// written only after ALL validations pass; failures leave a .reject file
// with diagnostics instead. --check re-validates existing outputs only.
import { readFileSync, writeFileSync, readdirSync, mkdirSync, statSync, existsSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { parseTsm } from '../runtime/src/node/render.mjs';

const args = process.argv.slice(2);
const opt = (k, d) => (args.includes(k) ? args[args.indexOf(k) + 1] : d);
const SRC = opt('--src'); const DST = opt('--dst');
const MODEL = opt('--model', 'glm-5.3');
const BASE = opt('--base', 'https://api.z.ai/api/coding/paas/v4');
const ONLY = opt('--only', null);
const CONC = parseInt(opt('--concurrency', '3'), 10);
const DRY = args.includes('--dry');
const SHOW = args.includes('--show');  // (with --dry) each unit as the model sees it
const CHECK = args.includes('--check');
const FORCE = args.includes('--force');
const KEY = process.env.ZAI_API_KEY;
if (!SRC || !DST) { console.error('usage: --src DIR --dst DIR [--model M] [--only substr] [--dry|--check]'); process.exit(1); }
if (!DRY && !CHECK && !KEY) { console.error('set ZAI_API_KEY (z.ai coding plan key)'); process.exit(1); }

const GLOSSARY = `radiance 辐射亮度; irradiance 辐照度; radiant flux 辐射通量; radiant intensity 辐射强度; radiant exitance 辐射出射度; radiometry 辐射度量学; photometry 光度学; spectral distribution 光谱分布; wavelength 波长; ray tracing 光线追踪; path tracing 路径追踪; Monte Carlo integration 蒙特卡洛积分; estimator 估计量; variance 方差; importance sampling 重要性采样; stratified sampling 分层采样; Russian roulette 俄罗斯轮盘; BRDF/BSDF/BTDF/BSSRDF/PDF/CDF/BVH/SAH 保留缩写; scattering 散射; reflectance 反射率; specular 镜面; diffuse 漫反射; glossy 光泽; light transport equation 光传输方程; rendering equation 渲染方程; participating media 参与介质; phase function 相位函数; albedo 反照率; intersection 求交/交点; bounding box 包围盒; acceleration structure 加速结构; primitive 图元; aggregate 聚合体; shape 形状; normal 法线; tangent 切线; texture 纹理; filtering 滤波; aliasing 走样; antialiasing 反走样; sampling 采样; reconstruction 重建; literate programming 文学编程; fragment 代码片段; camera 相机; film 胶片; lens 透镜; aperture 光圈; depth of field 景深; solid angle 立体角; quadric 二次曲面; rounding error 舍入误差; floating-point 浮点; standard illuminant 标准光源; color space 色彩空间; gamut 色域; chromaticity 色度; white balance 白平衡`;

const SYS = [
  '你是技术书籍翻译引擎。把 JSON 数组中的每个英文段落翻译为简体中文，返回等长 JSON 数组（只返回 JSON，无其他文字）。规则：',
  '1) 形如 ⟦数字⟧ 的占位符是被保护的代码/公式/链接，必须原样保留在译文中语法合适的位置，一个都不能增删或改动。',
  '2) 全角中文标点；人名、系统名（pbrt、RenderMan 等）、书名不翻译；难度标记 ①②③、^&dagger; 等符号原样保留。',
  '3) 术语表（强制）：' + GLOSSARY,
  '4) 强调标记（*粗体*、_斜体_）与链接文字的方括号 [文字] 保持成对，只翻译其中文字。',
  '5) 首次出现的专业术语可用全角括号括注英文原词。',
].join('\n');

// ---------------------------------------------------------------------------
// masking by the engine's AST (plan P3-35; design T1 S12): the translatable
// units are a document's paragraphs, headings and description terms (a
// table's cells, a figure's caption: their region's paragraphs), each its
// source span; inside one, every atom — a code span, a formula, a
// reference, a splice or keyword form, a URL and a link's (target), a
// note's ^[ and ], a statement, a comment, a hard break, a table's cell cut
// — becomes ⟦n⟧, by its engine span. Emphasis markers and a link's [text]
// stay visible to the model. The translations are patched into the source
// in place; everything else is untouched.
// ---------------------------------------------------------------------------
const enc = new TextEncoder(), dec = new TextDecoder();
const ATOMS = new Set(['code', 'math', 'ref', 'linebreak']);
const atomic = (n) => n.kind === 'splice' || n.kind === 'keyword' || n.kind === 'stmt' || n.kind === 'comment' ||
  n.kind === 'error' || (n.kind === 'call' && ATOMS.has(n.sugar));
const hasText = (n) => (n.kind === 'text' ? /\p{L}/u.test(n.str ?? '') : !atomic(n) && (n.kids ?? []).some(hasText));
const kidSpan = (kids) => [Math.min(...kids.map((k) => k.span[0])), Math.max(...kids.map((k) => k.span[1]))];

function units(ast, bytes) {
  const out = [];  // {span, masks: [[s, e]], cells}
  let inRefs = false;
  const masksIn = (kids, masks) => {
    for (const k of kids) {
      if (k.kind === 'text') continue;
      if (atomic(k)) { masks.push(k.span); continue; }
      if (k.kind === 'call' && k.sugar === 'link') {
        const autolink = (k.kids ?? []).length === 1 && k.kids[0].kind === 'text' && k.kids[0].str === k.url &&
          bytes[k.span[0]] !== 0x5b;
        if (autolink || !(k.kids ?? []).length) { masks.push(k.span); continue; }
        masksIn(k.kids, masks);
        masks.push([kidSpan(k.kids)[1], k.span[1]]);  // ](url)
        continue;
      }
      if (k.kind === 'call' && k.sugar === 'note') {
        if ((k.kids ?? []).every((x) => x.kind !== 'call' || !['para', 'list', 'quote'].includes(x.sugar))) {
          masks.push([k.span[0], k.span[0] + 2], [k.span[1] - 1, k.span[1]]);  // ^[ … ]
          masksIn(k.kids ?? [], masks);
        } else masks.push(k.span);
        continue;
      }
      masksIn(k.kids ?? [], masks);  // strong, em: their markers stay
    }
  };
  const unit = (kids, cells) => {
    if (!kids.length || !kids.some(hasText)) return;
    const masks = [];
    masksIn(kids, masks);
    out.push({ span: kidSpan(kids), masks, cells });
  };
  const walk = (n, ctx) => {
    if (n.kind === 'call') {
      switch (n.sugar) {
        case 'heading':
          inRefs = /^(references|参考文献|bibliography)$/i.test((n.kids ?? []).map((k) => k.str ?? '').join('').trim());
          unit(n.kids ?? [], false);
          return;
        case 'para': unit(n.kids ?? [], ctx === 'region'); return;
        case 'termpart': unit(n.kids ?? [], false); return;
        case 'fence': return;
        case 'list': if (inRefs) return; break;
        case 'region': for (const k of n.kids ?? []) walk(k, 'region'); return;
        default: break;
      }
    }
    if (n.kind === 'stmt' || n.kind === 'comment' || n.kind === 'error') return;
    for (const k of n.kids ?? []) walk(k, ctx);
  };
  walk(ast, 'doc');
  return out;
}

// a unit's text for the model: its atoms ⟦n⟧, its line joins one space
function maskUnit(bytes, u, spans) {
  let [s, e] = u.span;
  const masks = u.masks.filter(([a, b]) => b > a).sort((x, y) => x[0] - y[0]);
  let out = '';
  let at = s;
  const plain = (a, b) => {
    let t = dec.decode(bytes.subarray(a, b)).replace(/\n(?:[ \t]*>)*[ \t]*/g, ' ');
    if (u.cells) {  // a table cell cut (an unescaped |) is an atom too
      t = t.replace(/(?<!\\)\|/g, () => { spans.push('|'); return `⟦${spans.length - 1}⟧`; });
    }
    return t;
  };
  for (const [a, b] of masks) {
    if (a < at) continue;  // (nested in one already masked)
    out += plain(at, a);
    spans.push(dec.decode(bytes.subarray(a, b)));
    out += `⟦${spans.length - 1}⟧`;
    at = b;
  }
  out += plain(at, e);
  return out;
}

function maskFile(ast, bytes) {
  const spans = [];  // ⟦n⟧ → its source text
  const us = units(ast, bytes);
  const prose = us.map((u) => maskUnit(bytes, u, spans));
  return { units: us, prose, spans };
}

function unmask(str, spans, usedIds) {
  return str.replace(/⟦(\d+)⟧/g, (m, n) => { usedIds.add(+n); return spans[+n]; });
}

// the source with each unit's span replaced by its translation
function patch(bytes, us, texts) {
  const order = us.map((u, i) => i).sort((a, b) => us[a].span[0] - us[b].span[0]);
  const parts = [];
  let at = 0;
  for (const i of order) {
    const [s, e] = us[i].span;
    parts.push(dec.decode(bytes.subarray(at, s)), texts[i]);
    at = e;
  }
  parts.push(dec.decode(bytes.subarray(at)));
  return parts.join('');
}

// ---------------------------------------------------------------------------
// validation: the translation's tree is the source's, its text aside — the
// same blocks, the same markup, the same atoms (formulas, code, references,
// splices, URLs, labels), the same table cuts
// ---------------------------------------------------------------------------
const INLINE_HOLDERS = new Set(['para', 'heading', 'termpart', 'strong', 'em', 'link', 'note']);
function shape(n) {
  const o = {};
  for (const [k, v] of Object.entries(n)) {
    if (k === 'span' || k === 'rawmap' || k === 'bodyOffset' || k === 'bodyEnd' || k === 'lines' || k === 'str' && n.kind === 'text') continue;
    if (k === 'kids') continue;
    if (k === 'seps') continue;
    o[k] = v;
  }
  let kids = (n.kids ?? []).filter((k) => k.kind !== 'text').map(shape);
  // inside a paragraph, a translation may reorder its atoms (word order):
  // the same ones, in any order — blocks keep theirs
  if (n.kind === 'call' && INLINE_HOLDERS.has(n.sugar)) kids = kids.map((k) => [JSON.stringify(k), k]).sort().map(([, k]) => k);
  if (kids.length) o.kids = kids;
  const cuts = (n.kids ?? []).reduce((c, k) => c + (k.kind === 'text' && k.seps ? k.seps.split(',').length : 0), 0);
  if (cuts) o.cuts = cuts;
  return o;
}
function firstDiff(a, b, path = '') {
  if (typeof a !== 'object' || typeof b !== 'object' || !a || !b) return a === b ? null : `${path}: ${JSON.stringify(a)?.slice(0, 80)} ≠ ${JSON.stringify(b)?.slice(0, 80)}`;
  for (const k of new Set([...Object.keys(a), ...Object.keys(b)])) {
    const d = firstDiff(a[k], b[k], `${path}/${k}${a.sugar ? `(${a.sugar})` : ''}`);
    if (d) return d;
  }
  return null;
}
async function validate(en, zh) {
  const d = firstDiff(shape(await parseTsm(en)), shape(await parseTsm(zh)));
  return d ? [d] : [];
}

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------
async function chat(items, attempt = 0) {
  const res = await fetch(BASE.replace(/\/$/, '') + '/chat/completions', {
    method: 'POST',
    headers: { 'content-type': 'application/json', authorization: 'Bearer ' + KEY },
    body: JSON.stringify({
      model: MODEL, temperature: 0.2, stream: false,
      messages: [{ role: 'system', content: SYS },
                 { role: 'user', content: JSON.stringify(items) }],
    }),
  });
  if (!res.ok) {
    if (attempt < 4) { await new Promise((r) => setTimeout(r, 2000 * (attempt + 1))); return chat(items, attempt + 1); }
    throw new Error('API ' + res.status + ': ' + (await res.text()).slice(0, 300));
  }
  const j = await res.json();
  let txt = j.choices?.[0]?.message?.content ?? '';
  txt = txt.replace(/^```(?:json)?\s*/, '').replace(/```\s*$/, '').trim();
  const arr = JSON.parse(txt);
  if (!Array.isArray(arr) || arr.length !== items.length) throw new Error('length mismatch ' + arr?.length + ' vs ' + items.length);
  return arr.map(String);
}

async function translateProse(prose) {
  // chunk by size; on a failed chunk, retry item-by-item
  const outArr = new Array(prose.length);
  const chunks = [];
  let cur = [], size = 0;
  for (let i = 0; i < prose.length; i++) {
    cur.push(i); size += prose[i].length;
    if (size > 6000 || cur.length >= 25) { chunks.push(cur); cur = []; size = 0; }
  }
  if (cur.length) chunks.push(cur);
  for (const ids of chunks) {
    const items = ids.map((i) => prose[i]);
    let got;
    try { got = await chat(items); }
    catch (e) {
      got = [];
      for (const it of items) got.push((await chat([it]))[0]);   // singleton fallback
    }
    ids.forEach((i, k) => { outArr[i] = got[k]; });
  }
  return outArr;
}

// ---------------------------------------------------------------------------
// drive
// ---------------------------------------------------------------------------
const files = [];
for (const ch of readdirSync(SRC)) {
  const d = join(SRC, ch);
  if (!statSync(d).isDirectory()) continue;
  for (const f of readdirSync(d)) if (f.endsWith('.tsm')) files.push(ch + '/' + f);
}
files.sort();

const todo = files.filter((f) => {
  if (ONLY && !f.includes(ONLY)) return false;
  if (CHECK) return existsSync(join(DST, f));
  return FORCE || !existsSync(join(DST, f));
});
console.log((CHECK ? 'checking' : 'translating') + ' ' + todo.length + ' files (skipped ' + (files.length - todo.length) + ')');

let pass = 0, fail = 0;
async function one(rel) {
  const en = readFileSync(join(SRC, rel), 'utf8');
  if (CHECK) {
    const zh = readFileSync(join(DST, rel), 'utf8');
    const errs = await validate(en, zh);
    console.log((errs.length ? 'FAIL ' : 'ok   ') + rel + (errs.length ? '  [' + errs.join('; ') + ']' : ''));
    errs.length ? fail++ : pass++;
    return;
  }
  const bytes = enc.encode(en);
  const { units: us, prose, spans } = maskFile(await parseTsm(en), bytes);
  if (DRY) {
    console.log(rel + ': ' + prose.length + ' units, ' + spans.length + ' protected atoms');
    if (SHOW) prose.forEach((p, i) => console.log(`  [${i}] ${p}`));
    return;
  }
  try {
    const zhProse = await translateProse(prose);
    const usedIds = new Set();
    const texts = zhProse.map((s) => unmask(s, spans, usedIds));
    // every atom restored exactly once; none invented
    if (usedIds.size !== spans.length) throw new Error('placeholder loss: ' + usedIds.size + '/' + spans.length);
    if (texts.some((s) => /⟦\d+⟧/.test(s))) throw new Error('unresolved placeholder');
    let zh = patch(bytes, us, texts);
    // a leading attribution comment (the converters') notes the translation
    zh = zh.replace(/^%--([\s\S]*?)--%/, (m, body) => `%--${body.trimEnd()}\n中文为本地私用机器翻译（${MODEL}），未经授权不得传播。 --%`);
    const errs = await validate(en, zh);
    if (errs.length) throw new Error('validation: ' + errs.join('; '));
    mkdirSync(dirname(join(DST, rel)), { recursive: true });
    writeFileSync(join(DST, rel), zh);
    console.log('ok   ' + rel + '  (' + prose.length + ' paras)');
    pass++;
  } catch (e) {
    mkdirSync(dirname(join(DST, rel)), { recursive: true });
    writeFileSync(join(DST, rel) + '.reject', String(e.message ?? e));
    console.log('FAIL ' + rel + '  ' + String(e.message ?? e).slice(0, 120));
    fail++;
  }
}

const queue = [...todo];
await Promise.all(Array.from({ length: Math.min(CONC, queue.length) }, async () => {
  for (;;) { const f = queue.shift(); if (!f) return; await one(f); }
}));
console.log('done: ' + pass + ' ok, ' + fail + ' failed' + (fail ? ' (see .reject files; rerun to retry them after deleting)' : ''));
