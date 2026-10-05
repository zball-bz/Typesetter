#!/usr/bin/env python3
# The math vocabulary compiler (plan P1-22; design T8 MathDict):
# engine/data/math/symbols.tsv + the pinned UCD (engine/rules/ucd/17.0.0/
# UnicodeData.txt) + the pinned MathML Core operator dictionary
# (engine/data/mathml/operator-dictionary.tsv) -> engine/gen/math_dict.h,
# engine/src/math/atom.h and runtime/src/shared/math-vocab.gen.mjs.
# Font-independent: the font compiler (tools/mathc.py) no longer holds any
# vocabulary. Plain python3, no third-party modules, no unicodedata (only the
# vendored files decide). Run by tools/gen-all.mjs; --check fails on stale
# outputs.
#
# Build gates (the run fails):
#   - a vendored file whose version differs from the pinned one;
#   - duplicate names; a malformed row;
#   - a code point with more than one (non-accent) row and not exactly one
#     default_for_cp row;
#   - a key mixing ASCII letters and operator characters, outside the
#     allowlist (the !word rows; plan step S4 removes them);
#   - an operator key longer than the lexer's 4-character munch;
#   - a class that differs from what MathML Core derives, unless the row
#     says class_source=override.
import os, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), '..'))
CHECK = '--check' in sys.argv
UCD_VERSION = '17.0.0'
MATHML_VERSION = 'b07c0b3ecf985e3a7654439651c61fc8744fc8f4'
CLASSES = ['ord', 'op', 'bin', 'rel', 'open', 'close', 'punct', 'inner']
FLAGS = {'large': 1, 'limits': 4, 'textop': 8}
OP_CHARS = set('+-*=<>|~:;.,!@&?%')  # the lexer's operator characters (math.cc isOpChar)
MAX_OP_KEY = 4

def fail(msg):
    print('mathdict: ' + msg, file=sys.stderr)
    sys.exit(1)

def rel(*p): return os.path.join(ROOT, *p)

# ---- the pinned sources ------------------------------------------------------
ucd = rel('engine/rules/ucd', UCD_VERSION, 'UnicodeData.txt')
if not os.path.exists(ucd): fail('missing ' + ucd + ' (node tools/ucdc.mjs --fetch)')
negation = {}
for line in open(ucd, encoding='utf-8'):
    f = line.split(';')
    d = f[5].split()
    if len(d) == 2 and d[1] == '0338' and not d[0].startswith('<'):
        negation[int(d[0], 16)] = int(f[0], 16)

mml = {}
version = None
for line in open(rel('engine/data/mathml/operator-dictionary.tsv'), encoding='utf-8'):
    line = line.rstrip('\n')
    if line.startswith('#') or not line: continue
    cols = line.split('\t')
    if cols[0] == 'version':
        version = cols[1]
        continue
    cps, axis, form, l, r, props = cols
    cps = cps.split()
    if len(cps) != 1: continue
    mml.setdefault(int(cps[0], 16), {})[form] = (float(l), props.split())
if version != MATHML_VERSION: fail('operator dictionary version %s, pinned %s' % (version, MATHML_VERSION))

def mathml_class(cp):
    e = mml.get(cp)
    if not e: return 'ord'
    if 'infix' in e:
        l, p = e['infix']
        if 'largeop' in p: return 'op'
        if abs(l - 5 / 18) < 1e-6: return 'rel'
        if abs(l - 4 / 18) < 1e-6: return 'bin'
        if 'separator' in p: return 'punct'
        return 'ord'
    if 'prefix' in e:
        l, p = e['prefix']
        if 'largeop' in p: return 'op'
        return 'open' if 'fence' in p else 'ord'
    l, p = e['postfix']
    return 'close' if 'fence' in p else 'ord'

# ---- symbols.tsv -------------------------------------------------------------
rows = []
header = None
for n, line in enumerate(open(rel('engine/data/math/symbols.tsv'), encoding='utf-8'), 1):
    line = line.rstrip('\n')
    if line.startswith('#') or not line: continue
    cols = line.split('\t')
    if header is None:
        header = cols
        continue
    if len(cols) != len(header): fail('symbols.tsv:%d: %d columns, expected %d' % (n, len(cols), len(header)))
    r = dict(zip(header, cols))
    r['cp'] = int(r['cp'], 16)
    if r['class'] not in CLASSES: fail('symbols.tsv:%d: unknown class %s' % (n, r['class']))
    flags = 0
    for f in ([] if r['flags'] == '-' else r['flags'].split(',')):
        if f not in FLAGS: fail('symbols.tsv:%d: unknown flag %s' % (n, f))
        flags |= FLAGS[f]
    r['bits'] = flags
    r['line'] = n
    rows.append(r)

