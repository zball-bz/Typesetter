# T8-math

> **Audit design input (generated, English).** Produced by the 2026-10-05 audit (architect → two adversarial critics → revision). Where it conflicts with `docs/remediation/PLAN.md` (decision register §3) or with `INTEGRATION.md` (conflict resolutions), PLAN.md wins, then INTEGRATION.md. Step ids are mapped to plan steps in the Migration section. Line numbers refer to commit ecc3a89.

## Thesis

Math becomes a closed sub-language with one identity per concept and ONE row type per kind of extension, bound where styles bind. (1) Every spelling of a symbol resolves to one SymbolInfo from a generated, font-independent MathDict. (2) Every construct is a Call to a MathRow. A closed set of C++ primitives does the work: 14 layout primitives plus 4 bind-time rewrites. Every family (abs, binom, hat, bb, display, limits, pmat, overbrace...) is a template row written in the same template language a document uses, gated by the same validator. So a built-in `abs(x)` and a user `myabs(x) = lr(|, #x, |)` reduce to identical IR by construction. (3) Math crosses ops as one `math` node whose kids are `mathsrc` fragments (each with its own span) and typed holes. Declarations are schedule-ordered decl EMITs, applied at instantiation like STYLE_PUSH. Each math node is stamped with that declaration epoch, so names bind at the emission position even after the resolver moves or clones the node. Parsing is lazy, engine-side and memoized. Holes are isolated for parsing but transparent for layout. (4) The font is a runtime MathFont in a process-wide registry, with a coverage chain that ends in measured text leaves. (5) Layout produces generic T5 items with producer-authoritative penalties, through a generic pending-object finalize. A multi-row display is a group of row equations, so numbering stays with T3 and pagination with T6. No layer outside engine/src/math needs to know what a formula is.

## Diagnosis

Math was built as a sealed island: a string through ops, parsed only at emit (math-design §10). The ad-hoc items follow from three structural gaps and two integration gaps.

(1) There is no vocabulary layer. The lexicon is hand-curated inside the font compiler (tools/mathc.py:96-252) and filtered by one font's cmap (mathc.py:324-333). It even decides which glyph records get compiled: mathc.py:349-350 adds dictionary cps, including six accents outside RANGES. Every consumer then re-encodes the data:
- lexer branches (math.cc:95-213);
- a first-hit linear cp scan (math.cc:220-225);
- token-kind predicates (math.cc:264);
- the converters (tex2tsm.mjs:116-143).
Spelling, not symbol identity, decides behaviour.

(2) There is no call model and no row type. Constructs are two disjoint name lists (math.cc:231-235, :1178-1209) plus dedicated node kinds with no arity or slot spec. Families are either enumerated or missing, and any error degrades the whole formula (math.cc:1253-1256). The only 'extension' is the implicit-name fallback (math.cc:458-468), so documents can express nothing that a built-in does.

(3) There is no declaration or value channel. Math crosses ops as one ArgK::src string (executor.mjs:218-220). Converters compensate with regex macro expansion and by flattening (tex2tsm.mjs:73-113, :166-167; pbr2tsm.mjs:187-215, :303).

The two integration gaps:
- Math needed an inline box, a display unit, lazy measurement and break classes before any generic ones existed. So it hard-wired LinebreakBlock::math, FlowUnit::Math/special=4, Doc::mathTextMissing with a whole-document re-emit (doc.h:247-255), and three config penalties.
- The font is a compile-time include that the renderer, CSS, shell and packaging all name.

The review also showed that patching these with math-private machinery recreates the problem one layer up: a binding walk after the resolver, a weaker row type for users, or private style rules. So binding sits at instantiation next to styles, built-ins and users share one row type, and math consumes T4/T5/T6 protocols instead of defining parallel ones.

## Abstractions

### MathDict (font-independent symbol vocabulary; one SymbolInfo per symbol)

**owner_layer**

Cross-cutting data feeding L0/L3 (lexing, binding) and L5 (layout).
- Source: engine/data/math/symbols.tsv, plus vendored, version-pinned engine/data/ucd/UnicodeData-<ver>.txt and engine/data/mathml/operator-dictionary-<ver>.tsv.
- Generator: tools/mathdict.py.
- Outputs: engine/gen/math_dict.h and runtime/src/shared/math-vocab.gen.mjs (also consumed by tools/convert).
- API: engine/src/math/dict.h. AtomClass moves to a hand-written engine/src/math/atom.h.

**purpose**

One normative table maps every spelling to one SymbolInfo: name, alias, Typst-codex dotted name, ASCII key, TeX name (converters only) and typed codepoint. The lexer, parser, layout, converters and editor read only SymbolInfo, so a spelling can never change layout. Negation and alphabet maps are generated from pinned Unicode data. Font coverage is not a vocabulary concern.

**definition**

```
// atom.h
enum class AtomClass : u8 { Ord, Op, Bin, Rel, Open, Close, Punct, Inner };
enum SymFlag : u16 { Large=1, LimitsDisplay=2, LimitsAlways=4, StretchyV=8, StretchyH=16, Fence=32 /*symmetric | ‖*/, TextOp=64, Space=128 };
struct SymbolInfo { u32 cp; /*0 = text atom*/ StrRef text; AtomClass cls; u16 flags; u32 negCp; i8 spaceMu; };
using SymbolId = u16;
// dict.h (generated tables; binary search + generated trie)
struct MathDict {
  static const SymbolInfo& info(SymbolId);
  static std::optional<SymbolId> byName(std::string_view);   // names, aliases, dotted names, and std.-qualified names
  static SymbolId byCp(u32 cp);                               // default_for_cp index; unknown cp -> synthesized Ord
  static u32 matchOp(std::string_view, u32 pos, SymbolId*);   // trie over non-letter keys
  static u32 negate(u32 cp);                                   // UCD: precomposed N with canonical decomposition (cp, U+0338), plus override rows (!| -> U+2224, !|| -> U+2226)
  static u32 variant(Alphabet, u32 cp);                        // Mathematical Alphanumerics incl. letterlike holes (ℂℍℕℙℚℝℤ, ℬℰ…, ℭℌℑℜℨ)
};

symbols.tsv columns: name | cp | class | flags | negation | alias_of | tex | default_for_cp | class_source(mathml|override).

Generator rules:
- It asserts unicodedata-free operation: it parses only the vendored files and fails if their version header differs from the pinned one.
- MathML-Core form precedence: the infix form gives the class; prefix/postfix entries give Open/Close/Fence flags; an override row wins.
- Class mapping: largeop -> Op; infix 5mu -> Rel; infix 4mu -> Bin; prefix fence -> Open; postfix fence -> Close; separator -> Punct; else Ord.

Build gates (fail the build):
- duplicate names;
- a cp with more than one row and no default_for_cp;
- a key that mixes ASCII letters and op characters. In S1 an allowlist {o+, o-, o., :', _|_, the 16 !word rows} applies; S4 removes it.
- a class that differs from MathML Core without class_source=override.

Test gates:
- ctest math_dict_lexes_every_key runs the REAL lexer over every key.
- A CI gate lexes every token tools/convert emits to its intended SymbolId.

AA..ZZ are generated alias rows of the bb map.
```

**surface**

- `$arrow.r$`, `$subset.eq$`.
- `$∑_(i=1)^n a_i$` lays out exactly like `$sum_(i=1)^n a_i$`: display limits, and the greedy scope ends at a typed ≤.
- `$NN$`.
- `$a !models b$` gives ⊭ through the UCD map. `$a != b$` still gives ≠ (U+003D + U+0338 -> U+2260).
- `$std.arrow.r$` always reaches the built-in, even when a document shadows `arrow.r`.
- Example data row: `arrow.r  2192  Rel  stretchy-h  219B  -  \\rightarrow  y  mathml`.

**replaces**

- tools/mathc.py:96-252 (DICT inside the font compiler, incl. 27 '!x' rows at :179-187,:202 and AA..ZZ at :247-252)
- tools/mathc.py:324-333 (vocabulary filtered by the font cmap), :349-350 (dictionary cps decide glyph records), :456-466 (class enum/OpEntry in the font header)
- engine/src/math/mathfont.h:46-56 (mathOp over the font header)
- engine/src/math/math.cc:95-104 (isOpChar), :164-213 (_|_ lookahead, !word branch, 4-char munch)
- engine/src/math/math.cc:214-226 (linear strcmp-order first-hit cp->class scan)
- engine/src/math/math.cc:264 (atRel tests token kind, not class)
- tools/convert/tex2tsm.mjs:116-143 (divergent vocabulary copy)

### MathRow registry + MathCall IR (one row type; closed primitives; families as templates)

**owner_layer**

L3 binding and L5 layout dispatch.
- engine/src/math/row.{h,cc}: MathRow, checkRow, built-in primitive table.
- engine/data/math/stdlib.tsv: built-in family templates in the user template language. Generated into engine/gen/math_rows.h, plus runtime/src/shared/math-rows.gen.mjs for T2's stdlib generator.
- engine/src/math/ir.{h,cc}: lexer, parser, expansion, bind-time rewrites; run lazily on first use.
- engine/src/math/layout.cc: one switch over the 14 layout primitives.

**purpose**

Built-ins and documents extend math through the same struct and gates. Built-in families are template rows over a closed primitive set, the same thing a user declaration produces, so 'equal footing' holds by construction. Only the primitive table is privileged, and only because it is C++. Arity and slots are modelled, so errors become local Error nodes. One IR feeds layout, copy text, MathML/a11y (T7) and the `--stage=mathir` dump.

**definition**

