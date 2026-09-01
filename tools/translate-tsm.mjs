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
//        [--concurrency 3] [--dry] [--check] [--force]
//
// Files already present in --dst are skipped (resume-friendly). Output is
// written only after ALL validations pass; failures leave a .reject file
// with diagnostics instead. --check re-validates existing outputs only.
import { readFileSync, writeFileSync, readdirSync, mkdirSync, statSync, existsSync } from 'node:fs';
import { join, dirname } from 'node:path';

const args = process.argv.slice(2);
const opt = (k, d) => (args.includes(k) ? args[args.indexOf(k) + 1] : d);
const SRC = opt('--src'); const DST = opt('--dst');
const MODEL = opt('--model', 'glm-5.3');
const BASE = opt('--base', 'https://api.z.ai/api/coding/paas/v4');
const ONLY = opt('--only', null);
const CONC = parseInt(opt('--concurrency', '3'), 10);
const DRY = args.includes('--dry');
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
  '4) 行首的结构标记（= == === 标题层级、- 列表符、_强调_ 下划线对）保持不变，只翻译其中文字。',
  '5) 首次出现的专业术语可用全角括号括注英文原词。',
].join('\n');

// ---------------------------------------------------------------------------
// masking
// ---------------------------------------------------------------------------
const INLINE_RE = /(\\\$(?:[^$\\]|\\.)*?\\\$|(?<!\\)\$[^$\n]+?(?<!\\)\$|`[^`\n]+`|\]\([^)\n]+\)|<<[^<>\n]+>>)/g;

function maskFile(text) {
  const lines = text.split('\n');
  const out = [];          // template entries: {t:'raw',s} | {t:'prose',id}
  const prose = [];        // prose strings with inline placeholders
  const spans = [];        // inline placeholder id → original
  let inFence = false, inRefs = false;
  for (const l of lines) {
    if (l.startsWith('```')) { inFence = !inFence; out.push({ t: 'raw', s: l }); continue; }
    if (inFence) { out.push({ t: 'raw', s: l }); continue; }
    if (/^== References/.test(l) || /^== 参考文献/.test(l)) inRefs = true;
    else if (/^==? /.test(l) && inRefs) inRefs = false;
    const keep = l.startsWith('//') || l.startsWith('#!figure') || l === '#figure!' ||
      /^\$ .*\$$/.test(l) || l.trim() === '' || (inRefs && l.startsWith('- '));
    if (keep) { out.push({ t: 'raw', s: l }); continue; }
    const masked = l.replace(INLINE_RE, (m) => {
      // keep the "](" prefix of link targets outside the span
      if (m.startsWith('](')) { spans.push(m.slice(1)); return ']⟦' + (spans.length - 1) + '⟧'; }
      spans.push(m); return '⟦' + (spans.length - 1) + '⟧';
    });
    out.push({ t: 'prose', id: prose.length });
    prose.push(masked);
  }
  return { template: out, prose, spans };
}

function unmask(str, spans, usedIds) {
  return str.replace(/⟦(\d+)⟧/g, (m, n) => { usedIds.add(+n); return spans[+n]; });
}

// ---------------------------------------------------------------------------
// validation (same checks the manual pipeline used)
// ---------------------------------------------------------------------------
function blocks(x) {
  const o = []; let f = false, cur = [];
  for (const l of x.split('\n')) {
    if (l.startsWith('```')) { if (f) { o.push(cur.join('\n')); cur = []; } f = !f; }
    else if (f) cur.push(l);
  }
  return o;
}
function counter(arr) { const c = new Map(); for (const x of arr) c.set(x, (c.get(x) ?? 0) + 1); return c; }
function eqCounter(a, b) {
  if (a.size !== b.size) return false;
  for (const [k, v] of a) if (b.get(k) !== v) return false;
  return true;
}
function mathset(x) {
  const body = x.split('\n').filter((l) => !l.startsWith('```')).join('\n');
  return counter(body.match(/(?<!\\)\$[^$\n]+?(?<!\\)\$/g) ?? []);
}
function validate(en, zh) {
  const errs = [];
  if (en.split('```').length !== zh.split('```').length) errs.push('fence count');
  const be = blocks(en), bz = blocks(zh);
  if (be.length !== bz.length || be.some((b, i) => b !== bz[i])) errs.push('code blocks differ');
  const fe = en.match(/^#!figure\(.*$/gm) ?? [], fz = zh.match(/^#!figure\(.*$/gm) ?? [];
  if (fe.join('\n') !== fz.join('\n')) errs.push('figure param lines differ');
  const de = en.match(/^\$ .*\$$/gm) ?? [], dz = zh.match(/^\$ .*\$$/gm) ?? [];
  if (de.join('\n') !== dz.join('\n')) errs.push('display math differs');
  if (!eqCounter(mathset(en), mathset(zh))) errs.push('inline math multiset differs');
  if ((en.match(/\\\$/g) ?? []).length !== (zh.match(/\\\$/g) ?? []).length) errs.push('escaped-$ count differs');
  return errs;
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
    const errs = validate(en, zh);
    console.log((errs.length ? 'FAIL ' : 'ok   ') + rel + (errs.length ? '  [' + errs.join('; ') + ']' : ''));
    errs.length ? fail++ : pass++;
    return;
  }
  const { template, prose, spans } = maskFile(en);
  if (DRY) {
    console.log(rel + ': ' + prose.length + ' prose lines, ' + spans.length + ' protected spans, ' +
      template.filter((e) => e.t === 'raw').length + ' raw lines');
    return;
  }
  try {
    const zhProse = await translateProse(prose);
    const usedIds = new Set();
    const outLines = template.map((e) => e.t === 'raw' ? e.s : unmask(zhProse[e.id], spans, usedIds));
    // every span must be restored exactly once; none invented
    if (usedIds.size !== spans.length) throw new Error('placeholder loss: ' + usedIds.size + '/' + spans.length);
    let zh = outLines.join('\n');
    if (/⟦\d+⟧/.test(zh)) throw new Error('unresolved placeholder');
    const anchor = '// Local private adaptation; do not redistribute.\n';
    if (zh.includes(anchor)) zh = zh.replace(anchor, anchor + '// 中文为本地私用机器翻译（' + MODEL + '），未经授权不得传播。\n');
    const errs = validate(en, zh);
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