names = [r['name'] for r in rows]
if len(names) != len(set(names)):
    fail('duplicate names: ' + ' '.join(sorted({x for x in names if names.count(x) > 1})))
by_cp = {}
for r in rows:
    if r['cp']: by_cp.setdefault(r['cp'], []).append(r)
for cp, rs in by_cp.items():
    d = [r for r in rs if r['default_for_cp'] == 'y']
    if len(d) != 1: fail('U+%04X: %d rows, %d default_for_cp rows' % (cp, len(rs), len(d)))
for r in rows:
    k = r['name']
    letters = any(c.isascii() and c.isalpha() for c in k)
    ops = any(c in OP_CHARS for c in k)
    if letters and ops and not (k.startswith('!') and k[1:].isalpha()):
        fail('symbols.tsv:%d: key %r mixes letters and operator characters' % (r['line'], k))
    if ops and not letters and all(c in OP_CHARS for c in k) and len(k) > MAX_OP_KEY:
        fail('symbols.tsv:%d: operator key %r is longer than the %d-character munch' % (r['line'], k, MAX_OP_KEY))
    if r['cp'] and r['class_source'] != 'override' and mathml_class(r['cp']) != r['class']:
        fail('symbols.tsv:%d: %s is %s, MathML Core derives %s (mark class_source=override)'
             % (r['line'], k, r['class'], mathml_class(r['cp'])))
    want = negation.get(r['cp'])
    if r['negation'] != ('%04X' % want if want else '-'):
        fail('symbols.tsv:%d: negation %s, the UCD gives %s' % (r['line'], r['negation'], '%04X' % want if want else '-'))

# ---- outputs -----------------------------------------------------------------
HDR = 'GENERATED from engine/data/math/symbols.tsv by tools/mathdict.py — do not edit.'
srt = sorted(rows, key=lambda r: r['name'].encode())  # strcmp order
def cstr(s): return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'

# the operator-key trie (keys of operator characters only, the lexer's munch)
trie = [{'c': '', 'kids': {}, 'sym': -1}]
for i, r in enumerate(srt):
    k = r['name']
    if not k or not all(c in OP_CHARS for c in k): continue
    node = 0
    for c in k:
        if c not in trie[node]['kids']:
            trie.append({'c': c, 'kids': {}, 'sym': -1})
            trie[node]['kids'][c] = len(trie) - 1
        node = trie[node]['kids'][c]
    trie[node]['sym'] = i
flat = []  # (c, firstKid, nextSibling, sym), node 0 the root; kids contiguous, in char order
order = {0: 0}
queue = [0]
while queue:
    n = queue.pop(0)
    kids = sorted(trie[n]['kids'].items())
    for c, k in kids:
        order[k] = len(order)
        queue.append(k)
nodes = sorted(order, key=lambda n: order[n])
for n in nodes:
    kids = sorted(trie[n]['kids'].values(), key=lambda k: order[k])
    flat.append((trie[n]['c'], order[kids[0]] if kids else 0, len(kids), trie[n]['sym']))

h = ['// ' + HDR,
     '// The font-independent math vocabulary (docs/math-design.md §13): every',
     '// spelling with its code point, TeX atom class and flags; the class a bare',
     '// code point takes (its default row); the operator-key trie of the lexer;',
     '// the UCD negations. engine/src/math/dict.h is the API.',
     '#pragma once',
     '#include <cstdint>',
     '',
     'namespace tsr { namespace mathdict {',
     '',
     'struct Symbol {',
     '  const char* name;',
     '  uint32_t cp;  // 0: a text operator, set in letters',
     '  uint8_t cls, flags;',
     '};',
     'inline constexpr Symbol kSymbols[] = {  // sorted by name (strcmp)']
for r in srt:
    h.append('  {%s,0x%X,%d,%d},' % (cstr(r['name']), r['cp'], CLASSES.index(r['class']), r['bits']))
h += ['};', 'inline constexpr int kSymbolCount = %d;' % len(srt), '',
      '// a bare code point\'s class: its default_for_cp row',
      'struct CpClass {', '  uint32_t cp;', '  uint8_t cls;', '};',
      'inline constexpr CpClass kCpClasses[] = {  // sorted by cp']
for cp in sorted(by_cp):
    d = [r for r in by_cp[cp] if r['default_for_cp'] == 'y'][0]
    h.append('  {0x%X,%d},' % (cp, CLASSES.index(d['class'])))
