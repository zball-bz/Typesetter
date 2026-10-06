#!/usr/bin/env node
// engine/schema/languages.json → the code-highlight tables (plan P3-22;
// design T9 A6):
// - the worker's and the editor's: runtime/src/shared/languages.gen.mjs,
//   read through runtime/src/shared/hl-core.mjs;
// - the extension's legend: editors/vscode-tsm/src/hl.gen.js;
// - the engine's: engine/src/code/languages.gen.h (capture aliases,
//   language aliases, overlays, profiles);
// - the native build's grammar list: engine/native_grammars.gen.cmake. Run by tools/gen-all.mjs; `--check`
// fails on stale outputs. Refuses a manifest whose classes are not
// syntax.def's TOKEN_TAGS, whose theme lacks a class's rule, or whose native
// grammar's queries use a regular-expression predicate.
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const CHECK = process.argv.includes('--check');
const HDR = 'GENERATED from engine/schema/languages.json by tools/gen-languages.mjs — do not edit.';
const M = JSON.parse(readFileSync(join(root, 'engine/schema/languages.json'), 'utf8'));
const fail = (msg) => { throw new Error(`languages.json: ${msg}`); };

// the classes are syntax.def's token tags, in their order
const syntax = readFileSync(join(root, 'engine/src/syntax/syntax.def'), 'utf8');
const TAGS = /^TOKEN_TAGS\(([^)]*)\)/m.exec(syntax)?.[1].split(',').map((s) => s.trim()) ?? fail('syntax.def has no TOKEN_TAGS');
const classes = Object.keys(M.classes);
if (classes.join() !== TAGS.join()) fail(`classes must be syntax.def's TOKEN_TAGS in order (${TAGS.join(', ')})`);
const tagOf = (c, where) => { const t = TAGS.indexOf(c); if (t < 0) fail(`${where}: no class ${c}`); return t; };
for (const [a, c] of Object.entries(M.alias)) {
  if (TAGS.includes(a)) fail(`alias ${a} is a class`);
  tagOf(c, `alias ${a}`);
}
// the theme colours every class (runtime/src/main/theme.css: .tsr-c-tok-<class>)
const theme = readFileSync(join(root, 'runtime/src/main/theme.css'), 'utf8');
for (const c of TAGS)
  if (!theme.includes(`.tsr-c-tok-${c}`)) fail(`theme.css has no .tsr-c-tok-${c} rule`);