```
enum class SlotKind : u8 { Content, Sym /*exactly one symbol token; auto-lr and |-pairing suspended*/, Ident /*bare word/number: enums, sizes*/, Rows };
struct SlotSpec { StrRef name; SlotKind kind; bool optional; const MNode* dflt;
                  ShedRule shed; StyleXf st; bool cells;   // primitive-only attributes: shed (…) in sugar operands; style transform; Rows: ',' splits cells
};
enum class Prim : u8 { None,
  // layout primitives (L5)
  Frac, Stack, Root, Attach /*t b tl bl tr br; limits mode read from base*/, Lr /*open body close [size]*/, Mid, Delim /*one sized delimiter*/,
  Accent /*top|bottom; horizontal chain when the base is wider*/, Rule /*over|under*/, HStretch /*brace/bracket/arrow + optional annotation*/, Grid, MStyle, Space, Phantom /*full|h|v|smash*/,
  // bind-time rewrites (L3; never reach layout)
  Variant /*alphabet map per leaf, coverage checked once*/, Class /*force atom class*/, LimitsMode /*limits|scripts|auto on the base atom, base class kept*/, TextAtom /*op()/upright(): text leaf with class + limits*/ };
struct MathRow {                     // ONE type: built-in symbols, built-in primitives, stdlib templates, user decls
  StrRef name; enum K : u8 { Symbol, Function } k;
  SymbolInfo sym;                    // Symbol rows
  std::span<const SlotSpec> params;  // Function rows (positional then named)
  std::optional<AtomClass> cls;      // explicit result class (lowered to Class rewrite); else edges of the expansion
  SymbolId bare = kNoSym;            // meaning without '(' (dot -> dot.op ⋅ Bin)
  Prim prim = Prim::None;            // set ONLY by the C++ primitive table
  const MNode* body = nullptr;       // template with Param leaves, parsed once against env@epoch
  Span declSpan; u32 epoch;          // 0 = built-in
};
bool checkRow(const MathRow&, RowOrigin, DiagSink&);   // ONE validator: generator (build failure) and MathEnv (diagnostic)

stdlib.tsv rows, same syntax as $.math.fn (bound cps as today):
  abs(x)=lr(|, #x, |)
  norm(x)=lr(‖, #x, ‖)
  binom(n, k)=lr(paren.l, stack(#n, #k), paren.r)
  hat(x)=accent(#x, U+02C6) [bare hat.op]
  dot(x)=accent(#x, U+0307) [bare dot.op]
  bar(x)=rule(#x, over)
  bb(x)=variant(#x, alphabet: bb)
  display(x)=mstyle(#x, style: D)
  limits(x)=limitsmode(#x, mode: always)
  pmat(r: rows)=lr(paren.l, grid(#r), paren.r)
  cases(r: rows)=lr(brace.l, grid(#r, align: (l, l)), .)
  overbrace(x, t?)=attach(hstretch(#x, U+23DE, over), t: #t)
  big(d: sym)=delim(#d, size: 1.2)

struct MNode { enum K : u8 { Sym, Num, Text, Hole, Param, Call, Run, Error } k;
  u16 frag; u32 lo, hi;              // fragment index + byte range -> source span via the fragment's SPAN
  SymbolId sym; StrRef str; TextRole role; u16 row; std::span<MArg> args; std::span<std::span<std::span<MArg>>> rows;
  std::span<MNode*> kids; AtomClass first, last; u16 scopeEnd /*v2 §13 annotation*/; u16 hole; u8 limits; };
const MathIR& irOf(const ContentNode* math, const MathEnv&);   // lazy: parse + expand + rewrite against env@node->declEpoch; memoized on the node

Parser rules (grammar only):
- `a/b` -> frac; `x^a_b`, `x'` -> attach. Primes merge into t, so `f'^2` = attach(f, t: ′2).
- Single-token operands (scripts, both fraction operands): a letter word that resolves to a row is that row. Otherwise it is a Run of single-letter variables, never dictionary-dependent: x^ab = x^{ab}, a/bc = a/(bc). Only `(…)` is shed.
- `name(` with NO whitespace before '(' binds a Call iff the name resolves to a Function row (env@epoch, then built-ins). `name (` and non-function names stay name + group (`Id(A,B)`, `sin(x)`). A bare function name uses `bare`, else the implicit-name rule.
- Sym slots read exactly one symbol token, so `lr(angle.l, x, angle.r)`, `lr(|, #x, |)` and `mid(|)` parse. Named args are recognized only for declared slot names (`f(x: A)` keeps ':' as Rel).
- Rows slots: named args come before the first cell; `;` separates rows; ',' separates cells iff cells=true (mat); `&` marks alignment points (cases, aligned).
- Open-class ... Close-class -> auto lr; mixed pairs (intervals) allowed.
- Fence-flag symbols (| ‖): open at operand position; at operator position close a pending open in the same group, else open if a later same-symbol token in the group can close it, else lower to mid() (Rel spacing, stretched to the group). So `p(Y | X)`, `{x | x > 0}` and `(x,x) | x ∈ A` get \mid semantics without diagnostics, and `|a| |b|` pairs. An unclosed open at group end becomes Ord + info `math-fence-unpaired`.
- `!` directly followed by a symbol that is Rel-class OR has negate(cp)!=0 is negation:
  - the precomposed cp (or the row's declared negation), inheriting class and flags;
  - if none exists or the chain does not cover it: an Error leaf showing the source (`!defeq`) + `math-negation-missing`;
  - never the bare base glyph, never factorial.
  Otherwise `!` is postfix factorial. `n!<m` stays ≮, `n! < m` is factorial.

Errors and limits:
- Errors resync at `,`, `)`, `;`, a Rel token or the end, producing Error[lo,hi): the source slice as a measured text leaf with a sub-span diagnostic. At most 8 diagnostics per formula, the rest summarized.
- Missing args -> Error leaves. Extra args -> one Error leaf + `math-arity`.
- Parse depth <= 256 (fuzz target).
```

**surface**

`$frac(a, b)$` `$attach(x, t: a, b: i, tl: 2)$` `$lr(angle.l, x, angle.r)$` `$lr(|, x, |)$` `$abs(x)$` `$accent(x, ˇ)$` `$!models$` `$not A$` (¬, unchanged) `$bb(R) times cal(C)$` `$display(sum_i x_i)$` `$limits(=)^"def"$` (keeps Rel) `$p dot q$` (⋅) and `$dot(x)$` (accent) `$class(rel, ->>)$` `$big(\\|)$` `$phantom(x)$` `$overbrace(a + b, n)$` `$hat("p"' - "p")$` (wide hat) `$mat(1, 2; 3, 4)$` `$cases(x & "if" x >= 0; -x & "otherwise")$` `$ aligned(f(x) &= (x+1)^2; &= x^2 + 2x + 1) $`. `tsrc --stage=mathir` prints the IR plus each formula's sub-span diagnostics (goldened).

**replaces**

- engine/src/math/math.cc:66-78 (8 ad-hoc MNode kinds)
- engine/src/math/math.cc:231-235 (isCallName) and :437-439 (ACCENT flag routes to parseCall)
- engine/src/math/math.cc:451-457 (dictionary-dependent rewind in single-token context) and :473-500 (parseCall: no arity; '(' mandatory)
- engine/src/math/math.cc:504-524 (parseBigOp) and :988-1025 (layoutBigOp, dead textOp branch :990-993)
- engine/src/math/math.cc:1178-1209 (layoutCall if-chain; `static MNode empty` :1180) and :1102-1147 (layoutBinom re-implements fencing)
- engine/src/math/math.cc:301 (shed any bracket), :321-323 (prime then ^)
- engine/src/math/math.cc:247-249, :1253-1256, :1271-1274 (first error only; whole formula degraded)
- engine/src/math/math.cc:802-809 (text-operator limits special case)

### MathEnv: the 'math' namespace of the generic DeclEnv (epoch-stamped, persistent)

**owner_layer**

L3 (instantiation).
- engine/src/math/env.{h,cc} implements T2's DeclNamespace.
- T2 owns the generic decl kind and its instantiate-time application in schedule order.
- T4 owns scoping frames.
- JS surface: `$.math.*` in runtime/src/worker/executor.mjs (generated through T2's stdlib generator).

**purpose**

Documents declare symbols, operators and functions as MathRows through the same checkRow gate as built-ins. Declarations are schedule-ordered EMITs, applied where STYLE_PUSH is applied (model.cc:77-100). Instantiation stamps every copied node with the declaration epoch. A formula therefore resolves names against the env as of its emission position, even after the resolver lifts it (notes, resolve.cc:362-393) or T3 clones it into a TOC. There is no second semantic walk, and no JS runs after execution.

**definition**

```
// generic (T2): decl{ns:'math', name, payload…} EMITted by $.math.* at the schedule position; instantiate applies it and does not add it to the tree
struct ContentNode { /* … */ u32 declEpoch; };   // stamped by inst.copy at EMIT, carried by clones/moves
class MathEnv final : public DeclNamespace {
 public:
  void apply(const DeclPayload&, u32 epoch, DiagSink&) override;   // payload -> MathRow -> checkRow(); on failure: diag at decl span, decl ignored
  const MathRow* find(std::string_view, u32 epoch) const;          // latest row with row.epoch <= epoch; std.-qualified names skip the env
  u32 matchOp(std::string_view, u32 pos, u32 epoch, const MathRow**) const;
  std::optional<SymbolInfo> cpInfo(u32 cp, u32 epoch) const;       // claimCp overlay for typed input
  u64 stamp(u32 epoch) const;                                      // hash(decl prefix) ^ hash(math.* settings): T9 invalidation key
};

Gates (all diagnostics; the decl is ignored):
- A name is a word, a dotted word, or a non-letter key. Keys may not contain digits or structural characters (^ _ / ' ( ) [ ] { } " # $ \\ & ; ,). Letter/op mixes are rejected.
- A key that becomes a longer munch over an existing key -> info `math-key-munch`.
- Names starting with `std.` are reserved.
- Shadowing a built-in -> info `math-shadow`. `std.<name>` still reaches the built-in.
- cp <= 0x10FFFF and covered by the MathFont chain, else `math-coverage`.
- arity <= 9; the body must be a string or a math value.

Expansion: Call(templateRow, args) -> clone(body), with Param(name) := the argument's subtree.
- The argument is parse-isolated and never re-lexed.
- It is SPLICED into the surrounding Run (TeX macro semantics); class(ord, …) makes it atomic.
- Budgets are counted while expanding, as layout-visible nodes: depth <= 32, <= 4096 nodes per formula, <= 2^20 per document. Overflow -> Error + `math-macro-limit`.
```

**surface**

#{
  $.math.symbol('defeq', { char: '≝', class: 'rel', claimCp: true })
  $.math.op('colim', { limits: 'display' })
  $.math.op('Hom')
  $.math.fn('eqv', ['a', 'b'], '#a simeq #b')
  $.math.fn('idtype', ['A', 'a', 'b'], 'Id_(#A)(#a, #b)')
  $.math.fn('pmatrix', [{ name: 'r', kind: 'rows' }], 'lr(paren.l, grid(#r), paren.r)')
  $.math.fn('grad', ['f'], 'nabla #f', { bare: 'nabla' })
}
`$eqv(A, idtype(U, a, b))$`. A function body is data: a string, or a math value whose literal parts reference parameters as `#name`. Passing a JS function is a TypeError. A `#use`d module's `math = { symbols, ops, fns }` export is registered by a `__registerModule(mod)` call that codegen places at the #use site; this depends on T1/T2 implementing #use.

**replaces**

- engine/src/math/math.cc:458-468 (the only 'extension': unknown words become upright Op names)
- tools/convert/tex2tsm.mjs:73-113 (regex macro pre-expansion; fails on nesting, e.g. hott-introduction.tsm:43 `$Id(v,a)rA$`)
- the absence of any math registration next to $.fence/$.region (executor.mjs:268-269)

### MathValue: one `math` node with `mathsrc` fragments and typed holes

**owner_layer**

L1/L2 and the ops contract:
- codegen lowering of `$…$` (engine/src/codegen);
- ops.def KIND(math), KIND(mathsrc);
- executor constructors plus T2-generated `math.*` wrappers;
- hole conversion in engine/src/math/ir.cc.
The island scanner row is T1-owned.

**purpose**

Math becomes ordinary content in ops with the same constructor model as everything else (v2 §4). Fragments and holes differ on the wire, so a value can never re-enter the lexer. Each fragment carries its own SPAN, which gives sub-span diagnostics even for multi-line islands inside containers. Holes are evaluated as contained thunks. A multi-row display is a group of row equations, so labels, numbers and page breaks per row stay with T3 and T6.

**definition**

```
ops.def (S8, coordinated with T2's schema bump; OPS_VERSION 6->7):
  KIND(math, 28) replaces KIND(mathblock,14) and KIND(mathinline,22).
    attrs: display: bool?; label: str? (universal); numbered: auto|always|never? (T3 universal).
    kids: mathsrc | hole. Any non-mathsrc kid is a hole.
  KIND(mathsrc, 29): attr src: str (raw, escapes undecoded; the math lexer is the single decode point); own SPAN; valid only as a math kid.
  The reader rejects a mathsrc outside math, and block kinds as holes, with an error node + diagnostic.
The lexer sees the fragments joined with '\n' wherever they are adjacent, with a Hole token at each hole kid. Token offsets map back through each fragment's SPAN. JS-built fragments have no span, so they use the formula span.

Hole conversion (at irOf; content leaves are materialized at prepare):
  math                         -> parse-isolated subtree, spliced into the parent Run
  text                         -> Text(Quoted) leaf (JS strings)
  styled|link with sole kid math -> math subtree; the T4 paint-only properties (color, link) are applied to its leaves; a metric property gives `math-style-ignored`
  other inline content (ref, note marker, code, link text…) -> ContentLeaf from T5 shapeInlineBox (anchorId kept), class Ord
  error (from __hole)          -> Error leaf showing the hole source
  block kinds / image          -> Error leaf + `math-hole-kind`

codegen (math hole sub-grammar, from T1):
  holes are `#ident` with ident = [A-Za-z][A-Za-z0-9]*, with no `.`/`(` continuation, no `;` terminator and no [...] continuation; or `#(expr)`. Everything else uses #(…).
  $x^#n + #(f(y))$ -> __at(math(__at(mathsrc("x^"),s0,e0), await __hole(async () => n, s1, e1), __at(mathsrc(" + "),…), await __hole(async () => f(y), …)), s, e)
  __hole catches a throw and yields error(msg) with the hole span, mirroring __fence (executor.mjs:156-160).
  A stray `#` becomes an info diagnostic plus a literal, written `\\#` into mathsrc.

JS (executor):
  math(src, opts?)  and the tag  math`x^${n}`: literal parts -> mathsrc; ${} -> holes.
    finite number -> math(String(n)), with exponent forms written `m times 10^e`; non-finite -> error
    string -> text
    content -> as is
  math.frac(a, b), math.attach(x, {t, b}), math.lr(o, body, c), math.mat(rows): T2-generated from math-rows.gen.mjs. They emit std-qualified calls such as mathsrc('std.frac('), so shadowing cannot reach them. Strings in Sym slots are coerced through math.sym.
  math.sym('⟨' | 'alpha'), math.call('eqv', a, b) for user rows (bound by name at the emission epoch), math.equations([...rows]).
  mathinline(src) and mathblock(src, label) remain deprecated aliases.

Display: math{display:true} is block-level. T2's level normalization splits a paragraph around a mid-paragraph display (Typst semantics; the corpus has 0 such formulas).
Multi-row display: group{role:'equations'} whose kids are math{display:true} rows, each with its own label/numbered. Top-level `&` in each row marks alignment points shared across rows (T6). The inner aligned/cases/mat stay Grid inside one formula (TeX align vs aligned).
copyText = display ? '$ ' + parts + ' $' : '$' + parts + '$'. A math hole contributes its inner source with no delimiters; a text hole contributes "…"; a content hole contributes its copy text.
The fragment producer (engine/src/inline/fragment.cc:59-60) creates math(mathsrc) with no holes, and `#` stays literal + info.
```

**surface**

#let n = 3
`$x^#n$` -> x³; `$e^(#(2*k) pi i)$`; `$2^#k (n+1)$` (juxtaposition); `$#a_i$` = hole a with subscript i. `$a + #style({color: 'crimson'}, math`b^2`) = #link("#def-c", math`c`)$`. In a fence handler: `return math.mat(rows)`. A literal hash is `\\#` (cardinality `$\\#A$`).
Multi-row display with per-row labels:
$ f(x) &= (x+1)^2 $ <eq-a>
$      &= x^2 + 2x + 1 $ <eq-b>

**replaces**

- engine/src/inline/inline.cc:266-310 (math body as one verbatim string carved before holes; label parsed only after the island)
- engine/src/codegen/codegen.cc:95-117 (mathblock only for whole-paragraph display; label forwarded only there)
- runtime/src/worker/executor.mjs:218-220 (string-only constructors)
- engine/src/ops/ops.def:28,36 (two kinds for one concept; ArgK::src payload)
- engine/src/emit/emit.cc:108-121 and :752-759 (emit reads ArgK::src and parses there)
- engine/src/inline/fragment.cc:59-60 (fourth producer creating mathinline{src})

### MathFont runtime object + process-wide registry with a coverage fallback chain

**owner_layer**

L5/L7 cross-cutting resource:
- engine/src/math/font.{h,cc}: blob reader, registry, coverage chain; no browser dependency.
- tools/mathc.py: one run produces .tsmf + .woff2 + a shared content hash.
- The embedded default (Euler) is the existing constexpr data wrapped as a MathFont.
- Extra fonts arrive through T9's resource protocol.
- On the paint side, the font is an ordinary declared-webfont entry with role 'math' (T4 font roles).

**purpose**

Turns the 'font-agnostic MATH artifact' (math-design §1) into a runtime object. Algorithms read MathFont. One font manifest serves paint, the worker, static export and packaging. Uncovered codepoints follow a defined chain that ends in a measured text leaf, never a magic box painted by a CSS fallback.

**definition**

```
struct MathFont {
  u16 id, upem; i16 hheaAsc, hheaDesc, axisHeight, xHeight; u16 minConnectorOverlap;
  std::array<i16, kMathConstCount> c;
  std::span<const GlyphRec> glyphs; std::span<const VarChain> vert, horiz; std::span<const u32> variantCps; std::span<const AsmPart> parts;
  std::string_view family; u64 contentHash;
  const GlyphRec* glyph(u32) const; const VarChain* chain(u32, bool vertical) const;
};
class MathFontRegistry {   // PROCESS-WIDE (one per WASM instance), keyed by contentHash; Docs reference fonts by hash
  static MathFontRegistry& get();               // Euler embedded at id 0, zero-copy
  const MathFont* load(std::span<const u8> tsmf, DiagSink&);   // validated like ops: magic 'TSMF', version, bounds, sortedness; idempotent per hash
  LeafSource resolve(u32 cp, const MathPolicy&) const;          // first font in math.fonts covering cp, else TextFont
};
struct LeafSource { u16 fontId; /*kTextFont = 0xFFFF*/ };   // replaces MathBox::textFont

The PRIMARY font (math.fonts[0]) supplies every MATH constant: axis, script shifts, rule thickness. A secondary font supplies only per-glyph records and chains, scaled by its own upem. A load-time warning `math-font-mismatch` fires when a secondary font's axis or x-height differs from the primary by more than 5%.

MathPolicy is the settings namespace math.* (T4 registry). Every value is converted to su once, with a fixed rounding mode:
  delimiterShortfall = {num:1, den:10}   // applied as target - target*num/den (integer), so today's `target -= target/10` is reproduced exactly
  delimiterShortfallEm = 0; fracPadEm = 0 (S2) -> 0.1 (S5); maxAssemblyRepeats = 64
  break.after {rel: .8, bin: .95}, break.before {rel: .85}   // TeX math parameters: math-owned data, honoured by T5 as explicit producer penalties
  nameClass = Op; implicitNames = allow|info; fonts = ["euler"]; text.weight = normal|inherit

Non-embedded primary fonts: the mathFont resource is a PRE-EMIT gate, like tokensPending/imagesPending (doc.h:244). This amends v2 §9: math is exact from t=0 only with the embedded primary.

mathc.py gates:
- cmap reachability (existing, mathc.py:311-322);
- the glyph set is RANGES (+0x02B0-0x02FF) ∪ chain refs, independent of the vocabulary;
- a CI check that the kGlyphs cp set is a superset of the previous set;
- woff2 cps ⊇ blob cps, and U+0020 present;
- typo == hhea when USE_TYPO_METRICS;
- GPOS kern pairs reported.
```

**surface**

Settings: `{"math": {"fonts": ["libertinus", "euler"], "break": {"after": {"rel": 0.8}}, "implicitNames": "info"}}`. Font declaration (one list): `fonts: [{family: 'Libertinus Math', src: '…woff2', role: 'math', metrics: '…tsmf'}]`. Build: `python3 tools/mathc.py --font Libertinus-Math.otf --out fonts/libertinus.tsmf --woff2 fonts/libertinus.woff2`.

**replaces**

- engine/src/math/mathfont.h:5, :10-56 (generated header include; constexpr globals as THE font)
- engine/src/math/math.h:30 (bool textFont)
- engine/src/render/typeset_html.cc:5, :103-108 (renderer includes the font header)
- runtime/src/main/shell.mjs:58-64 (family literal and 'STIX Two Math' fallback), :89-90 and :200-206 (hard-coded woff2 URL and a second @font-face path beside ensureFontFaces :92-110)
- tools/export-static.mjs:87, tools/pack-dist.mjs:26 (copy the font by path)
- engine/src/math/math.cc:546-554, :568-572 (600/700 stand-ins; decimal 'U+'; once per formula)
- engine/src/math/math.cc:933, :1132 (duplicated shortfall literal), :658 (r <= 64)

### Lazy MathLayout: segment plan at emit, boxes at finalize, generic items out

**owner_layer**

L5 shaping: engine/src/math/layout.{h,cc}.
- emit calls prepareMath(irOf(node)), which produces T5 items.
- finalize runs through T5's generic pending-object hook (registered per producer id), called by resolveWidths without naming math, and later by T9's ready hook.
- Display bodies go to T6.
- The painter is registered with T7.

**purpose**

One conversion from math list to items: classes and demotion computed once from the IR, class-indexed break tables, generic glue. Text and content leaves are ordinary measurement needs, so there is no whole-document re-emit. Leaf styling is a projection of the emission-time style through T4's property registry, never a math-private rule.

**definition**

```
struct MathCtx { const MathFontRegistry* fonts; const MathPolicy* pol; StyleTable* styles;
                 StyleId ctxStyle;      // style at the EMIT position (v2 §12)
                 double basePx;         // the glyph em; text-leaf size derives from it, so both follow T4's sizePx fix together
                 DiagSink* diags; };
struct SegmentPlan { u16 lo, hi; Su glueBefore; float penaltyBefore; AtomClass first, last; };
struct PreparedMath : PendingObjectGroup {   // T5 generic
  const MathIR* ir; bool display; std::vector<SegmentPlan> segs; std::vector<MeasureItem> needs; std::vector<InlineObject*> objs;
  bool finalize(const MetricStore&) override;   // all needs present -> box layout ONCE; coverage diagnostics once per distinct cp (hex)
};

Leaf styling (T4 registry classification):
- measureStyle(text leaf) = Styling{ sizeMul = basePx*scale/docBasePx } + family from the T4 'math-text' role (default body font). This is exactly today's synthesized Styling (math.cc:582-584), so StyleIds and requests are unchanged.
- Weight and italic are stripped; `math.text.weight: inherit` opts into bold names in bold contexts.
- paintStyle(any leaf) = measureStyle + the paint-only properties of ctxStyle (color, link). So headings neither bolden nor resize names.
- A content leaf is an opaque box from T5 shapeInlineBox(content, ctxStyle), with w/asc/desc, display-list runs and anchors. Math only positions it.

Segmentation:
- Rules 5–6 demotion runs ONCE over the top-level atoms of the spliced IR.
- penalty(i|i+1) = min(after[last_i], before[first_{i+1}]).
- glue = pairGlue(last_i, first_{i+1}).
- A segment is a maximal run with no finite penalty inside it.

Items:
- Box(InlineObject{w, asc, desc, lbStart = lb(first), lbEnd = lb(last), painter = kMath, payload, copyText on the first segment, pending}), where lb maps Open->OP, Close/Punct->CL, else AL.
- Between segments: Glue{w, discardable, synthetic} + Penalty. The segments form one object group in which these explicit penalties are authoritative; T5's pair table rules only at the group's outer edges.
- Until T5's Glue.synthetic lands, the glue keeps BF_BOUND, so copy join, kerning context, run splitting and data-syn stay as today.

A display formula is one object, the body of T6's DisplayBox. A pure-glyph formula finalizes inside prepareMath (exact from t=0).
```

**surface**

No markup surface. Settings `math.break.*`, `math.text.weight`. Dumps: `--stage=mathbox` keeps its format, and `--stage=mathir` is added.

**replaces**

- engine/src/math/math.h:40-46 (MathTextCtx threading MetricStore/StyleTable/Interner) and :55-70 (MathSeg with magic brkBefore)
- engine/src/api/doc.h:53-56, :247-255, :367-378 (mathTextMissing, whole-document re-emit, hand-merged requests)
- engine/src/math/math.cc:579-599 (textFontBox style synthesis, now a named T4 projection)
- engine/src/math/math.cc:1214-1243 (effClsOf preview) and :1281-1335 (duplicated Rules 5–6)
- engine/src/emit/emit.cc:122-146 (brkBefore -> three config keys; unconditional breakPenalty=0 :145)
- engine/src/api/config.h:47-49 (three math penalty keys unreachable from wasm)

## Subsumption (finding → mechanism)

- **subsumed** by *MathRow registry + MathCall IR*: `math/call-construct-string-dispatch`, `math/missed:1`
  - Built-in families are template rows over primitives; layout dispatches one switch over 14 primitives.
  - `name(` binds a Call only when there is no space before '(' and the name is a Function row. The corpus has 0 `name (` uses for call or accent names.
  - A bare function name uses `bare`: `p dot q` gives ⋅ Bin, so the shipped HoTT formula renders. Without `bare` it falls to the implicit-name rule plus an info diagnostic, never a whole-formula error.
  - `not` stays the ¬ symbol (mathc.py:212; 8 corpus `f_(not Delta)`). Negation is reached only through `!`.
  - Generation gate: a Function row whose name is also a symbol must set `bare` to that symbol.
  - This removes bar's dead cp and binom's duplicated fencing.
- **subsumed** by *MathDict*: `math/vocabulary-in-font-artifact`, `math/lexer-hardcoded-alphabet`, `math/missed:2`, `math/missed:3`
  - The font compiler no longer reads the vocabulary. Its glyph set is RANGES (+0x02B0-0x02FF, which keeps the six accents that only DICT pulled in) plus chain refs, with a superset CI gate.
  - symbols.tsv is seeded from the post-filter header (322 rows), not from DICT. varrho, varsigma and ::= (dropped by the cmap filter today) arrive in S6, once the coverage chain renders them as measured text leaves.
  - Keys that mix letters and ops are allowlisted in S1 and dropped in S4 (`o+ o- o. :'`; 0 corpus uses).
  - `!` becomes a parser rule. Classes come from pinned MathML Core data with an override column.
  - Converter maps are generated, and a CI gate lexes every emitted token.
- **subsumed** by *MathDict (UCD negation/variant maps; Open/Close/Fence/Stretchy flags) + bind-time Variant rewrite + Lr/Mid/Delim primitives + the fence/mid parser rule*: `math/negation-enumerated`, `math/alphabet-variants`, `math/fence-pairs-ascii-only`
  - `!` before a Rel-class symbol, or any symbol with negate!=0, is negation. A user Rel with no precomposed form gives an Error leaf with the source and a diagnostic: never the bare base glyph (meaning inversion), never factorial. `!=` (22 corpus uses) keeps giving U+2260.
  - Variants are a bind-time rewrite with coverage checked per leaf. An uncovered letter becomes an Error leaf showing the source plus `math-coverage`, not a silent base letter.
  - Unpaired | and ‖ at operator position lower to mid() with Rel spacing and no diagnostic. That fixes set-builder notation and the 54 conditional-probability bars in pbr.
  - Stretch is applied iff StretchyV is set, which gives kFlagStretchy a reader.
- **subsumed** by *MathDict Space rows + MStyle/Space/Phantom primitives + LimitsMode/Class rewrites*: `math/missed:4`
  - Space rows: quad, wide, thin, med, thick, negthin.
  - MStyle templates: display, inline, script, sscript, cramped.
  - limits(x)/scripts(x) set a mode on the base atom and KEEP its class, so `limits(=)^"def"` stays Rel.
  - class(cls, x) and phantom/hphantom/vphantom/smash are new primitives.
- **subsumed** by *Name resolution (env@epoch -> std rows/MathDict -> MathPolicy implicit-name rule) + the single-token operand rule*: `math/implicit-names-op-class`
  - The default class stays Op: the documented §14 trade-off, and the demotion is TeX-faithful.
  - implicitNames=info adds `math-implicit-name` (catching `rA`, `Sn`), and also flags a script word that resolves to a Rel/Bin name (`x_in`).
  - In single-token operands (scripts AND fraction operands), a non-row letter word is a Run of letters: x^ab = x^{ab}, a/bc = a/(bc), ab/c = italic ab over c. The rule never depends on the dictionary. The corpus has 0 such words in any slot, and the change is listed in the deltas.
  - Limits are an atom property read by the single attach primitive.
- **subsumed** by *Lazy MathLayout + Attach primitive + LimitsMode*: `math/bigop-greedy-body`, `math/segmentation-class-preview`
  - Big operators are ordinary Op atoms with a limits mode.
  - The v2 §13 scope is a parse annotation only (scopeEnd), ended by any Rel-class SymbolInfo.
  - Demotion runs once over the spliced top-level atoms, so `(a+b)^2 - c`, scripted relations, macro expansions and math holes all yield break points.
  - No speedup is claimed.
- **subsumed** by *MathCall IR (Error nodes with resync; slot-generated Error leaves)*: `math/missed:5`
  An Error leaf is the source slice set as a measured text leaf with a sub-span diagnostic, and the rest of the formula lays out normally. No unverified Euler stand-ins are painted, and `root(3)` no longer paints NUL.
- **subsumed** by *MathValue (mathsrc + typed holes) + MathEnv (MathRow decls) + Grid/HStretch/Delim primitives + equations groups*: `math/math-opaque-string`, `math/closed-vocabulary`, `real-world-evidence/math-extensibility`
  - `#` holes in math amend v2 §5 (design-decisions-v2.md:158): the math island is verbatim except for JS-lexed holes. The delta entry is shared with T1, and the corpus has 0 `#` in math.
  - Fragments and holes differ on the wire.
  - There is one template definition form (string or math value with `#param`); the traced JS form is removed.
  - `mat`, `cases`, `aligned` (Grid), `overbrace` and wide accents (HStretch/Accent), `big` (Delim) and pre-scripts cover what the converters flatten or delete (tex2tsm.mjs:133,135,166-167; pbr2tsm.mjs:141,187-215,303). Per-row-numbered align becomes an equations group.
  - `cancel` is the one accepted gap: the converters emit a diagnostic instead of deleting it.
- **subsumed** by *MathValue (math{display,label,numbered}; display is block-level) + T1 universal labels + T2 level normalization*: `math/math-island-oneoff-syntax`
  - The sugar expresses only what the constructor can.
  - A mid-paragraph `$ … $` is a display block that splits the paragraph (Typst; 0 corpus cases), decided rather than left open.
  - Labels always attach, because math carries `label`. T1's `label-dropped` remains only for other orphan forms.
  - A paragraph consisting only of display lines is an equations group (T1 line rule).
- **subsumed** by *MathFont + process-wide registry + MathPolicy*: `math/compiled-in-font`, `math/missing-glyph-fallback`
  - Euler stays embedded, so math is exact from t=0 by default.
  - Host fonts are content-hashed, pre-emit-gated resources declared as role 'math' font entries.
  - The primary font supplies constants.
  - An uncovered cp walks the chain and then becomes a measured text leaf. The CSS fallback stack is removed.
  - Policy literals become integer-exact settings.
- **subsumed** by *Lazy MathLayout leaf projection (T4 registry: measureStyle vs paintStyle) + T5 shapeInlineBox + T7 openRun*: `math/math-leaves-bypass-style`
  - Colour and link reach every leaf.
  - Measurement keeps today's style tuple, so there is no heading-bold leak and no sizePx split.
  - Lang and family for text leaves come from T4's 'math-text' role, not from ad-hoc ctxStyle bits.
  - The per-word ink box is rejected (v2 §6). An optional per-style reference ink stays an S10 setting.
- **subsumed** by *Lazy MathLayout (prepare at emit; finalize through T5's generic pending-object hook)*: `math/math-text-pull-channel`, `api-measure-code/math-text-measure-side-channel`, `emitter/full-reemit-for-math-text`
  - Formula text leaves are ordinary MeasureItems in the same request, with no re-emit.
  - The finalize hook is generic: resolveWidths never names math, and images and tokens can use it too.
  - When T9's ResourceTable lands, the needs become need(textWidth)/need(fontVmet) behind the same hook, which is not a second migration.
  - T9 owns diagnostic dedup for other re-emits.
- **owned-by-other-theme** by *T5-text-shaping*: `math/inline-math-special-block`, `emitter/math-only-inline-box`
  This assumes T5 provides:
  - InlineObject{w, asc, desc, lbStart, lbEnd, painter, payload, copyText, pending} on Item::Box;
  - object groups with authoritative producer penalties;
  - a generic pending finalize hook;
  - Glue.synthetic, which subsumes BF_BOUND at all five sites;
  - the shapeInlineBox service.
  Math provides the producer (edge lb classes from atom classes) and the painter. LinebreakBlock::math stays as an adapter until T5 P3 lands.
- **owned-by-other-theme** by *T6-layout-pagination*: `math/display-math-unit`
  This assumes T6 provides:
  - a DisplayBox (atomic body, measured tag content, shared baseline, collision policy, skips from settings, display-overflow warning);
  - an equations layouter for group{role:'equations'}: rows with shared alignment columns from top-level `&` offsets, a tag per row, page breaks between rows.
  Math supplies one body object per row, with axis height and alignment x-offsets.
- **owned-by-other-theme** by *T3-semantics*: `math/equation-numbering`
  - T3's resolver wraps a numbered math{display:true} as equation{label}(body: math, tag: content), like figure. The math node never holds a tag child, so its kids stay pure fragments and holes.
  - `numbered` is T3's universal attribute; never = \notag.
  - Each row of an equations group is its own equation, so per-row labels and numbers fall out with no extra mechanism.
  - ArgK::name is no longer used for the tag.
- **owned-by-other-theme** by *T1-surface-frontend*: `math/math-span-lexer-triplication`, `math/missed:0`, `math/island-scan-escapes-block`
  This assumes T1's IslandRule table is applied inside the linepass loop. Math contributes the `$` row:
  - escapes are 'raw' (T1 policy; decoded only by the math lexer);
  - crossLines=WithinBlock;
  - one mathsrc fragment per source line, container prefixes stripped, each with its SPAN;
  - the math hole sub-grammar, with `#(…)` lexed by the JS lexer BEFORE the closing `$` is sought;
  - island-aware table-cell splitting;
  - the same rules mirrored in the tree-sitter and TextMate grammars.
- **bug-fix-only** by *MathCall IR + MathRow slot specs + the mathir stage*: `math/prime-then-script-degrades`, `math/bracket-shedding-any-group`, `math/call-arity-silent`, `math/diag-quality`, `real-world-evidence/math-leniency-silent`
  - Primes merge into t.
  - Only `(…)` is shed (0 corpus bracket arguments).
  - Arity comes from slots.
  - Diagnostics carry sub-spans through fragment SPANs and hex cps, are capped per formula, and are reported once. The memoized irOf removes per-emit repeats.
  - `--stage=mathir` goldens the per-formula diagnostics, closing the 'no post-ops diags stage' gap for math.
  - Mismatched pairs and a dangling `/` keep their leniency but emit info diagnostics.
- **bug-fix-only** by *Attach primitive + MathPolicy.fracPadEm*: `math/spacing-edge-classes`
  - attach propagates base firstCls/lastCls (math.cc:853,877).
  - fracPadEm moves to 0.1em in its own S5 commit.
  - assemble propagates a lone child's topAccent, which deletes math.cc:1080-1084.
  - Runs expose first/last edges to splices.
- **bug-fix-only** by *MathFont gates + MathDict split + doc updates*: `math/exactness-gaps-paint`, `math/dead-data-and-params`, `math/doc-drift`
  - Paint: font-kerning:none on .tsr-mg, no fallback family, a woff2 from the gated tool with U+0020, and the math-design §11 audits.
  - ssty: neither metrics nor paint apply it, because CSS never enables ssty, so the two stay consistent. Using ssty alternates requires PUA injection (see not_generalized).
  - Docs:
    - math-design §8's run-coalescing promise is retracted; T7's display list may coalesce runs whose positions equal summed advances.
    - Corrected: §3, §5, §7, §13, §14, architecture §1/§5 and document-model §11 fonts.math.
- **owned-by-other-theme** by *T7-render-runtime*: `math/fallback-and-a11y`
  - T7 owns role=math with aria-label from copyText, AnchorNamer for anchors inside math leaves, and the static serializer (finalized MathBoxes, or MathML from the IR).
  - Math provides copyText, the IR with scope annotations, and finalized boxes.
- **owned-by-other-theme** by *T3-semantics*: `math/toc-excerpt-drops-math`
  - T3's structured excerpts clone the math node. The clone carries declEpoch and the IR memo, so it does not rebind.
  - Until then, S8 makes excerptInto skip math, as today: no raw source leaks into TOC text.
  - copyText is available for plain-text fallbacks.

## User extension examples

### Declare a new relation ('defined as' ≝) with correct spacing, breaking and negation

**Today**

`$f defeq g$` sets an upright Op name (math.cc:458-468). A typed ≝ gets Ord because no row names U+225D (math.cc:214-226). `$f !defeq g$` would lay out as (f!) ≝ g.

**After**

- `#{ $.math.symbol('defeq', {char: '≝', class: 'rel', claimCp: true}) }` EMITs a decl at that schedule position.
- Every later formula (by declEpoch) resolves both `defeq` and a typed ≝ to SymbolInfo{0x225D, Rel}, with thick spacing, Rule 6 demotion and Rel penalties.
- `!defeq` has no precomposed form, so it renders an Error leaf `!defeq` plus `math-negation-missing`, unless the decl names `negation:`.
- A footnote containing `$f defeq g$` written before the decl stays unbound, even though the resolver moves the note to the end.

### An operator with limits (colim, Hom), and an annotated relation (overset)

**Today**

Only the 12 hard-coded TEXTOP|LIMITS rows get limits, through a special case (math.cc:802-809). `limits` does not exist.

**After**

- `$.math.op('colim', {limits: 'display'})`: in display style, `colim_(i in I) F(i)` sets limits through the same attach primitive as `sum`.
- `$limits(=)^"def"$` keeps Rel spacing and its break point.
- `$.math.fn('defas', ['x'], 'limits(=)^(#x)')` packages it. A built-in could do no more.

### HoTT macros (\\eqv, \\idtype, \\prd) with nested arguments

**Today**

tex2tsm regex expansion fails on nesting (tex2tsm.mjs:73-113) and ships `$Id(v,a)rA$` (hott-introduction.tsm:43).

**After**

- tex2tsm translates \\newcommand into `$.math.fn('eqv', ['a','b'], '#a simeq #b')`, `$.math.fn('idtype', ['A','a','b'], 'Id_(#A)(#a, #b)')` and `$.math.fn('prd', ['x'], 'Pi_(#x)')`, and emits `$eqv(A, idtype(U, a, b))$`.
- Expansion splices the arguments, so ≃ keeps its Rel spacing and break point.
- No JS runs after execution.

### Computed values and content inside a formula

**Today**

`$x^#n$` renders x^{#} n. `#mathinline("x^" + n)` re-lexes the value and breaks on `)` or `,`. Colour and links are dropped (math.cc:582-584, typeset_html.cc:474-478).

**After**

- `$x^#n$` -> math(mathsrc('x^'), __hole(n)). With n=-3 this gives x^{−3}, because the value is a parse-isolated hole.
- `$e^(#(2*k) pi i)$` works.
- `$#undefinedVar$` gives a contained Error leaf, not a failed document.
- `$a + #style({color: 'crimson'}, math`b^2`) = #link('#def-c', math`c`)$` colours b² and links an italic c, both bound at emission time.

### Bold vectors, calligraphic and fraktur letters, wide hats and braces

**Today**

Only AA..ZZ are reachable (mathc.py:247-252). tex2tsm drops \\mathbf/\\mathcal (:125-127), \\big (:133) and \\mathopen (:135). pbr2tsm maps underbrace to '' (:303). `hat("p"' - "p")` gets a narrow hat even though Euler compiles wide variants (euler_math.h:2382-2390).

**After**

- `$bold(v) dot bold(w)$`, `$cal(C)$`, `$frak(g)$`, `$bb(1)$`.
- `$hat("p"' - "p")$` stretches horizontally.
- `$underbrace(a + b, n)$` and `$big(\\|)$` work.
- A user row `$.math.fn('vb', ['x'], 'bold(#x)')` is equal to the built-in templates.

### Matrices, cases, inner aligned, and numbered multi-row derivations

**Today**

pbr2tsm flattens matrices (pbr2tsm.mjs:187-215). tex2tsm deletes & and \\\\ (tex2tsm.mjs:166-167). Handlers return strings only.

**After**

- `$mat(a, b; c, d)$`, `$cases(x & "if" x >= 0; -x & "otherwise")$` and the inner `$ aligned(…) $` use the Grid primitive.
- An align environment converts to consecutive display lines, each with its own `<id>`. They form an equations group: per-row numbers (T3), shared alignment (T6), page breaks between rows.
- A fence handler can `return math.mat(rows)`, and the strings in Sym slots are coerced.

### Use a different math font

**Today**

This means re-running mathc.py, regenerating euler_math.h and rebuilding the engine. It also means editing shell.mjs:60, :89-90, export-static.mjs:87 and pack-dist.mjs:26.

**After**

- `python3 tools/mathc.py --font Libertinus-Math.otf …` produces the blob and the woff2.
- The host declares `{family, src, role: 'math', metrics}` and sets `math.fonts: ['libertinus', 'euler']`. The engine waits for the blob (pre-emit gate) and caches it process-wide by hash.
- Libertinus supplies the constants, and Euler covers what it lacks.
- No engine rebuild.

## Invariants

- **I1 su fixed-point determinism; byte-exact goldens**
  Vocabulary, rows and the default font are generated, committed artifacts. The UCD and MathML inputs are vendored and version-pinned.

  MathEnv is a pure function of the decl sequence, and irOf is a pure function of (node, env@epoch, settings). MathPolicy values are converted to su once with integer-exact arithmetic (shortfall as num/den).

  Golden churn by step:
  - Byte-neutral by construction, verified by re-running all goldens:
    - S1: superset glyph gate; seeded from the post-filter header.
    - S2: integer shortfall.
    - S6: the measure projection equals today's synthesized style. Only html for formulas inside colour or link scopes changes, which no current fixture has.
  - Churn, called out:
    - S3: the parse-diag fixture, and new mathir goldens. binom stays identical only if its stack exceeds the natural paren.
    - S4: the language deltas; none expected in test/golden.
    - S5: flattened sums, new break points; fracPadEm in its own commit.
    - S7: T5/T6/T7 formats.
    - S8: js/tree dumps and every .ops re-recorded.
    - S9: additive.
    - S10: optional reference ink, default off.
- **I2 measurement–render robustness (v2 §7)**
  Every leaf is one of these:
  - a glyph of a gated MathFont, painted by codepoint from the same file (woff2 ⊇ blob, hash checked, font-kerning:none, ssty never applied on either side);
  - a text, content or error leaf measured through the pull loop with ε. Its measureStyle is the exact tuple it is painted with, minus paint-only properties.

  The CSS fallback stack is deleted. Uncovered cps and unverified degraded glyphs become measured text leaves. Weight, size and italic bits from the context never reach the measured leaf, which keeps measurement and paint aligned in headings. Nothing re-breaks in the browser.
- **I3 ops contract (reader is a fuzz target; OPS_VERSION discipline)**
  New kinds `math` and `mathsrc`, plus decl{ns:'math'} payloads, all validated at ingest:
  - mathsrc only under math;
  - holes are inline kinds only;
  - cp <= 0x10FFFF;
  - a closed class enum;
  - arity <= 9;
  - a body is a string or a math node.
  Violations give an error node plus a diagnostic.

  OPS_VERSION goes 6->7 exactly once, in S8, bundled with T2's schema bump and one re-record. The math lexer, parser and expander are total fuzz targets: depth <= 256, expansion depth <= 32, <= 4096 nodes per formula and <= 2^20 per document, each counted while expanding, with diagnostics.
- **I4 execution declares, resolver decides**
  - `$.math.*` only appends decl EMITs. JS cannot query MathEnv, the dictionary, coverage or layout.
  - Function bodies are data; no JS function is ever traced or called after execution.
  - Hole expressions run during execution like any splice, and their values are opaque content.
  - Binding is an instantiation-time stamp, like style binding. Parsing and expansion are lazy engine-side functions of (node, env@epoch).
  - The resolver stays the single semantic pass: it sees math labels and numbered attributes, equations rows as separate nodes, and refs or notes inside holes as ordinary kids. It never needs the IR.
- **I5 dual-target rule**
  - MathFont blobs are bytes handed through T9's resource seam.
  - The registry is plain C++.
  - The dict and row tables are generated headers.
  - Native tests use embedded Euler and the mock measurer.
  - The shell only installs declared font entries, the same path as body fonts.
- **I6 emission-time style binding with the DAG/schedule encoding**
  - Leaves project the StyleId at the EMIT position.
  - Names bind at the same point, through the declEpoch stamped by inst.copy. A value reused through the DAG may render or bind differently in two places, exactly like text.
  - Resolver moves (notes) and clones (TOC) carry the stamp, so neither re-binds.
- **I7 block-granular containment**
  Math is stricter than block containment:
  - Hole expressions are `__hole` thunks with try/catch, so a ReferenceError or a throw becomes an Error leaf at the hole's span.
  - Parse, arity and hole-kind errors become Error nodes with resync.
  - A decl error is a diagnostic at the decl, and the decl is ignored.
  - Generated wrapper TypeErrors are script errors, contained by T2's per-block execution containment.
- **I8 resumable pull-loop state machine; atomic per-paragraph upgrade**
  - There is no new Doc state. Text leaves are ordinary needs finalized through T5's generic pending-object hook inside resolveWidths, and KP never runs with pending objects.
  - Pure-glyph formulas finalize at emit, exact from t=0 with the embedded primary.
  - A host primary font is a pre-emit gate, like tokens and images (amending v2 §9).
  - Doc::mathTextMissing and the whole-document re-emit are deleted.
- **I9 performance (hot path parse/codegen/execute/emit; editor fast path)**
  - Ingest only stamps declEpoch (one u32 per node). Parsing is lazy and memoized, once per Doc instead of once per emit pass, so the ~6 ms compile+execute+ingest editor path does not grow.
  - Typed-cp lookup is O(log n) instead of a 322-row scan per character (math.cc:220-225).
  - The env lookup is an empty-overlay check when there are no decls.
  - Removing the re-emit halves emit for the 3,751 of 12,543 corpus formulas with quoted text.
  - A formula without holes costs one extra mathsrc node in ops.
  - The font registry is process-wide, so editor Docs do not reload blobs.
  - Benchmark gate: the pbr-en corpus and the 87K editor bench must not regress.

## Interfaces

- **T1-surface-frontend** (consumes)
  An IslandRule row for `$`:
  - applied inside the linepass loop, crossLines=WithinBlock;
  - escape policy 'raw' (T1 owns the policy, and the math lexer decodes);
  - one mathsrc fragment per source line with container prefixes stripped, each with its SPAN;
  - a math hole sub-grammar: `#[A-Za-z][A-Za-z0-9]*` or `#(…)`, with no `.`, `(`, `;` or [...] continuation;
  - `#(…)` lexed with the JS lexer before the closing `$` is sought, as a v2 §5 amendment with a delta entry;
  - an island map consumed by table-cell splitting;
  - Appendix B display lines lower to math{display:true}, and a paragraph of only display lines becomes group{role:'equations'};
  - the universal `<id>` lowers to `label`.
  The tree-sitter and TextMate grammars mirror all of this.
- **T1-surface-frontend** (provides)
  mathTokens(src, epoch?) -> [{lo, hi, class: sym|op|fn|name|num|text|hole|error}] from the trie, the rows and the parser. Names come from math-vocab.gen.mjs and math-rows.gen.mjs. User-declared names are classified only when an executed env is available; the editor shows them as `name` until then.
- **T2-constructor-ir** (consumes)
  - Per-kind schema rows: math{display, label, numbered; kids mathsrc|inline} and mathsrc{src}.
  - The generic decl{ns, …} kind EMITted at schedule position, applied by instantiate through registered DeclNamespaces, with ContentNode.declEpoch stamped at copy and preserved by clones. No dedicated mathdecl kind ever ships.
  - Level normalization: math{display:true} is block-level and splits paragraphs.
  - `await __hole(async () => expr, s, e)` thunks.
  - One stdlib generator that consumes math-rows.gen.mjs and emits std-qualified wrappers with Sym-slot coercion.
  - Explicit number/string conversion inside math().
  - Per-block containment for wrapper TypeErrors.
  - One coordinated OPS_VERSION bump.
  - `__registerModule` at #use sites.
- **T2-constructor-ir** (provides)
  - The row data (engine/data/math/symbols.tsv, stdlib.tsv and the primitive slot table), generated as math-rows.gen.mjs: name, params {name, kind, optional}, rows flag, bare and cls.
  - The 'math' DeclNamespace implementation.
  - Deprecated aliases mathinline/mathblock -> math().
- **T3-semantics** (consumes)
  - Element class `equation` for math{display:true}. The resolver wraps a numbered one as equation{label}(math, tag content), so the math node never holds a tag.
  - `numbered` as a universal attribute.
  - Each row of group{role:'equations'} is an equation.
  - Structured excerpts clone math nodes, keeping declEpoch and the IR memo.
  - The resolver walks math hole kids like any content (refs resolve, notes become markers in place).
- **T3-semantics** (provides)
  copyText for every math node, display-padded, with holes contributing their inner source and no nested `$`. It is available without parsing or layout.
- **T4-style-settings** (consumes)
  - The property registry classification (metric-affecting vs paint-only), used to project leaf styles.
  - Font roles: 'math' (declared webfont with a metrics resource) and 'math-text' (text-leaf family).
  - The settings registry with a `math.*` namespace (MathPolicy fields, break tables keyed by atom-class name, fonts, implicitNames, text.weight), plus su conversion with a fixed rounding mode.
  - The fontPx/sizePx fix, applied to glyph basePx; leaves derive from it.
  - DeclEnv scoping frames (doc-global from position until T4 defines region frames).
  - CSS contract generation: font-kerning:none on .tsr-mg and no fallback family.
- **T4-style-settings** (provides)
  Schema rows for math.* settings. Each has a type, a default and a value domain:
  - penalties in [0, INF];
  - fracPadEm in [0, 0.5];
  - fonts must name declared role-'math' fonts.
  All are marked metric-affecting, and all feed the envStamp.
- **T5-text-shaping** (consumes)
  - InlineObject{w, asc, desc, lbStart, lbEnd, painter, payload, copyText, pending} on Item::Box.
  - Object groups whose explicit producer Penalty/Glue items are authoritative inside the group, with the pair table ruling only at group edges.
  - A generic pending-object finalize hook called by resolveWidths without naming any producer.
  - Glue.synthetic, replacing BF_BOUND at layout.cc:73,546, emit.h:50, emit.cc:863 and typeset_html.cc:491,588.
  - shapeInlineBox(content, style) -> nowrap box {w, asc, desc, runs, anchors} for content holes.
- **T5-text-shaping** (provides)
  A math item producer:
  - one Box per segment, with lbStart/lbEnd from the first/last atom class (Open->OP, Close/Punct->CL, else AL);
  - between segments, synthetic discardable Glue + Penalty from the math.break tables;
  - the math painter id.
  The penalty values are TeX math parameters, owned by math in T4's registry.
- **T6-layout-pagination** (consumes)
  - A DisplayBox for one equation: body object, measured tag on a shared baseline, collision policy (tag drops below), display skips from settings, and a display-overflow warning (layout.cc:388-389).
  - An equations layouter for group{role:'equations'}: column widths shared across rows from the alignment offsets, a tag per row, page breaks allowed between rows.
  - One line-metrics function for object-bearing lines (replacing layout.cc:332-335, :446-449, :528-531).
- **T6-layout-pagination** (provides)
  Per display row: one InlineObject with the axis height and the x-offsets of its top-level `&` alignment points. Later and optionally, top-level Rel break items for overlong rows (same producer).
- **T7-render-runtime** (consumes)
  - Display-list Box/Run items with a painter id.
  - SynKind::Math with data-copy and a copy group for split formulas (replacing copy.mjs:15-18 and audit.mjs:20-24).
  - role=math with aria-label from copyText.
  - AnchorNamer and runs nested inside math objects (content leaves, note markers with fnref anchors).
  - A future slanted-line item if cancel is adopted.
- **T7-render-runtime** (provides)
  The math painter:
  - glyph leaves by codepoint, with family and hhea from the registry by fontId;
  - rules as boxes;
  - text leaves painted through openRun with their paintStyle;
  - content leaves as the display-list runs T5 returned.
  Also copyText and the IR with scope annotations, for MathML/a11y.
- **T9-host-protocol** (consumes)
  - The generic need/finalize protocol: need(textWidth), need(fontVmet), and optionally reference ink with deterministic mock values.
  - A `mathFont{hash}` resource (tsmf bytes) declared in the single font manifest, acting as a pre-emit gate for non-embedded primaries.
  - A process-wide registry lifetime.
  - envStamp per math node as a product-graph key.
  - Per-(stage, pid) diagnostic slices.
  - The settings JSON ABI carrying math.*.
- **T9-host-protocol** (provides)
  - Removal of Doc::mathTextMissing, its hand merge (doc.h:367-378) and the math-triggered re-emit.
  - envStamp(epoch) for invalidation.
  - A tsmf validator usable as a resource-load check.

## Migration

### S0 (T1-owned prerequisite) Bound math islands  → plan P0-04

- contiguous() returns false past the block's last span.
- splitCells consumes the island map, so `|` in a formula no longer cuts table cells and islands cannot escape a block.
- The tex2tsm `\\mid -> ∣` workaround (tex2tsm.mjs:129) can later be dropped.

**Golden impact:** None in test/golden. New regression fixtures are added (including the `> quoted $a +\n> b$` and list-item overrun probes).

**OPS bump (as designed):** False

**Fixes:** `math/island-scan-escapes-block`, `math/missed:0`

### S1 MathDict split (byte-neutral)  → plan P1-22

- Seed symbols.tsv from the generated header (the 322 post-filter rows), with default_for_cp reproducing first-hit.
- Vendor pinned UCD and MathML Core files.
- tools/mathdict.py generates math_dict.h and atom.h.
- mathc.py stops emitting OpEntry and the class enum, and stops adding dictionary cps. RANGES gains 0x02B0-0x02FF, with a CI gate that the kGlyphs cp set is a superset of the previous set.
- The lexer uses the trie and the cp index, but KEEPS its `!word` and `_|_` branches. The key-lex gate allowlist covers {o+, o-, o., :', _|_, !word rows}.
- Fix architecture.md §1/§5.

**Golden impact:** None: mathbox, blocks and html are byte-identical (all goldens re-run). euler_math.h grows by the new spacing-modifier records, and no fixture references them.

**OPS bump (as designed):** False

**Fixes:** `math/dead-data-and-params`, `math/doc-drift (architecture part)`, `math/vocabulary-in-font-artifact (partial)`

### S2 MathFont object, process-wide registry, paint decoupling (byte-neutral)  → plan P1-23

- Wrap Euler as MathFont id 0 and thread it through MathCtx. textFont becomes LeafSource.
- typeset_html reads family and hhea from the registry.
- mathc.py emits a woff2 (with U+0020) plus a hash.
- The shell installs math as a declared-webfont entry with role 'math' through ensureFontFaces and drops the second @font-face path and the STIX fallback. .tsr-mg gets font-kerning:none.
- MathPolicy holds today's literals, with the shortfall as the integer rational 1/10.

**Golden impact:** None for engine goldens: the integer shortfall reproduces `target -= target/10`. The e2e math audit may shift by sub-pixels and gains the §11 baseline and glyph-position audits.

**OPS bump (as designed):** False

**Fixes:** `math/exactness-gaps-paint`, `math/compiled-in-font (partial)`

### S3 MathRow registry, Call IR, error containment, lazy bind  → plan P1-24

- Add the primitive table plus stdlib.tsv templates (abs, norm, floor, ceil, binom, accents, bar, sqrt, root) and checkRow.
- MNode{Sym…Error}; slot specs give arity; primes merge.
- Error leaves resync and carry sub-span diagnostics with hex cps.
- ContentNode.declEpoch is stamped by instantiate (always 0 until S8). irOf parses lazily from today's src arg with an empty env, and the parse moves out of every emit pass.
- Calls bind only on adjacent `name(`. `bare` is added (dot -> ⋅), and `not` stays ¬.
- New `--stage=mathir` dump with per-formula diagnostics.

**Golden impact:** - The parse-diag fixture changes from a whole-formula text box to a partial layout plus an Error leaf: mathbox, blocks, layout and html change (called out).
- binom: byte-identical if its stack exceeds the natural paren (re-inspect stretch.mathbox). Otherwise the stretch goldens move by the centring of the natural paren (called out).
- New mathir goldens, including diagnostics.

**OPS bump (as designed):** False

**Fixes:** `math/call-construct-string-dispatch`, `math/call-arity-silent`, `math/prime-then-script-degrades`, `math/diag-quality`, `math/missed:1`, `math/missed:5`

### S4 SymbolInfo identity, data-driven families, language deltas  → plan P3-24

- Predicates read only SymbolInfo.
- Auto-lr from Open/Close classes; Sym slots; fence pairing with mid() lowering.
- The `!` rule (Rel or negate≠0, Error leaf otherwise) and the UCD negation map.
- Variant/Class/LimitsMode rewrites and the bb/cal/frak/bold/sans/mono/italic rows; AA..ZZ as aliases.
- Space rows plus MStyle rows; dotted and std.-qualified names.
- (…)-only shedding and the single-token operand rule.
- Drop the allowlisted keys and the !word/_|_ branches.
- Generate converter maps, add the CI lex gate, and check in tools/math-corpus.py (scripted with tsrc --stage=js).
- Record the deltas in design-decisions.md and math-design §13: `#`, adjacency, x^ab, a/bc, (…)-only, bars as mid, `vec` stays an accent unlike Typst, `cases` rows use `;`.

**Golden impact:** Expected none in test/golden: the fixtures use none of the affected constructs, and `!` occurs only as factorial before `/` and `)`. To be verified.

Corpus re-render diff to review, for pbr and zball-io/src/docs:
- 54 formulas with conditional or set-builder bars gain Rel spacing;
- 64 `||` norms are unchanged;
- 22 `!=` are unchanged;
- 0 bracket arguments, 0 `o+` keys, 0 non-row words in operands, 0 `name (` calls.

**OPS bump (as designed):** False

**Fixes:** `math/bracket-shedding-any-group`, `real-world-evidence/math-leniency-silent`, `math/missed:2`, `math/missed:3`, `math/missed:4`, `math/lexer-hardcoded-alphabet`, `math/negation-enumerated`, `math/alphabet-variants`, `math/fence-pairs-ascii-only`, `math/implicit-names-op-class`

### S5 Operator atoms and the single math-list -> item conversion  → plan P3-25

- Remove BigOp: Op atoms carry a limits mode, and scopeEnd is an annotation only.
- One demotion pass over the spliced top-level atoms.
- The math.break settings tables replace the three config keys.
- Delete effClsOf.
- attach propagates edges; assemble propagates topAccent.
- fracPadEm 0.1em lands as a separate commit.
- BF_BOUND is KEPT on math glue until T5's Glue.synthetic lands.

**Golden impact:** - break fixture: flattened sum bodies and new break points.
- mathbox: flatter trees for sums and integrals, with positions unchanged when unbroken.
- blocks and html: unchanged apart from the new break glue ('boundary' and data-syn kept).
- fracPadEm moves every fraction in mathbox, blocks, layout and html: its own commit, with numeric review.

**OPS bump (as designed):** False

**Fixes:** `math/spacing-edge-classes`, `math/bigop-greedy-body`, `math/segmentation-class-preview`

### S6 Lazy layout, generic pending finalize, style projection  → plan P1-25

- prepareMath runs at emit over irOf, producing the segment plan, needs and pending objects. finalize runs through a minimal generic pending-object hook in resolveWidths, which T5 later owns and which never names math.
- Delete Doc::mathTextMissing, the re-emit and the hand merge.
- Leaves use measureStyle (today's tuple) and paintStyle (+color, link) and paint through openRun.
- Uncovered cps become measured text leaves.
- Add the varrho, varsigma and ::= rows.
- Coverage diagnostics fire once.

**Golden impact:** blocks, breaks, layout and html are byte-identical for every current fixture (no fixture puts math in a colour or link scope, and headings are unaffected because no bits are inherited). Diagnostics are no longer duplicated.

**OPS bump (as designed):** False

**Fixes:** `emitter/full-reemit-for-math-text`, `math/math-text-pull-channel`, `api-measure-code/math-text-measure-side-channel`, `math/math-leaves-bypass-style`, `math/missing-glyph-fallback (single-font part)`

### S7 Adopt the T5 InlineObject/groups/Glue.synthetic, T6 DisplayBox and T7 copy/a11y protocols  → plan P3-26

- Math registers its painter and emits Items.
- Delete LinebreakBlock::math, FlowUnit::K::Math, special=4 and the renderer-side centring.
- BF_BOUND on math glue becomes Glue.synthetic.
- data-copy and SynKind::Math replace the data-syn inversion; role=math and aria-label come from copyText.

**Golden impact:** html: data-copy, explicit display baselines, data-syn per T5/T7. layout: display skips. blocks: object lines. T5, T6 and T7 own and call out these format changes.

**OPS bump (as designed):** False

**Fixes:** `math/fallback-and-a11y`, `math/inline-math-special-block`, `emitter/math-only-inline-box`, `math/display-math-unit`

### S8 The math/mathsrc kinds, holes, decls via DeclEnv, JS API (with T2)  → plan P2-15

- ops.def: math + mathsrc; decl{ns:'math'}; OPS 6->7.
- Codegen lowers islands into per-line mathsrc fragments and `__hole` thunks (hole sub-grammar from T1), with display/label/numbered options.
- The executor provides math(), math``, generated std-qualified wrappers, math.sym, math.call, math.equations and $.math.symbol/op/fn (data bodies).
- instantiate applies decls (MathEnv) and stamps declEpoch; irOf binds against env@epoch.
- fragment.cc produces math(mathsrc).
- excerptInto skips math, as today.
- T3 wraps equations.

**Golden impact:** - js goldens containing math; tree dumps (kind names, mathsrc kids); every .ops re-recorded.
- mathbox, blocks and layout are unchanged.
- html data-src keeps the display padding (copyText is padded).
- eqref html and layout change as T3's equation wrapper lands (called out by T3).
- New fixtures: holes, contained hole errors, decls, macros, a note inside math, and a decl after a footnote use.

**OPS bump (as designed):** True

**Fixes:** `math/toc-excerpt-drops-math (interim)`, `math/math-opaque-string`, `math/closed-vocabulary`, `math/math-island-oneoff-syntax`, `math/equation-numbering`

### S9 Grid, equations groups and the remaining primitives  → plan P3-29

- Grid (`;` rows, `,` cells when cells=true, `&` alignment) with mat, pmat, cases and aligned templates.
- group{role:'equations'} with T6's layouter and T3 per-row numbering.
- HStretch, horizontal Accent stretch, Delim (big family), Phantom, Class and attach tl/bl/tr/br.
- tex2tsm and pbr2tsm emit these instead of flattening or deleting, and emit a diagnostic for cancel.

**Golden impact:** Additive (new fixtures). The 8 corpus wide-hat formulas change appearance; corpus review only.

**OPS bump (as designed):** False

**Fixes:** `real-world-evidence/math-extensibility`

### S10 Multi-font chain, host fonts, optional reference ink  → plan P5-01

- registry.load() for host .tsmf through T9's mathFont resource, as a pre-emit gate, cached process-wide by hash.
- The math.fonts chain, with the primary supplying constants and the mismatch warning.
- One font manifest serves the static export and pack-dist.
- Amend v2 §9.
- Behind a setting: per-style reference ink.

**Golden impact:** None for Euler-covered input. Reference ink ships default-off.

**OPS bump (as designed):** False

**Fixes:** `math/compiled-in-font`, `math/missing-glyph-fallback`

## Not generalized (kept special)

- **The TeX 8×8 spacing matrix, Bin demotion Rules 5–6 and the 8-style transition tables (math.cc:35-62)** — These are typographic law (TeXbook Appendix G), not vocabulary. Users reach the style algebra through display/inline/script/cramped and class(), and do not edit it.
- **The primitive table (14 layout primitives + 4 bind-time rewrites) stays closed C++** — In WASM, JS cannot register C++ procedures, and a user-defined layout procedure would break measurement-free determinism. Equal footing is defined at the row level: every built-in family is a template row a user could have written, and only `prim` is privileged.
- **cancel/bcancel/xcancel** — They need a slanted-line display-list item; Rule boxes cannot slant (math-design §13). Until T7 adds one, converters emit `math-unsupported` instead of silently deleting. Open question 6.
- **Templates have no conditionals, recursion or JS bodies** — Keeps I4 and post-execution emit free of JS. HoTT's macros are pure templates. Computation over values belongs in `#(…)` holes, which run during execution with real values.
- **Codepoint painting, the cmap-reachability gate, Euler embedded as default; ssty and MathKernInfo unused** — Only codepoint painting keeps paint identical to the precompiled metrics in every browser (math-design §1). ssty alternates are unencoded glyphs, and neither metrics nor CSS apply them, so the two stay consistent. Using them requires PUA injection in mathc.py, the documented escape hatch. Euler has no MathKernInfo.
- **The implicit-name default class stays Op** — This is the documented math-design §14 trade-off, and the demotion is TeX-faithful. The fix is diagnostics plus explicit op()/upright()/"…".
- **The v2 §13 greedy big-operator scope survives only as a parse annotation** — It is a decided reading-precedence rule worth keeping for MathML/a11y. As a layout node it caused the segmentation defects.
- **Line-breaking WITHIN one display row, and sub-numbering (1a, 1b)** — Multi-row displays are equations groups, so per-row numbers and page breaks are covered. Breaking an overlong single row belongs to T6 (the same producer could feed it). Sub-numbering is a T3 numbering-pattern feature.
- **Math-specific declaration scoping** — MathEnv inherits the generic DeclEnv rule: document-global from the declaration's position, plus T4's region frames if adopted. A math-private scoping rule would diverge from styles and settings.
- **"quoted" text stays a lexical form, not a hole** — It is the literal upright-text spelling that needs no JS. A string hole converts to the same Text(Quoted) leaf, so the two paths converge in the IR.

## Risks

- Language deltas need delta entries and user communication:
  - `#` holes inside `$…$`, with JS-lexed `#(…)` (v2 §5 amendment);
  - adjacency-only `name(` calls;
  - single-token operand words (x^ab = x^{ab}, a/bc = a/(bc));
  - (…)-only shedding;
  - unpaired bars become mid;
  - `o+ o- o. :'` are dropped;
  - `vec` stays an accent, and `cases` rows use `;` (both unlike Typst).
  The reproducible scan (12,543 formulas in 249 of 577 .tsm files; tools/math-corpus.py in S4) found 0 uses of every breaking form except the 54 bar formulas, whose spacing improves. zball-io/src/docs must be added to the review, and external blog sources are not scanned.
- Fence pairing with lookahead and mid() lowering is still a heuristic. `a | b | c` reads as a·|b|·c. Mitigation: abs()/mid() as the unambiguous spellings, documented in the deltas.
- Deriving classes from MathML Core can disagree with TeX (e.g. ⊥, ∄). The override column is seeded so S1 is neutral, and later class changes are deliberate, golden-reviewed edits.
- OPS_VERSION 6->7 must be bundled with T2's schema bump. S8 depends on T2's decl kind and DeclEnv: if T2 slips, S8 waits, and no interim mathdecl kind ships.
- Macro expansion and holes enlarge the fuzz surface. The budgets must be enforced while expanding, both per formula and per document.
- Positional env semantics: documents that put declarations after use get a `math-unknown-name` hint, unlike TeX-preamble habits. Lazy parsing must always use env@declEpoch, never the final env. A test covers a decl placed after a footnote use.
- Content holes add measured leaves and runs nested inside .tsr-math, a new DOM shape. Copy, audit and selection must handle them (T7). Until T5's shapeInlineBox exists, content holes degrade to their copy text as a measured text leaf.
- Lazy finalize: KP must never run with pending objects. A host primary math font adds a pre-emit wait that does not exist today.
- If T5, T6 or T7 land late, S7 and the S9 equations groups stay blocked, and LinebreakBlock::math, special=4 and BF_BOUND remain adapters. S1–S6 do not depend on them.
- The fracPadEm and attach-edge fixes in S5 shift many goldens. Land them as isolated commits with numeric mathbox review.

## Open questions (decided in PLAN.md §3)

- Should `{…}` in scripts and fractions be an invisible TeX-style group or visible braces (Typst)? This design assumes visible, with class(ord, …) as the explicit atomic group and (…) as the shed group.
- Should DeclEnv adopt T4 region frames, so that a `$.math.symbol` inside a region is local to it, or stay document-global from position, like $.fence?
- Should text-leaf vertical extents adopt a per-style reference ink (cap/x-height) through T9's fontVmet, or keep line metrics?
- Should MathPolicy.implicitNames default to `info`? It surfaces converter bugs (rA, Sn, x_in) but adds noise to converted corpora.
- Should the semantic/static output (T7) render finalized MathBoxes or MathML from the IR? If neither consumer wants the scope annotation, it can be dropped.
- Is cancel worth a slanted-line display-list item in T7, which would also serve strike-through in math?
- Should the math hole grammar allow `#p.x` member chains? They are excluded today because `.` is decimal or punctuation in math, so `#(p.x)` is required.
- Equations-group surface: the implicit 'paragraph of only display lines' rule (T1), or an explicit `#!equations … #equations!` region?

## Changelog (critique responses)

- C1-blocker (ops encoding: fragments and string holes share kind `text`): ACCEPTED. Verified executor.mjs:59-62 and opbuf.mjs:40-46. Fragments are now a distinct `mathsrc` kind (each with its own SPAN). Any other kid is a hole. math()/math`` convert numbers to math holes and strings to text holes explicitly. The reader validates both. A separate `param{k}` kind is REJECTED as unnecessary: function bodies are data, with parameters referenced lexically as `#name` inside mathsrc, and the traced-function form is removed (see C2-macro-forms).
- C1-major (bindMath after the resolver binds moved or cloned formulas at the wrong position): ACCEPTED. Verified resolve.cc:362-393, :509, :528-529 and model.cc:77-100. Decls are schedule-ordered decl EMITs applied by instantiate through T2's DeclEnv. inst.copy stamps ContentNode.declEpoch, and clones and moves carry it. irOf binds against env@epoch. Holes are referenced by kid index, so the resolver can replace them (notes -> markers) safely.
- C1-major (Appendix A splice heads collide with math: `_`/`$` in identifiers, `(` and `.` continuations, the `;` terminator, `$` inside `#(…)`): ACCEPTED. Verified jslex.h:90-93, inline.cc:176-188, :213, :269-273. The math hole sub-grammar is `#[A-Za-z][A-Za-z0-9]*` or `#(…)`, with no continuations or terminator. The island scan JS-lexes `#(…)` before seeking the closing `$`. It is recorded as a v2 §5 amendment, owned by T1 and mirrored in the editor grammars.
- C1-major (I7: a hole error fails the whole document): ACCEPTED. Holes lower to `await __hole(async () => expr, s, e)`, which catches into an error kid (an Error leaf at the hole span), mirroring __fence (executor.mjs:156-160).
- C1-major (S1 not byte-neutral: six accents compiled only via DICT; three rows resurrected): ACCEPTED. Verified mathc.py:87-94, :336-350, the header's 322 vs DICT's 325 rows, and the header line `{0x2C6,…}`. RANGES gains 0x02B0-0x02FF so the glyph set no longer depends on the vocabulary, with a superset CI gate. symbols.tsv is seeded from the post-filter header. varrho, varsigma and ::= move to S6, behind the coverage chain.
- C1-major (lr/mid examples cannot parse under auto-fence rules): ACCEPTED. A `Sym` slot kind reads exactly one symbol token with auto-lr and bar pairing suspended. lr, mid, delim and accent use it, and parser goldens cover the exact surface examples.
- C1-major (negation narrowed to negate≠0; base-glyph fallback inverts meaning): ACCEPTED. `!` before a Rel-class symbol or any symbol with negate≠0 is negation. If there is no form or no coverage, it becomes an Error leaf showing the source plus `math-negation-missing`: never the base glyph, never factorial. User rows may declare `negation`. `n!<m` stays ≮.
- C1-major (`not` collides with ¬; whitespace before '(' unspecified): ACCEPTED. Verified mathc.py:212 and 8 corpus uses. `not` stays ¬, and the Not core is removed: negation is the `!` rule only. Calls require `name(` with no space (0 corpus `name (` uses), recorded as a delta. A generation gate requires `bare` when a Function row's name is also a symbol.
- C1-major (tracing with placeholders unsound; math.<userFn> semantics; namespace collisions): ACCEPTED, by removing tracing. Bodies are data (string or math value), and passing a function is a TypeError. User rows are called from JS through `math.call(name, …)`, which emits a Call bound at the emission epoch. Built-in wrappers emit `std.`-qualified names that user declarations cannot define.
- C1-major (S6 composition leaks heading bold; glyph/text sizePx split; wrong golden claim): ACCEPTED. Verified emit.cc:251, :500-501 and measure.h:66-68. The leaf measureStyle is exactly today's synthesized tuple (sizeMul from the glyph basePx), and paintStyle adds only paint-only properties (color, link). Weight requires `math.text.weight: inherit`. S6 is byte-neutral for the current fixtures, and the I1 row is corrected.
- C1-minor (S8 hidden changes: excerpts, fragment.cc, copyText padding, nested `$`): ACCEPTED. excerptInto skips math, as today, until T3 clones. fragment.cc is in S8. copyText keeps the display padding (typeset_html.cc:150-153), and holes contribute inner source with no delimiters.
- C1-minor (MathGlue dropping BF_BOUND breaks five consumers): ACCEPTED. Verified layout.cc:73,546, emit.h:50, emit.cc:863, typeset_html.cc:491,588. S5 keeps BF_BOUND, so blocks and html keep 'boundary'. T5's Glue.synthetic replaces it at all five sites in S7.
- C1-minor (S6 depends on S8's bindMath; invariant table contradiction): ACCEPTED. S3 introduces the lazy, env-less irOf with declEpoch stamping, and S8 only adds decls. The I1 row is corrected.
- C1-minor (float delimiterFactor drifts from integer truncation): ACCEPTED. Verified math.cc:933, :1132. The shortfall is the rational {1,10}, applied as target - target*num/den in integers, and all policy values are converted to su once with a fixed rounding mode.
- C1-minor (host fonts break v2 §9; registry lifetime per Doc): ACCEPTED. A non-embedded primary is a pre-emit gate (doc.h:244 pattern), with a v2 §9 amendment. The registry is process-wide and keyed by content hash.
- C1-minor (macro budgets counted on shared IR; no per-document cap): ACCEPTED. Expanded, layout-visible size is counted during expansion, with a per-document budget of 2^20.
- C1-minor (Grid grammar inconsistent): ACCEPTED. `;` rows; ',' cells only when the rows slot declares cells=true (mat); `&` alignment points; named args before the first cell; parse goldens. cases uses `;` rows, unlike Typst (recorded delta).
- C1-minor (link(math) not a math subtree; ContentLeaf drops anchorId): ACCEPTED. styled or link wrappers whose sole kid is math become math subtrees with paint-only properties. Other content goes through T5 shapeInlineBox, which keeps anchors (fnref-n).
- C1-minor (corpus numbers not reproducible; bar policy noisy): ACCEPTED. Re-ran with the engine's own lowering: 12,543 formulas, 249 files, 3,751 with quoted text, 22 `!=` and no other negations, 0 bracket arguments, 54 bar formulas, 64 `||`. tools/math-corpus.py is checked in at S4. Unpaired operator-position bars lower to mid() with no diagnostic.
- C1-minor (S1 allowlist misses the !word rows): ACCEPTED. S1 keeps the `!word` and `_|_` lexer branches explicitly and allowlists them, and S4 removes both.
- C1-minor (#use and fences auto-registration cited as existing): ACCEPTED. Marked as dependent on T1/T2 implementing #use, with codegen emitting `__registerModule(mod)` at the #use site.
- C1-minor (unicodedata version drift): ACCEPTED. The UCD and MathML Core operator dictionary are vendored with pinned versions, and the generator parses only those files. MathML form precedence is defined (infix gives the class, prefix/postfix give fence flags, override wins).
- C1-missing_items (opaque-string, closed-vocabulary, negation, missed:1, fence pairs, leaves-bypass-style, vocabulary-in-font): ADDRESSED through the fixes above. Each subsumption note now states the corrected mechanism.
- C2-blocker (users not on equal footing; composite built-ins do not fit the row type): ACCEPTED and restructured. There is ONE MathRow type and one checkRow gate. Families are template rows in stdlib.tsv, written in the user template language. Only the C++ primitive table sets `prim`. Users can declare params (kind, optional, default), rows, result class, bare, negation and spaceMu. Identical IR now holds by construction.
- C2-major (closed core lacks class, size, phantom, cancel, hstretch, pre-scripts): PARTIALLY ACCEPTED. Verified tex2tsm.mjs:133,135, pbr2tsm.mjs:141,303, euler_math.h:2382-2390 and 8 corpus wide hats. Added Class, Delim (big family), Phantom, HStretch, horizontally stretching Accent and attach tl/bl/tr/br. cancel is deferred because it needs a slanted-line paint item (T7); converters emit a diagnostic instead of deleting.
- C2-major (aligned as one atomic object rules out per-row numbers and page breaks): ACCEPTED. A top-level multi-row display is group{role:'equations'} of math rows: per-row labels and numbering (T3) and page breaks plus shared alignment (T6). The inner aligned/cases/mat stay Grid, mirroring TeX's align vs aligned.
- C2-major (bindMath L3 label but runs after L4): ACCEPTED, same fix as the C1 binding issue. Binding is an instantiation-time stamp, and parsing is lazy. The resolver never needs the IR, because rows and labels are structural and holes are ordinary kids.
- C2-major (math content model vs tag-as-content-child): ACCEPTED, preferred option. T3 wraps numbered displays as equation{label}(math, tag), so the math node's kids stay pure.
- C2-major (holes and expansions atomic for layout): ACCEPTED. They are parse-isolated but SPLICED into the parent Run for spacing, demotion and segmentation. class(ord, …) makes them atomic, and Runs expose first/last edges.
- C2-major (wrapper hygiene, namespace collisions, strings in symbol slots): ACCEPTED. Wrappers emit `std.`-qualified calls; `std.` is reserved; user functions are reached by math.call; Sym slots coerce strings through math.sym; T2's single generator consumes math-rows.gen.mjs.
- C2-major (leaf styling bypasses the T4 registry): ACCEPTED. Leaves use measureStyle and paintStyle, projected by T4's metric/paint classification, plus the 'math-text' font role. Size or weight deltas inside math give `math-style-ignored`, and a boldmath-like setting is `math.text.weight`.
- C2-major (math penalties vs T5 pair table; constant AL edges): ACCEPTED. Producer penalties are authoritative inside an object group, and the pair table rules only at group edges. lbStart/lbEnd come from the first/last atom class. This also closes open question 6: the values are math-owned TeX parameters in T4's registry.
- C2-major (provenance: contiguous-source assumption): ACCEPTED. Verified with the critic's quote/list probes and inline.cc:268-275. There is one mathsrc fragment per source line, with container prefixes stripped by T1, and each fragment has its own SPAN. JS fragments fall back to the formula span.
- C2-major (hole syntax `;` and `(` clashes; `#1` second dialect): ACCEPTED. The math hole sub-grammar (see C1) applies, and template parameters use the same `#name` spelling. `#1` is dropped.
- C2-major (two macro definition mechanisms): ACCEPTED. There is a single data template form, and the traced JS form is removed, so placeholder-coercion failures cannot arise.
- C2-minor (ContentLeaf re-implements inline shaping): ACCEPTED. Content holes use T5 shapeInlineBox as opaque leaves, and math only positions them.
- C2-minor (math-specific finalize hook, double migration): ACCEPTED. S6 implements a minimal generic pending-object hook (later T5-owned) that resolveWidths calls without naming math. T9's needs plug into the same hook.
- C2-minor (Not, Variant and Text listed as layout cores): ACCEPTED. The primitive table is split into 14 layout primitives and 4 bind-time rewrites (Variant, Class, LimitsMode, TextAtom), and Not is removed (negation is the `!` rule). Coverage is checked once per leaf at bind.
- C2-minor (second @font-face path beside declared webfonts): ACCEPTED. Verified shell.mjs:92-110, :200-206 and worker.mjs:39-60. The math font is a declared-webfont entry with role 'math' plus a metrics resource, so one manifest serves paint, the worker, export and pack.
- C2-minor (which font supplies MATH constants in a chain): ACCEPTED. The primary supplies constants, a secondary supplies glyphs and chains scaled by its upem, and a `math-font-mismatch` warning fires on axis or x-height divergence.
- C2-minor (user op keys with structural characters; munch changes): ACCEPTED. Digits and structural characters are forbidden in keys, and an info `math-key-munch` fires when a key extends an existing one.
- C2-minor (fraction-slot change, vec naming, zball docs, x_in): ACCEPTED. The single-token operand rule covers fractions (a/bc, ab/c) and is listed in the deltas; the corpus has 0 such words. `vec` stays an accent (a documented zball-io surface), recorded as a divergence from Typst. zball-io/src/docs joins the review, and `x_in` gives an info under implicitNames=info.
- C2-minor (set-builder bar gets Ord plus a diagnostic): ACCEPTED. Unpaired operator-position bars lower to mid() (Rel, stretched) with no diagnostic, covering 54 corpus formulas.
- C2-minor (limits() result class Op loses Rel): ACCEPTED. LimitsMode is a bind-time rewrite that keeps the base class.
- C2-minor (no invalidation key for the decl prefix): ACCEPTED. MathEnv::stamp(epoch) hashes the decl prefix and the math.* settings, and is exposed to T9 as a per-node key.
- C2-missing diag-quality: ACCEPTED. The `--stage=mathir` golden includes per-formula diagnostics (sub-spans, cap, once-only).
- C2-missing exactness-gaps-paint/ssty: ACCEPTED. Disposed explicitly: neither metrics nor CSS apply ssty, so they stay consistent; using ssty requires PUA injection (not_generalized).
- C2-missing doc-drift §8 coalescing: ACCEPTED. The promise is retracted in math-design §8; T7's display list may coalesce runs.
- C2-missing equation-numbering conflict: ACCEPTED, fixed by T3's equation wrapper.
- C2-missing math-island-oneoff-syntax: ACCEPTED. Mid-paragraph display is decided (a block that splits the paragraph through T2 level normalization; 0 corpus cases), and labels always attach because math carries `label`.
- C2-missing fence-pairs (mid semantics): ACCEPTED, through the mid() lowering above.
- Overlaps (both critics): T1 owns the escape policy, the per-line fragment spans, the hole sub-grammar and the editor grammars, and mathTokens classifies user names only after execution. T2 owns one generator, the decl kind, DeclEnv, __hole and the single bump, and no mathdecl kind ships. T3 owns the equation wrapper, `numbered`, excerpt clones and per-row equations. T4 owns the property classification, font roles, the math.* registry and DeclEnv frames (the decl scope rule is shared, not math-private). T5 owns object groups, Glue.synthetic, shapeInlineBox and the pending hook. T6 owns the equations layouter. T7 owns anchors and runs inside math. T9 owns the need protocol, envStamp in the product graph, the single font manifest and the pre-emit font gate. All of these are reflected in the interfaces.