h += ['};', 'inline constexpr int kCpClassCount = %d;' % len(by_cp), '',
      '// operator keys (operator characters only, at most %d): node 0 is the' % MAX_OP_KEY,
      '// root; a node\'s kids are contiguous from `kid`, in character order',
      'struct TrieNode {', '  char c;', '  uint16_t kid, nKids;', '  int16_t sym;  // into kSymbols, -1 = not a key', '};',
      'inline constexpr TrieNode kOpTrie[] = {']
for c, kid, nk, sym in flat:
    h.append("  {%s,%d,%d,%d}," % ("'\\0'" if c == '' else ("'\\\\'" if c == '\\' else "'%s'" % c), kid, nk, sym))
neg = sorted(negation.items())
h += ['};', '', '// the UCD negations: a code point and its precomposed form with U+0338',
      'struct Negation {', '  uint32_t cp, neg;', '};', 'inline constexpr Negation kNegations[] = {']
h += ['  {0x%X,0x%X},' % (a, b) for a, b in neg]
h += ['};', 'inline constexpr int kNegationCount = %d;' % len(neg), '', '}}  // namespace tsr::mathdict', '']

atom = ['// ' + HDR.replace('engine/data/math/symbols.tsv', 'the class and flag columns of symbols.tsv'),
        '// TeX atom classes and symbol flags (plan P1-22: the vocabulary is the',
        '// dictionary\'s, not the font compiler\'s).',
        '#pragma once', '#include <cstdint>', '', 'namespace tsr {', '',
        'enum AtomClass : uint8_t { ' + ', '.join('k' + c.capitalize() for c in CLASSES) + ' };',
        'enum SymFlag : uint8_t {',
        '  kFlagLarge = 1,    // a large operator (display size)',
        '  kFlagLimits = 4,   // limits above/below in display style',
        '  kFlagTextOp = 8,   // a multi-letter operator set upright in text',
        '};', '', '}  // namespace tsr', '']

js = ['// ' + HDR, '// name → [code point, class] for tools (converters, the editor).',
      'export const MATH_SYMBOLS = {']
js += ['  %s: [0x%X, %s],' % (cstr(r['name']), r['cp'], cstr(r['class'])) for r in srt]
js += ['};', '']

# ---- stdlib.tsv: the template rows (parsed and checked by the engine) --------
rows_h = ['// ' + HDR.replace('symbols.tsv', 'stdlib.tsv'),
          '// The built-in math function rows (engine/src/math/ir.cc parses and checks',
          '// them at start-up: checkRow).',
          '#pragma once', '', 'namespace tsr { namespace mathrows {', '',
          'struct StdRow {', '  const char* signature;', '  const char* body;',
          '  const char* bare;  // "" = none', '};', 'inline constexpr StdRow kStdlib[] = {']
seen = set()
header2 = None
for n, line in enumerate(open(rel('engine/data/math/stdlib.tsv'), encoding='utf-8'), 1):
    line = line.rstrip('\n')
    if line.startswith('#') or not line: continue
    cols = line.split('\t')
    if header2 is None:
        header2 = cols
        continue
    if len(cols) != 3: fail('stdlib.tsv:%d: %d columns, expected 3' % (n, len(cols)))
    sig, body, bare = cols
    name = sig.split('(')[0]
    if name in seen: fail('stdlib.tsv:%d: duplicate row %s' % (n, name))
    seen.add(name)
    if bare != '-' and len(bare) > 1 and bare not in names: fail('stdlib.tsv:%d: bare %s is not a symbol' % (n, bare))
    rows_h.append('  {%s, %s, %s},' % (cstr(sig), cstr(body), cstr('' if bare == '-' else bare)))
rows_h += ['};', '', '}}  // namespace tsr::mathrows', '']

outputs = {
    'engine/gen/math_rows.h': '\n'.join(rows_h),
    'engine/gen/math_dict.h': '\n'.join(h),
    'engine/src/math/atom.h': '\n'.join(atom),
    'runtime/src/shared/math-vocab.gen.mjs': '\n'.join(js),
}
stale = 0
for path, text in outputs.items():
    p = rel(path)
    prev = open(p, encoding='utf-8').read() if os.path.exists(p) else None
    if prev == text: continue
    if CHECK:
        print('mathdict: %s is stale (run python3 tools/mathdict.py)' % path, file=sys.stderr)
        stale += 1
        continue
    with open(p, 'w', encoding='utf-8') as f: f.write(text)
    print('wrote ' + path)
sys.exit(1 if stale else 0)