// languages: a canonical name, its aliases; every tag lowercase and unique
const langOf = {};
const claim = (tag, lang, where) => {
  if (!/^[a-z0-9][a-z0-9+#.-]*$/.test(tag)) fail(`${where}: tag ${tag} is not lowercase [a-z0-9+#.-]`);
  if (tag in langOf) fail(`${where}: tag ${tag} is already ${langOf[tag]}'s`);
  langOf[tag] = lang;
};
for (const [name, l] of Object.entries(M.languages)) {
  claim(name, name, `language ${name}`);
  for (const a of l.aliases ?? []) claim(a, name, `language ${name}`);
  const g = l.grammar ?? fail(`language ${name} has no grammar`);
  for (const f of [...g.files.map((x) => join(g.src, x)), ...g.scm])
    if (!f.startsWith('node_modules/') && !existsSync(join(root, f))) fail(`language ${name}: ${f} is missing`);
  if (l.native) {
    if (!/^[a-z_][a-z0-9_]*$/.test(name)) fail(`native language ${name}: its name is the C symbol tree_sitter_${name}`);
    for (const f of g.scm) {
      const scm = readFileSync(join(root, f), 'utf8');
      const re = /#(not-)?match\?/.exec(scm);
      if (re) fail(`native language ${name}: ${f} uses ${re[0]} (the native twin evaluates only #eq?, #not-eq?, #any-of?, #not-any-of?)`);
    }
  }
}
const overlayIds = Object.keys(M.overlays);
if (overlayIds.length > 32) fail('at most 32 overlays');
for (const [name, o] of Object.entries(M.overlays)) {
  if (!/^[A-Za-z_][A-Za-z0-9_-]*$/.test(name)) fail(`overlay ${name}: a name is an identifier`);
  if (!o.open || !o.close) fail(`overlay ${name}: open and close are not empty`);
  if ((o.suffix ?? []).length > 4) fail(`overlay ${name}: at most 4 suffixes`);
  tagOf(o.class, `overlay ${name}`);
}
for (const [tag, p] of Object.entries(M.profiles)) {
  if (!(p.lang in M.languages)) fail(`profile ${tag}: no language ${p.lang}`);
  for (const o of p.overlays ?? []) if (!(o in M.overlays)) fail(`profile ${tag}: no overlay ${o}`);
  claim(tag, p.lang, `profile ${tag}`);
}

// ---- runtime/src/shared/languages.gen.mjs ----------------------------------
const js = `// ${HDR}
// The code-highlight manifest's tables (plan P3-22): read by hl-core.mjs, the
// worker's token provider, the editor and the stdlib (fence profiles).
// LANGUAGES: a canonical language → its grammar asset basename
// (runtime/assets/hl: tree-sitter-<asset>.wasm, <asset>.scm). LANG_OF: a
// fence tag (lowercased: a name, an alias, a profile) → its language.
// CAPTURE_ALIAS: a capture's first segment → its class. EDITOR_TYPES: a
// class → the editor's semantic token type (null: plain). OVERLAYS and
// PROFILES as in the manifest.
export const LANGUAGES = Object.freeze(${JSON.stringify(Object.fromEntries(Object.keys(M.languages).map((n) => [n, { asset: n }])))});
export const LANG_OF = Object.freeze(${JSON.stringify(langOf)});
export const CAPTURE_ALIAS = Object.freeze(${JSON.stringify(M.alias)});
export const EDITOR_TYPES = Object.freeze(${JSON.stringify(M.classes)});
export const OVERLAYS = Object.freeze(${JSON.stringify(M.overlays)});
export const PROFILES = Object.freeze(${JSON.stringify(M.profiles)});
`;

// ---- engine/src/code/languages.gen.h ---------------------------------------
const cstr = (s) => JSON.stringify(s);  // (ASCII and \n only: a JSON string is a C++ literal)
for (const s of Object.values(M.overlays).flatMap((o) => [o.open, o.close, o.forbid ?? '', ...(o.suffix ?? [])]))
  if (!/^[\x20-\x7e\n]*$/.test(s)) fail(`an overlay's delimiters are printable ASCII (${JSON.stringify(s)})`);
const maskOf = (names) => (names ?? []).reduce((m, n) => m | (1 << overlayIds.indexOf(n)), 0) >>> 0;
const h = `// ${HDR}
// The code-highlight manifest's engine tables (plan P3-22; design T9 A6).
#pragma once
#include <cstdint>
#include <string_view>

namespace tsr {

// a capture name's first segment → its token tag (kTokenTags), beyond the tags themselves
struct CaptureAlias {
  std::string_view name;
  int tag;
};
inline constexpr CaptureAlias kCaptureAliases[] = {
${Object.entries(M.alias).map(([a, c]) => `    {${cstr(a)}, ${TAGS.indexOf(c)}},  // → ${c}`).join('\n')}
};

// a fence tag → its built-in language (names, aliases and profiles)
struct LangAlias {
  std::string_view tag, lang;
};
inline constexpr LangAlias kLangAliases[] = {
${Object.entries(langOf).map(([t, l]) => `    {${cstr(t)}, ${cstr(l)}},`).join('\n')}
};

// an overlay (code/overlay.h): open, a name of no forbid character, close,
// then at most one of the suffixes (longest listed first), one token of tag
struct OverlaySpec {
  std::string_view name, open, close, forbid;
  std::string_view suffix[4];
  std::uint8_t nSuffix;
  int tag;
};
inline constexpr OverlaySpec kOverlays[] = {
${Object.entries(M.overlays).map(([n, o]) => {
  const suf = [...(o.suffix ?? []), '', '', '', ''].slice(0, 4).map(cstr).join(', ');
  return `    {${cstr(n)}, ${cstr(o.open)}, ${cstr(o.close)}, ${cstr(o.forbid ?? '')}, {${suf}}, ${(o.suffix ?? []).length}, ${TAGS.indexOf(o.class)}},`;
}).join('\n')}
};
inline constexpr std::uint32_t kOverlayCount = ${overlayIds.length};

// a fence tag that means a language with overlays (bit k: kOverlays[k])
struct LangProfile {
  std::string_view tag, lang;
  std::uint32_t overlays;
};
inline constexpr LangProfile kProfiles[] = {
${Object.entries(M.profiles).map(([t, p]) => `    {${cstr(t)}, ${cstr(p.lang)}, ${maskOf(p.overlays)}u},`).join('\n')}
};

}  // namespace tsr
`;

// ---- engine/native_grammars.gen.cmake --------------------------------------
const natives = Object.entries(M.languages).filter(([, l]) => l.native);
const cmake = `# ${HDR}
# The grammars the native build links (plan P3-22): paths from the repository root.
set(TSR_NATIVE_GRAMMARS ${natives.map(([n]) => n).join(' ')})
${natives.map(([n, l]) => [
  `set(TSR_GRAMMAR_${n}_DIR ${l.grammar.src})`,
  `set(TSR_GRAMMAR_${n}_FILES ${l.grammar.files.join(' ')})`,
  `set(TSR_GRAMMAR_${n}_SCM ${l.grammar.scm.join(' ')})`,
].join('\n')).join('\n')}
`;

// ---- editors/vscode-tsm/src/hl.gen.js (CommonJS: the extension's legend is
// needed synchronously at activation) -----------------------------------------
const legend = [...new Set(Object.values(M.classes).filter(Boolean))];
const editor = `// ${HDR}
// The editor's semantic token types (plan P3-22): a class → VSCode's standard
// type (null: left plain), and the legend in the classes' order.
'use strict';
const TYPE_OF = Object.freeze(${JSON.stringify(M.classes)});
const LEGEND = Object.freeze(${JSON.stringify(legend)});
module.exports = { TYPE_OF, LEGEND };
`;

const outputs = {
  'runtime/src/shared/languages.gen.mjs': js,
  'editors/vscode-tsm/src/hl.gen.js': editor,
  'engine/src/code/languages.gen.h': h,
  'engine/native_grammars.gen.cmake': cmake,
};
let stale = 0;
for (const [rel, text] of Object.entries(outputs)) {
  const p = join(root, rel);
  const prev = existsSync(p) ? readFileSync(p, 'utf8') : null;
  if (prev === text) continue;
  if (CHECK) { console.error(`gen-languages: ${rel} is stale (run node tools/gen-languages.mjs)`); stale++; continue; }
  writeFileSync(p, text);
  console.log(`wrote ${rel}`);
}
if (stale) process.exit(1);
