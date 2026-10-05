# T2-constructor-ir

> **Audit design input (generated, English).** Produced by the 2026-10-05 audit (architect → two adversarial critics → revision). Where it conflicts with `docs/remediation/PLAN.md` (decision register §3) or with `INTEGRATION.md` (conflict resolutions), PLAN.md wins, then INTEGRATION.md. Step ids are mapped to plan steps in the Migration section. Line numbers refer to commit ecc3a89.

## Thesis

Make the constructor call the single, schema-described unit of the IR, and reach it by exactly one execution path. One versioned schema is the source for the JS stdlib, the C++ reader, codegen, the normalizer, dumps and docs. It holds kinds with a level class and body/slot content models, per-kind typed attributes with value domains, constructor parameter specs and ops, and every row carries its own `since`. All markup lowers to one LowerProgram of bound constructor calls with holes for user JavaScript: the document, an m`` template, ctx.m.parse and a sidecar note alike. One interpreter runs that program against one registry. In the registry, built-ins, regions, fences and user extensions are entries with one bound calling convention and `next` delegation, invoked inside frames that contain errors and bound style scopes at every block depth. Content values are immutable Nodes that belong to no op buffer. Instantiation is the one place that turns their op DAG into a normal, per-occurrence, budgeted tree, and resolver output goes through the same normalizer. As a result, sugar becomes a late-bound registry entry, user code never shares a scope with generated code, user extensions stand on the same footing as built-ins, and new vocabulary is a schema row that only the buffers using it advertise.

## Diagnosis

The constructor is the concept the governing principle rests on ('every syntax form is sugar for a constructor', design-decisions-v2.md:107), yet it has no single definition:
- its names live in codegen's destructured prologue (codegen.cc:209-212);
- its semantics live in a hand-written JS bag (executor.mjs:162-237);
- its wire vocabulary is a flat ARGK list with no per-kind schema (ops.def:43-75);
- its level lives in a resolver helper that disagrees with emit: resolve.cc:30 lists raw as inline, while emit.cc:685 handles raw only as a block;
- a second, divergent lowering lives in fragment.cc:34-93.
Five root causes follow.
(1) There is no calling convention. Each positional signature was shaped by its codegen call site, so `#heading(2)[T]` fills the label slot and `#list(false)[a]` stores a node id as `start`.
(2) Generated code and user code share one JS scope, and nothing frames an item. `#let list = 1` kills the document, and containment, style-height restore and splice spans have nothing to attach to (codegen.cc:226-228).
(3) Built-ins are privileged branches, not registry entries: `name === 'table'|'figure'` (executor.mjs:127-128), private builders, and four hook contracts.
(4) The IR validates nothing per kind and versions nothing per buffer. Keys are cast unchecked (ops.cc:68), values reach `(int)` casts, node-valued args dangle, and every knob became an exact-match version bump (ops.cc:100): five in four days.
(5) Instantiation is a raw recursive copy (model.cc:36-54). It has no budget, spans on shared DAG nodes are last-write-wins (ops.cc:179-185), and level well-formedness is patched wherever a feature hit it (codegen.cc:95-117, resolve.cc:469-492, emit.cc:170-172). The one container kind, group, silently serves three content shapes: region blocks, inline bibentry/term bodies, and the sidecar line column.
Features with no generic mechanism moved into the wrong layer: sidecars into api/ behind a magic role (doc.h:88-150), bibliography emission after the program (executor.mjs:238-266), and region styling by key-sniffing (executor.mjs:131-137).

## Abstractions

### Schema + schema-driven ops codec

**owner_layer**

IR contract at the L2/L3 boundary.
- Sources: engine/src/ops/schema.json and engine/src/ops/schema.lock.json (ids and since values, immutable).
- Generator: tools/gen-schema.mjs, which replaces gen-ops-ts. It writes engine/src/ops/schema.gen.h, engine/src/ops/schema.gen.cc and runtime/src/shared/schema.gen.mjs. These outputs are committed, so the C++ build needs no Node; CI checks they are fresh.
- Reader: engine/src/ops/ops.cc. Writer: runtime/src/shared/opbuf.mjs.

**purpose**

One source of truth for what a node may be and how it travels. A kind has a level class, a body content model, slots with their own models, typed per-kind attributes with value domains, defaults and write order, and an inline fallback. The schema also holds ctor parameter specs, decl types and ops. Each row carries the vocabulary version that introduced it. Everything is generated from it: the JS writer and binder, the C++ reader, typed accessors, the normalizer, the dumps, the document-model §2.1 table and the ctor manifest. The version byte advertises the vocabulary a buffer actually uses, not a global release.

**definition**

```
schema.json (excerpt; ids and since values are mirrored in schema.lock.json):
"kinds": {
  "heading":   {"id":2, "level":"block", "body":"inline", "attrs":["level","label"], "since":6},
  "group":     {"id":8, "level":"adaptive", "body":"position", "attrs":["role","label","name"], "since":6},
  "codeblock": {"id":6, "level":"block", "body":"code", "attrs":["lang","wrap","lineNo","hl","sidecar"], "slots":{"margin":{"model":"lines","since":10}}, "since":6},
  "collect":   {"id":13, "level":"block", "body":"data", "attrs":["what","form"], "since":6},
  "mathblock": {"id":14, "level":"block", "body":"none", "attrs":["src","label","name"], "inlineFallback":"mathinline", "since":6},
  "styled":    {"id":18, "level":"transparent", "attrs":["bits","font","lang","color","sizePx"], "since":6},
  "field":     {"id":28, "level":"inline", "body":"none", "attrs":["name","of"], "since":9},      // semantics owned by T3
  "softbreak": {"id":29, "level":"inline", "body":"none", "wire":"with T5", "downlevel":"text(join)"}
}
"attrs": {
  "heading.level":    {"key":1, "dom":["int",1,6], "def":1},
  "list.start":       {"key":3, "dom":["int",-1073741824,1073741824]},
  "codeblock.lang":   {"key":4, "dom":"token"},   // accepts ""
  "styled.lang":      {"key":4, "dom":"lang"},    // same wire key, separate per-kind spec, no shadowing
  "codeblock.lineNo": {"key":25, "dom":["int",0,1048576], "coerce":"boolAsInt"},
  "codeblock.hl":     {"key":26, "dom":"rangeset"},   // string on the wire, parsed once at decode
  "image.side":       {"key":30, "dom":["enum","left","right"], "alias":"float"},
  "raw.w":            {"key":18, "dom":["num",0,100000], "alias":"width"},
  "term.name":        {"key":9, "dom":"str"},
  "group.name":       {"key":9, "dom":"ident"},
  "note.name":        {"key":9, "dom":"str", "flags":["resolved"]},
  "mathblock.name":   {"key":9, "dom":"str", "flags":["resolved"]},
  "collect.form":     {"key":16, "dom":["enum","all"]},
  "ref.form":         {"key":16, "dom":["enum","number","name","full"]},
  "styled.bits":      {"key":20, "dom":["flags",{"EM":[2,6],"BOLD":[3,6],"UNDER":[16,6],"OVER":[17,6],"STRIKE":[18,6]}]},   // public mask; T4 adds members, each with its own since
  "*.label": {"key":0, "dom":"label", "since":{"heading|group|table|term|mathblock":6, "*":9}},
  "*.role":  {"key":6, "dom":"ident", "since":{"group":6, "*":9}},   // single-valued element identity (T3)
  "*.slot":  {"key":31, "dom":"ident", "since":9},
  "*.ext":   {"key":"EXT", "since":9}
  // "*.class" is reserved for T4: multi-valued style tokens with their own key
}
"decls": {"element":{"owner":"T3","since":9,"attrs":{}}, "rule":{"owner":"T4"}, "mathdef":{"owner":"T8"}}
"ops":   {"MAKE_TEXT":1, "MAKE_NODE":2, "EMIT":3, "STYLE_PUSH":4, "STYLE_POP_TO":5, "SPAN":6, "DIAG":[7,7], "AT":[8,8], "DECL":[9,9]}

Generated C++:
enum class Level : u8 { Block, Inline, Adaptive, Transparent, Trivia };
enum class Body : u8 { None, Inline, Blocks, Position, Items, Rows, Cells, Code, Lines, Data, Mixed, Text };
struct Member { const char* name; i64 value; u8 since; };
struct Domain { enum T : u8 { Bool, Int, Num, Str, Token, Ident, Label, Lang, Enum, Flags, RangeSet, StyleVal } t; i64 lo, hi; const Member* members; u8 nMembers; };
struct AttrSpec { ArgK key; const char* name; Domain dom; u16 flags; /* RESOLVED | INTERNAL | BOOL_AS_INT */ u8 since; ArgVal def; };
struct SlotSpec { const char* name; Body model; u8 since; };
struct KindInfo { const char* name; Level level; Body body; Kind inlineFallback; u8 since; Slice<AttrSpec> attrs; /* write order, universals expanded */ Slice<SlotSpec> slots; };
constexpr u16 KIND_COUNT = /* counted */;
extern const KindInfo kKinds[KIND_COUNT];
const AttrSpec* findAttr(Kind, ArgK);
i32 attrInt(const ContentNode*, A);
StrRef attrStr(const ContentNode*, A);
bool attrBool(const ContentNode*, A);
const RangeSet* attrRanges(const ContentNode*, A);
StrRef extAttr(const ContentNode*, StrRef name);
Values are validated at decode, so no consumer ever casts a raw double.

Generated JS: SCHEMA.kinds.heading = {id, level, body, attrs:[…], slots}, plus the attr specs, the since tables and RT_ABI.

Codec rules (amending document-model §4):
- Version byte = max(MIN_COMPAT, the largest since among the ops, kinds, (kind, attr) rows, enum/flag members, slot names and body variants the buffer uses). The writer tracks it while writing. MIN_COMPAT = 6 is today's version, and every current row has since 6, so every existing buffer stays byte-identical. Each landing that adds vocabulary takes the next number.
- The reader accepts MIN_COMPAT <= v <= OPS_VERSION.
- Inside an accepted buffer, an unknown kind, (kind, key) pair, enum member or slot makes that node error{ops-invalid} with an Error diag. An unknown op rejects the buffer, because its length is unknown. Unknown vocabulary is never silently dropped.
- Author-value faults on known keys fall back to the default with an 'ops-arg' warning: wrong tag, out of domain, NaN or ±inf, or a RESOLVED/INTERNAL attr on input. Integers are clamped and -0 becomes +0.
- ARG_NODE is rejected; node-valued data travels as slot children.
- `bits` is masked to the public flag members.
- A change to the meaning or encoding of an existing row bumps MIN_COMPAT and forces a full re-record. That covers level, body model, domain narrowing and wire encoding. Ids and since values are immutable, enforced against schema.lock.json.
- EXT attrs are encoded as `argKey=EXT nameStrRef argVal`, with a scalar value and a name matching [a-z][a-z0-9-]{0,31}. They are opaque to emit and layout. The resolver's specs, T7's display list and user Nodes read them through extAttr.
- New ops:
  - 0x07 DIAG sev code msg s e: warnings only; errors are error nodes.
  - 0x08 AT target s e → id: an occurrence alias.
  - 0x09 DECL type nargs (argKey argVal)* ntempl id*: a schedule op, typed by its decls row.
- STYLE_PUSH patches are validated with styled's AttrSpecs. That gives one delta schema with an unchanged wire format.
```

**surface**

Mostly invisible to authors.
- `$.std.schema.heading` for introspection.
- `ext: {chart: 'bar'}` on any ctor.
- `$.declare(type, name, attrs, ...templates)` writes a typed DECL record; T3's `$.element`, T4's rules and T8's `$.math.def` are built on it.
- `tsrc --stage=ops` prints attrs in schema write order, which equals today's order.

**replaces**

- engine/src/ops/ops.def:14-75 (flat KIND/ARGK lists, global key namespace, the lang overload at :74-75)
- engine/src/ops/ops.h:7 and :28 (hand-kept OPS_VERSION and KIND_COUNT)
- tools/gen-ops-ts.mjs:11-14 (version regex over a comment)
- runtime/src/shared/opbuf.mjs:46 (kind: 17 literal), :53-57 (throw on unknown key), :64 ('opId' in v → ARG_NODE)
- engine/src/ops/ops.cc:67-90 (unchecked ArgK cast at :68; ARG_NODE accepted), :100 (exact version equality), :132 (whole buffer rejected on an unknown kind)
- engine/src/resolve/resolve.cc:7-15 (findArg/argStr loops), :26-35 (isInlineKind)
- (int)a.num casts at engine/src/resolve/resolve.cc:133-135 and engine/src/emit/emit.cc:488, :512, :558, :656; the atoi hl parser at engine/src/emit/emit.cc:558-575
- engine/src/model/model.cc:17 (unmasked bits OR)
- docs/document-model.md:32-61 (hand-maintained kind table, which already disagrees with code for raw, tcell and term)

### Node values + constructor ABI

**owner_layer**

L2 Execution.
- runtime/src/shared/node.mjs: Node, toContent, plain.
- runtime/src/shared/stdlib.mjs: binder, defineCtor, derived ctors.
- Generated runtime/src/shared/ctors.gen.mjs.
- The `tsm:std` module specifier, for #use libraries.

**purpose**

Give every constructor one calling convention and one value-to-content coercion, built-in or user-defined, and give every public kind one public ctor. Content values are immutable branded Nodes that belong to no op buffer. That makes them unforgeable, and safe to build in library modules, across executions and across documents that run concurrently in one worker.

**definition**

```
class Node { kind; attrs; kids; #brand; #ser /* {buf, id} memo */ }
- Frozen. Node.is(x) is `#brand in x`, which cannot be forged. Freezing does not affect private fields.

Serialization:
- The std of execution E writes MAKE ops eagerly into E's OpBuf in construction order, which reproduces today's bytes.
- A Node not yet in E's buffer is serialized into E recursively on first reference, as a child or an EMIT, and memoized per buffer (#ser fast path, WeakMap fallback). Such Nodes come from buffer-free `tsm:std` ctors in a #use module, from another execution, or from before any execution.
- Spans and occurrence aliases live in E's side table, keyed by Node. A Node is never mutated.
- No ambient 'current buffer' exists, so concurrent executions need no AsyncContext: the referencing ctor decides the target buffer.

toContent(x) → Node[]:
- Node → [x]
- string | number | bigint → text
- null | undefined → [], plus a 'splice-undefined' warning when x is a splice result
- false → []
- Array or iterable → flatMap(toContent)
- obj[Symbol.for('tsm.content')] → recurse
- f with f[Symbol.for('tsm.nullary')] → recurse(f())
- thenable → error{content-thenable}, whose message says to await it
- any other function → error{splice-function}
- plain object → error{content-object}
- any other object → text(String(x)) plus an info diag
plain(content) → string: the plain-text projection (today's shadowText), public as $.std.plain and ctx.plain.

defineCtor(name, spec, impl)
  spec = {kind?, params: Param[], options: {name: Domain}, async?, nullary?, sealed?}
  Param = Attr(name, Domain) | Projected(name) | Text | Lines | Slot(name) | Body
  impl(call, ctx), where call = {attrs, slots: {name: Node[]}, kids: Node[], body?: Body}

The binder, bind(spec, args) → call:
1. Positional params bind left to right while the param accepts the arg. The first rejection ends positional binding.
   - Attr: scalars in its domain; null or undefined means absent; never a Node, an array or a plain object.
   - Projected: strings, numbers or Nodes, projected via plain() into a Str attr.
   - Text: a string, as one text kid.
   - Lines: a string (one text kid) or an Array (one seq per element; an element is a Node, a string or an array of runs). Lines are never flattened.
   - Body: a Body value.
2. The next arg, if it is a plain object (prototype Object or null, unbranded), is the options object. Universal options: label, role, slot, ext, and style (wraps the result in styled(delta)). Schema aliases also apply: float→side, width/height→w/h.
3. The remaining args become kids via toContent. Kids carrying `slot` go to call.slots. A ctor with a Body param called without a region receives its kids as the Body.
4. Attrs are validated against the schema; an unknown or out-of-domain attr gets a 'ctor-arg' warning (DIAG) and is dropped.

Specs reproduce today's signatures, so every committed call shape binds identically:
- heading [level: Int, label: Label]
- list [ordered: Bool, start: Int]
- codeblock [lang: Token, body: Lines]
- term [name: Projected]
- mathblock [src, label]
- link [url]; image [src]; ref [target]; raw [html]
- code [Text]; comment [Text]
- para, item, quote, group, table, row, cell, seq, note: no positional params
Consequences: heading('Intro') binds 'Intro' as a kid at level 1, and #list(false)[a] binds a as a kid.

Generated, one per public kind: para heading list item quote codeblock rule group table row(trow) cell(tcell) term collect mathblock mathinline error text styled link code ref raw seq image note field, plus node(kind, opts, ...kids).
Derived, written with the same defineCtor and listed in the manifest: strong, em, style, figure, toc, glossary and notes (all three NULLARY), bibliography (async), and sup/sub once T4 lands.
Sealed: text, seq, error, node.
Builders call the sealed base ctors through ctx.std.base and forward only explicit attr subsets: figure passes image src/alt/w/h/scale/side, never label or role.

Manifest:
- static: schema plus stdlib specs (names, params, options, level, Body), for tree-sitter and the .tsm printer;
- runtime: $.std.manifest() also includes ctors defined by the document or by #use modules, for editor completions.
```

**surface**

- #heading(2)[Intro]
- #list({ordered: true, start: 3})[- a]   (equivalent to #list(true, 3)[- a])
- #group({role: 'aside', label: 'a1'})[…]
- #table({cols: 2}, row(cell[a], cell[b]))
- #raw('<svg…/>', {w: 120, h: 40})
- #(rows.map(r => em(r)))   (arrays flatten)
- export const sig = Object.assign(() => em('— K.'), {[Symbol.for('tsm.nullary')]: true})   (a bare #sig then behaves like #toc)
- // lib.mjs
  import {group, para} from 'tsm:std'
  export const logo = raw('<svg…/>', {w: 24, h: 24})   (safe in any execution)

**replaces**

- runtime/src/worker/executor.mjs:59-62 (toShadow: auto-calls any function, duck-typed 'opId' in x, String() fallback)
- runtime/src/worker/executor.mjs:63-71 (private styled/node helpers, shadowText)
- runtime/src/worker/executor.mjs:76-121 (private tableBuild/regionJoin/figureBuild; figure forwards the whole args bag at :118)
- runtime/src/worker/executor.mjs:162-237 (hand-written ctor bag: heading :171-172, term :174-175, list :190-191, codeblock :196-206 incl. lineNo true→1, image :214-217 float→side, mathblock :219-220)
- runtime/src/worker/executor.mjs:58 (ctors are closures over one OpBuf)
- runtime/src/shared/opbuf.mjs:40-71 (mutable plain-object shadows) and :96-103 (span stored by mutating the shadow)

### Registry + frames

**owner_layer**

L2 Execution: runtime/src/shared/registry.mjs and runtime/src/shared/frame.mjs. Both are engine-agnostic and used by the interpreter in every host.

**purpose**

Constructors, regions, fences and formatters are entries in one registry, with one bound calling convention and `next` delegation. A region is simply a constructor that takes a Body. One frame implementation wraps every block at every depth and every hook call. It contains errors, enforces the documented style-height rules and stamps spans.

**definition**

```
class Registry { define(ns /* 'ctor' | 'fence' | 'format' */, name, factory: next => entryFn, opts); resolve(ns, name); trampoline(name) }
- define() captures the current entry as `next`. Registration runs in document order, so it is deterministic. Sealed ctors refuse.
- std exports are stable trampolines that resolve the entry at call time. Sugar, explicit `#name[…]`, hole code and fragments therefore all see an override from its registration point on.
- Overrides receive the bound call; the binder runs once, before the chain.

API:
- $.ctor(name, next => (call, ctx) => Content) → trampoline.
- $.region(name, (body, ctx) => Content, {ownsStyle}) is sugar for a ctor whose spec is params [Body]. It returns the trampoline, so `#let theorem = $.region('theorem', fn)` also makes `#theorem({label: 't2'})[…]` callable. ctx.next(body, {args}) delegates.
- $.fence(tag, (src, ctx) => Content).
- $.bib.format = (entry, ctx) => Content, in the 'format' namespace.

Region resolution: `#!name(args) … #name!` calls ctor `name` iff its spec declares a Body param. Otherwise the default region applies: group({role: name, label}, ...await body.blocks()). The legacy font/lang/color/sizePx header keys (document-model.md:83) are honoured there and nowhere else.
Fence resolution: fence:tag, otherwise the default fence (codeblock, below).
The built-ins (table, figure, default region, default fence, default bib format) are registered through the same define().

Body is lazy and memoized, and is evaluated inside the hook frame:
  {blocks(): Promise<Node[]>, rows({sep: '|'}): Promise<Node[][][]>, source: {text, offset, lines: [{text, start}]}}
- The interior runs at most once, in the first mode requested (blocks, or inline rows split at softbreak and sep). The other mode is derived from that result.
- An interior that is never read never runs (draft regions).

ctx = {args, label, span, lines /* T1 provenance */, source(), m /* tag, parse, parseMany */, error(msg, localOffset), next, std /* incl. std.base */, load /* T9 */, plain}

Frames (frame.mjs):
- Top-level content block: save h0, evaluate, EMIT. If the height differs from h0: 'style-in-value' warning. Then popTo(h0). On throw: popTo(h0) and EMIT error{script-error, name, message} with the block span.
- Top-level statement block (#let, #{…}, #use prologue): no pop on success. The documented `#{ push } … #{ popTo }` scope (document-model.md:83) is the one stated exception to v2 §12:251, amended explicitly. On throw: popTo(h0) and EMIT an error block.
- Nested frames (interior blocks of regions, items and quotes; hook and override calls; #for iterations): save h0 and run. If the height differs: 'style-in-value' plus popTo(h0), because a nested push could only restyle the enclosing top-level block retroactively. On throw: popTo(h0) and an error node in place.
- invoke() stays synchronous for sync results, so sync chains allocate no promises; a thenable is post-processed when it settles.
- Results pass through toContent. Span-less roots are stamped through the side table.
- A region `style:` arg becomes one styled wrapper around all roots, unless the entry declares ownsStyle.
- A depth guard (64) yields error{hook-recursion}.
- Errors exist only as error nodes, carrying a stable code and the JS error name. DIAG carries warnings that have no content node. DiagSink dedupes by (code, span).

Default fence (codeblock) when args.sidecar is set:
- split each line at the marker in JS;
- keep the code text as the single text body, so highlighting is unchanged (doc.h:152-155);
- parse all notes with one ctx.m.parseMany([{text, offset: lines[k].start + col}]);
- if any note exists, append group({slot: 'margin'}, ...one seq per line).
This is today's shape (doc.h:88-150, empty lines kept index-aligned), now with exact spans and with real notes and refs.
```

**surface**

- #{ $.region('table', async (body, ctx) => figure({label: ctx.label}, await ctx.next(body, {args: {cols: ctx.args.cols}}), para(ctx.args.caption))) }
- #{ $.ctor('strong', next => (call, ctx) => group({role: 'key-term'}, next(call, ctx))) }   (structural; presentation belongs to T4 rules)
- #{ $.region('plot', async (body, ctx) => raw(await render(ctx.args), {w: 400, h: 240})) }   (awaited and contained)

**replaces**

- runtime/src/worker/executor.mjs:122-139 (__region if-cascade and style-key sniffing; the same key list again at :228 and :279)
- runtime/src/worker/executor.mjs:141-161 (separate __fence dispatcher, own ctx, e.message errors)
- runtime/src/worker/executor.mjs:238-266 incl. :253-259 (bib post-program loop, formatter receives the raw ctor bag, failures become ⚠ text)
- runtime/src/worker/executor.mjs:267-273 ($.fence/$.region plain maps, no next)
- engine/src/codegen/codegen.cc:148 vs :158 (await __fence but a plain __region call)
- engine/src/api/doc.h:73 and :88-150 (extractSidecars: content rewrite in api/, plain-text bodies only)
- engine/src/emit/emit.cc:579-601 (role == 'sidecar-lines' string match; becomes a slot-id test)
- engine/src/render/semantic_html.cc:246 (skips every group child of a codeblock)

### Template lowering: one program, one interpreter

**owner_layer**

L1 Lowering: engine/src/codegen/codegen.cc plus sugar.def emit a LowerProgram and a hole module. tsr_parse_fragment / tsr_parse_fragment_many over inline/ emit the same program format. The interpreter is L2: runtime/src/shared/lower.mjs, shared by worker.mjs, node/render.mjs and tools/record-fixtures.mjs.

**purpose**

Give markup exactly one lowering and one executor. Static markup structure never becomes JS text. It becomes a program of bound ctor calls, and only user code becomes JS, as holes. The document, m`` templates, ctx.m.parse and sidecar notes therefore share one semantics, and generated code can never collide with user bindings. Every block at every depth gets a frame. Adding a ctor or sugar never touches a JS golden.

**definition**

```
tsr_compile(doc) → {program: Uint8Array, js: string}, exported through the C ABI (T9). Dumps: `tsrc --stage=lower` and `tsrc --stage=js`.

LowerProgram:
- Header: magic, RT_ABI, string table, and the block table [{kind: content | stmt | use, s, e, holeRange}].
- Body, binary and preorder:
  BLOCK k s e … END
  CALL ctorStrRef nAttrs (key val)* nKids s e
  TEXT strRef s e
  SOFTBREAK join s e
  HOLE i nKids s e
  NAME strRef s e        (fragments only: a bare #name resolved in scope)
  REGION nameStrRef (argsHole | CONST attrs) nInterior … s e
  FENCE tagStrRef (argsHole | CONST attrs) bodyStrRef lines s e
  STMT i
  IF i nBranches …
  FOR i …
  ERROR code msg s e     (T1 error nodes)
- Sugar rows (sugar.def: SUGAR(astSugarId, ctorName, ARGMAP)) are resolved at compile time into CALL ops with bound, schema-validated attrs. The interpreter therefore needs no sugar table and runs no binder for markup.
- Literal-only header args become CONST data, so they need no hole.
- Optional fast path: `#name[…]` or `#name(literals)[…]`, where name is a std ctor and not user-bound, compiles to CALL. The semantics are the same, because std names are trampolines.

Hole module: user code only. The generated identifiers are limited to __rt, $, __h and __k.
export const abi = 0x…;   // RT_ABI = hash(schema.json, sugar.def, program opcodes, runtime protocol); checked before default() runs
export default async (__rt, $) => {
  const {ref, em} = __rt.std;    // std names mentioned in holes and not user-bound (jslex identifier scan)
  let avg, n;                    // hoisted #let names: one document scope (v2 §2)
  const __h = [
    () => { avg = (a, b) => (a + b) / 2; },   // #let avg = …
    () => (avg(3, 5)),                        // #avg(3,5)
    (__k) => (f(...[a, ], ...__k)),           // #f(a,)[c]: structural content args
  ];
  await __rt.run(__h, 0, 7);     // the interpreter runs blocks 0..6
  function helper() {}           // a #{…} with top-level declarations stays verbatim at its flow position; 'stmt-uncontained' info
  await __rt.run(__h, 7, 12);
};

run(h, from, to):
- runs blocks in order, each inside a frame;
- CALL goes to the registry trampoline with the bound call;
- HOLE calls h[i](kids) and applies toContent;
- only thenables are awaited;
- spans come from the side table. In S5, hole results are left unspanned, exactly like today's val(). From S7, a fresh result gets SPAN and a reused value gets AT.

Statements:
- `#let x = e`: a hoisted name plus a statement hole. Destructuring patterns hoist every name; T1 supplies the pattern.
- `#{…}` with no top-level let/const/var/class/function (jslex keyword scan at depth 0): a contained statement hole. Otherwise it is emitted verbatim at its flow position, uncontained, with an info diag.
- `#use`: one prologue frame per import. On failure: an error block at the #use span, and the names stay undefined. Exported ctors, regions and fences auto-register through __rt.use(mod).
- `#if` / `#for` (S12): IF/FOR ops. Conditions and iterables are holes; bodies are subprograms, and #for gets a fresh hole table per iteration that closes over the loop variable.

Fragments:
- tsr_parse_fragment(doc, segments[], baseOffset, flags) and parseMany([{text, offset}]) return programs in the same format.
- Segment boundaries are HOLE atoms. No sentinel character is used, so a literal U+FFFC cannot collide.
- m`a ${v} *b*` = run(parse(strings), holes = values). m`` has no source base, so its spans are empty and inherit the enclosing occurrence span.
- ctx.m.parse(str, {offset, mode, scope}) resolves NAME in scope ∪ std. A `#f(args)` inside a runtime string becomes error{fragment-splice-js}; there is no eval.
- Hosts inject parse and parseMany into execute(): the worker, Node render, and record-fixtures (WASM in Node, or `tsrc --stage=fragment`).

SyntaxError isolation (only when import fails):
- The worker keeps the last-good set of hole hashes (T9). It stubs the holes not in that set first, which costs one extra import for a typical keystroke.
- Then it bisects the remaining holes, at most 4 levels deep.
- Then it reports one whole-document 'script-syntax' error, and the editor keeps the last good render.
- An unchanged hole module is never re-imported (cached by hash), so prose keystrokes import nothing.
```

**surface**

Authors see no change.
- `#let list = [1, 2]`, `#let toc = 3` and a repeated `#let x` are all legal. Only `$`, `__rt` and the `__` prefix are reserved.
- m`see ${fig} and *this*`
- ctx.m.parse(src, {offset})
- ctx.m.parseMany(notes)
- `tsrc --stage=lower` shows the program, and `--stage=js` shows the user code that will run.

**replaces**

- engine/src/codegen/codegen.cc:209-212 (31-name destructured prologue, i.e. 31 reserved words; churns all 48 JS goldens per new ctor)
- engine/src/codegen/codegen.cc:214-225 (verbatim top-level let and #{}), :226-228 (bare __emit, no frame)
- engine/src/codegen/codegen.cc:67-94, esp. :73-84 (content-arg text surgery on lastCallStart)
- engine/src/codegen/codegen.cc:95-117 (display-math peephole; becomes the schema inlineFallback)
- engine/src/codegen/codegen.cc:145-193 + runtime/src/worker/executor.mjs:94-113 (private __fence/__region encodings, nested-array rows, lossy ' | ' rejoin)
- engine/src/codegen/codegen.cc:198-200 (silent default text(""))
- engine/src/ast/ast.h:22 (lastCallStart byte offset)
- engine/src/inline/fragment.cc:9-113 (second AST→content lowering: drops comments, flattens notes, one outer span for all nodes)
- runtime/src/worker/executor.mjs:151 and :232-236 (cooked-text m)
- runtime/src/worker/executor.mjs:289-310 (a 127 KB module imported per edit for an 86 KB document)
- docs/architecture.md:120 and docs/document-model.md:93 (fragments as ops slices: amended); docs/design-decisions-v2.md §2 compilation model (amended: the program plus a user-code module replaces 'one JS function')

### Instantiation normal form

**owner_layer**

L3 Model: engine/src/model/model.cc (instantiate) and the new engine/src/model/normalize.cc. Both are driven by the generated KindInfo, and normalize.cc is a standalone API the resolver also calls.

**purpose**

Be the one place where the op DAG becomes a content tree every later stage can trust: per-occurrence spans that stay inside their paragraph, schema-checked content models that respect opaque and slot models, block/inline levels, a node budget, and typed declarations with anchors. The same normalize() also runs on subtrees the resolver synthesizes.

**definition**

```
struct InstLimits { u32 maxNodes; u16 maxDepth; };   // default max(1<<20, 64*raw.nodes.size()) nodes, depth 256; host config (T9)
struct Decl { DeclType type; std::vector<ArgVal> args; std::vector<ContentNode*> templates; u32 flowIndex; Span span; };
struct ContentTree { ContentNode* root; std::vector<Decl> decls; };
ContentTree instantiate(const RawOps&, Arena&, Interner&, StyleTable&, const InstLimits&, DiagSink&);
void normalize(ContentNode* n, Position pos /* Blocks | Inline | Opaque */, Arena&, DiagSink&);
  // Pure. Fused into copy() for executor output. The resolver must call it on every subtree it synthesizes (T3 contract).
  // Debug builds assert the normal form before emit ('emit-unnormal').

copy() works on an explicit stack:
- Exceeding the budget or depth replaces the subtree with error{ops-too-large}.
- Span containment: a node whose span is empty, or not inside its parent's span, takes the parent's span (document-model §1 l.26). An AT alias instantiates its target with the alias span at the root. Values spliced away from their definition therefore never move a paragraph's srcBase (typeset_html.cc:613-619).
- styled folds as today (I6).
- DECL templates are instantiated with an empty stack (style-neutral), for T4's use-site fold.
- A Decl keeps its flow anchor (the number of EMITs before it, plus its span) and appears once per $.declare call.

Normalization rules, in precedence order:
N0 Each EMIT root is normalized alone and never merged with another, so there is one pid per EMIT (emit.cc:900-905). An inline-level root becomes para{anon}.
N1 INLINE_FALLBACK comes first: a fallback kind in an Inline position becomes its fallback (mathblock→mathinline), with 'label-dropped' if it was labelled.
N2 A para whose only child is Block- or Adaptive-level (not para, not Trivia) becomes that child. An empty child span takes the para's span.
N3 At a Blocks position, a para whose kids are all empty text is removed (moved verbatim from resolve.cc:469-474).
N4 At a Blocks position, a maximal run of Inline-level kids within one container becomes para{anon}.
N5 A Block in an Inline position gets a 'block-in-inline' warning and stays where it is, which keeps today's rendering until S8b. With S8b, a para at a Blocks position splits into [para, block, para{cont}], and other inline contexts get an error node.
N6 Checked models (Items, Rows, Cells): an offending child is wrapped in error{content-model}. Opaque models (Code, Lines, Data, Mixed, Text) are never wrapped or pruned. Slot children are validated against the parent's SlotSpec and are exempt from its body model.
Level resolution: Transparent kinds (styled, seq) take their children's level; Adaptive kinds (group, term, error) take their position's level, and group's body model is that of its position; Trivia (comment) never counts.
Every error node yields exactly one DiagSink entry, deduped by (code, span) across per-occurrence copies.

Level and body model of every kind (B = Block, I = Inline, A = Adaptive, T = Transparent):
| kind | level | body |
|---|---|---|
| para | B | Inline |
| heading | B | Inline |
| list | B | Items |
| item | B | Blocks |
| quote | B | Blocks |
| codeblock | B | Code, plus slot margin: Lines |
| rule | B | None |
| group | A | Position |
| table | B | Rows |
| trow | row | Cells |
| tcell | cell | Inline (today's flattening, emit.cc:671-674) |
| term | A | Mixed |
| collect | B | Data |
| mathblock | B | None (fallback mathinline) |
| error | A | Mixed |
| comment | Trivia | Text |
| text | I | — |
| styled | T | — |
| link | I | Inline |
| code | I | Text |
| ref | I | None |
| mathinline | I | None |
| raw | B | None |
| hardbreak | I | None |
| seq | T | — |
| image | B | None |
| note | I | Mixed |
| field | I | None |
| softbreak | I | None |
raw is Block because emit handles it only as a block (emit.cc:685). resolve.cc:30's inline listing is the latent bug that drops a mid-paragraph raw.

Verified: replaying N0–N6 with this table over the .ops of all 48 fixtures (scratchpad/t2r/norm.py) fires only today's 7 unwraps and 2 empty-para removals. There are no inline-run wraps and no block-in-inline cases. The bibentry groups sit under collect (Data, untouched).
```

**surface**

No new syntax; the visible changes are behavioural:
- `See #toc() here.` produces a 'block-in-inline' warning, and with S8b it splits the paragraph;
- a fence handler that returns em('x') renders inline content as a paragraph instead of an empty pid;
- `#style({..})[#codeblock(..)]` stays a code grid;
- a value spliced twice anchors to each splice site, inside its paragraph.

**replaces**

- engine/src/model/model.cc:36-54 (recursive copy, no budget or depth limit)
- engine/src/ops/ops.cc:179-185 + runtime/src/shared/opbuf.mjs:96-103 (node-level SPAN, last write wins)
- engine/src/resolve/resolve.cc:26-35 and :469-492 (isInlineKind, emptyPara, one-way unwrap that drops the splice span)
- engine/src/codegen/codegen.cc:95-117 (mathblock promotion and silent inline degrade)
- engine/src/api/doc.h:73 (feature-specific ingest rewrite slot)

## Subsumption (finding → mechanism)

- **subsumed** by *Node values + constructor ABI (region builders through Registry + frames)*: `markup-language/closed-constructor-set`, `markup-language/ctor-signature-vs-content-args`, `codegen-ops-model/ctor-signatures-break-sugar-equivalence`, `codegen-ops-model/private-region-builders-and-missing-ctors`, `resolver/missed:0`
  - One public ctor per public kind is generated, including group, table, row, cell, raw, error, collect, field and node. figure is a derived ctor with a spec, and it appears in the manifest.
  - The binder generalizes today's positional signatures instead of replacing them. Typed param kinds (Attr, Projected, Text, Lines, Slot, Body) keep codeblock(lang, lines, opts), term(Node, …) and heading(1, 'label', …) binding exactly as now, while `#heading(2)[T]` and `#list(false)[a]` no longer misbind.
  - User ctors (defineCtor) and user regions (ctors with a Body param) are callable and listed in the runtime manifest.
  - hardbreak has no ctor until T6 implements forced breaks (see not_generalized).
  - Equivalence is asserted at --stage=tree modulo spans, including after one override.
- **subsumed** by *Node values + constructor ABI (toContent, NULLARY/CONTENT symbols, branded immutable Nodes)*: `markup-language/value-coercion`, `codegen-ops-model/val-coercion-adhoc`, `codegen-ops-model/missed:4`
  - toc, glossary and notes carry the public NULLARY symbol, so bare #toc keeps working and users can mark their own nullary ctors.
  - Any other function, a plain object or a thenable becomes a coded error node instead of a silent call or a debug string.
  - undefined renders nothing, plus a 'splice-undefined' warning; this churns splice/dot-rule, as called out in S4.
  - toContent also applies to fence, region and formatter results.
  - m keeps content values: an interim seq in S4, and the real program in S10.
- **subsumed** by *Registry + frames*: `markup-language/region-builtin-privilege`, `parser-frontend/region-fence-private-dispatch`, `codegen-ops-model/region-meta-args-hijack`, `codegen-ops-model/missed:2`, `real-world-evidence/sugar-dispatch-fixed`
  - table, figure, the default region, the default fence and the bib formatter are define() entries, and overrides delegate through next.
  - A region is a ctor that declares a Body param, so the 'regionable' flag and the region/ctor namespace split are gone. Region syntax and the explicit call are the same call.
  - Regions, fences, formatters and overrides share one frame: awaited where needed, contained, height-checked.
  - Legacy style keys are honoured only by the default region, so `#!code(lang: …)` with a user handler is not hijacked. `style:` is an explicit option, and an entry opts out with ownsStyle.
  - Trampolines make $.ctor the sugar-dispatch table for sugar, explicit calls and fragments alike.
  - Presentation-only show rules belong to T4.
- **subsumed** by *Template lowering: one program, one interpreter*: `codegen-ops-model/missed:0`, `parser-frontend/missed:3`
  - Hygiene holds by construction: markup lowers to program ops, not JS, so generated calls never live in the user's scope. The hole module declares only __rt, $, __h and __k, the hoisted #let names, and the std trampolines that holes actually mention.
  - Content args are desugared structurally (`f(...[inner], ...__k)`) from T1's argument-list span, so trailing commas, comments and spreads stay legal.
- **subsumed** by *Registry + frames (frames at every block depth), with Template lowering supplying the block table*: `markup-language/missed:3`, `real-world-evidence/missed:0`
  - Content blocks and nested frames pop to their entry height at exit, with a 'style-in-value' warning, as v2 §12:251 and document-model.md:87 require. A splice push therefore no longer leaks past its block.
  - Top-level statement blocks are the explicitly amended exception, because document-model.md:83 documents cross-block `#{ push } … #{ popTo }` scopes.
  - Every throw becomes an error block or node with a span. #use failures are contained in prologue frames. SyntaxErrors are isolated incrementally.
  - T4 owns the scoped-cascade semantics; T2 owns the mechanism.
- **subsumed** by *Registry Body protocol + Template lowering (codegen half); parser half owned by T1; caption slot owned by T3*: `codegen-ops-model/parse-time-pipe-segmentation`, `markup-language/missed:0`
  - Region interiors are ordinary block subprograms inside a lazy Body. Display math in #!figure is therefore a labelled mathblock, and the normal form applies.
  - body.rows() splits at softbreak and at sep-flagged '|' text (JS-only flags from T1), so non-table regions get lossless text with real spans.
  - The 'first para = caption' convention is replaced by T3's caption slot.
- **subsumed** by *Template lowering: one program, one interpreter*: `parser-frontend/fragment-parallel-lowering`, `codegen-ops-model/duplicate-lowering-fragment`, `markup-language/sidecar-private-lowering`, `real-world-evidence/markup-reentry-missing`
  - There is now one interpreter rather than two engines sharing a table: documents, m`` templates, ctx.m.parse(Many) and sidecar notes all run LowerPrograms against the same registry. fragment.cc is deleted.
  - Fragments resolve bare names in an explicit scope and reject JS-argument splices instead of using eval.
  - Holes are segment boundaries, so no sentinel character is needed.
  - Documentation examples need no re-entry: body.source and ctx.source() supply the text.
- **subsumed** by *Registry default fence + schema SlotSpec(codeblock.margin: Lines)*: `codegen-ops-model/sidecar-ingest-pass`, `api-measure-code/sidecar-api-layer-rewrite`, `emitter/sidecar-role-string`
  - The default fence splits sidecars in JS. It keeps the code as the single text body, so highlighting is unchanged, and adds group{slot:'margin'} with one seq per line, kept index-aligned.
  - The Lines slot model is opaque, so the normalizer never merges or prunes those lines.
  - Offsets come from T1's per-line provenance, so they are exact in nested and CRLF fences.
  - emit and semantic_html test the generated slot id instead of the role string.
  - The api/ pass and fragment.cc are deleted.
- **subsumed** by *Schema + schema-driven ops codec*: `codegen-ops-model/global-argk-namespace`, `codegen-ops-model/version-bump-per-vocabulary`, `real-world-evidence/open-arg-schema`, `resolver/argk-overloading`, `codegen-ops-model/missed:3`, `real-world-evidence/missed:3`
  - Per-kind AttrSpecs carry domains, defaults and write order. The `form` and `name` overloads become separate per-kind specs on one key, and RESOLVED specs (note.name, mathblock.name) are rejected on input.
  - The version byte is max(MIN_COMPAT = 6, since of the vocabulary used). since covers enum and flag members, slots and body variants too. Unknown vocabulary inside an accepted buffer is an error, never a drop. Meaning changes (level, content model) are honest MIN_COMPAT bumps rather than 'data changes'.
  - The open channel is EXT, carrying validated identifier names and scalar values.
  - The reader range-checks keys, canonicalises numbers and masks bits to the public flag members.
- **subsumed** by *Instantiation normal form*: `markup-language/block-inline-placement`, `codegen-ops-model/block-promotion-peepholes`, `resolver/rewrite-normalizations`, `codegen-ops-model/missed:1`
  - One table of level classes (Block, Inline, Adaptive, Transparent, Trivia) and body models, including opaque and slot models, replaces the codegen peephole, the resolver lists and the emit drops.
  - normalize() is standalone, so resolver-synthesized subtrees go through it too.
  - Rule precedence is fixed: fallback, then unwrap, then empty-para removal, then wrap, then the block-in-inline policy. Roots are never merged.
  - The split policy is gated on T6/T7 (S8b). Until then block-in-inline is diagnosed and rendered as today.
  - A replay over all fixtures shows the change is golden-neutral.
- **subsumed** by *Schema (one delta schema: STYLE_PUSH patches validated with styled's AttrSpecs) + frames (height rules); property set owned by T4*: `codegen-ops-model/two-style-encodings-and-stack`
  - styled stays the only node-level delta; the universal STYLE_DELTA attr group is dropped.
  - The STYLE_PUSH/POP_TO schedule stays, because v2 §12 commits to it.
  - Raw-number $.style.push is removed and the reader masks bits.
  - The wire is unchanged, so no MIN_COMPAT bump is needed.
- **subsumed** by *Node values + constructor ABI (async ctors) + Registry ('format' entry) + Template lowering (awaits thenables)*: `codegen-ops-model/bibliography-placeholder-and-end-emission`
  - #bibliography(src) is an async derived ctor that returns its collect node at the splice. This removes the placeholder text(''), the end-of-document emission and the reason emptyPara exists.
  - It depends on T9's ctx.load (with caching) and on T3 expanding collectors after all refs.
- **owned-by-other-theme** by *T3-semantics*: `codegen-ops-model/role-string-dispatch`, `codegen-ops-model/note-kind-and-lift`, `codegen-ops-model/collector-switch-and-fixed-counters`
  T3 owns element classes, counters, collectors, inserts and entries.
  T2 supplies:
  - the universal single-valued `role` and `label` and the `slot` and EXT attrs;
  - the typed DECL op with rows contributed by T3 (element attr schemas included), flow anchors and style-neutral templates;
  - the `field` placeholder kind;
  - Adaptive group/term and the opaque Data/Mixed models that preserve today's bibentry and term shapes until T3 replaces them;
  - the normalize() contract;
  - removal of the engine-internal role handoff it owns ('sidecar-lines' becomes the margin slot).
  The note kind stays until T3's insert. Thanks to per-buffer versioning, that replacement is additive.
- **owned-by-other-theme** by *T4-style-settings*: `codegen-ops-model/cls-sup-feature-bit`, `codegen-ops-model/kind-default-styles-in-emit`, `codegen-ops-model/resolver-fabricated-styles`, `codegen-ops-model/token-class-as-color`, `codegen-ops-model/fixed-styling-fields`
  T4 owns the style property registry, the `class` channel (its own multi-valued attr row), default kind/role rules as DECL{rule} rows, and baselineShift.
  T2 supplies:
  - styled as the only node-level delta, with STYLE_PUSH validated by the same schema;
  - the StyleVal domain hook at decode;
  - public flag members, each with its own since, for the bits mask;
  - frames that bound pushes;
  - the stdlib define() through which T4 adds sup, sub and style.
- **owned-by-other-theme** by *T1-surface-frontend*: `parser-frontend/per-feature-ast-kinds`
  Assumes T1 delivers a typed ast.def with generated sugar ids, typed payloads, a generic dump and -Werror=switch. T2's sugar.def maps each sugar id to a ctor name and argument map, which codegen resolves into bound CALL ops. A new sugar costs one AST row plus one SUGAR row, and no JS golden changes.
- **owned-by-other-theme** by *T4-style-settings*: `codegen-ops-model/css-injection-style-values`, `codegen-ops-model/sizepx-ignored-in-emit`, `codegen-ops-model/metric-key-fragmentation`, `codegen-ops-model/tree-dump-omits-sup`
  Not in T2's required list. Recorded here because a critic flagged them as undisposed.
  - T2's part is the StyleVal domain hook in the S1 reader: T4's canonicaliser is called at decode, which closes CSS injection for both styled and STYLE_PUSH.
  - sizePx handling in emit, metric keys and the dump's bit names belong to T4.

## User extension examples

### A numbered, referenceable theorem environment

**Today**

`#!theorem(label:'t1') … #theorem!` becomes group{role:'theorem'}. @t1 renders '??' because only role 'figure' registers labels (resolve.cc:166-170).
A $.region handler cannot help:
- there is no group ctor;
- an async handler renders '[object Promise]';
- a throwing handler kills the document;
- there is no way to show the resolved number without reading resolved values (v2 §11.1:238).

**After**

`#let theorem = $.element('theorem', {counter: 'thm', within: 'heading@1', supplement: 'Theorem', attrs: {name: 'str'}})`
This is T3's sugar over $.declare plus defineCtor, so the decl and the ctor are registered atomically. `#!theorem(label: 't1', name: 'Zorn') … #theorem!` and `#theorem({label: 't2'})[…]` are the same call, and @t1 gives 'Theorem 2.1'.
Custom presentation is a contained, awaited handler that requests resolved values declaratively:
`$.region('theorem', async (body, ctx) => group({role: 'theorem', label: ctx.label}, para(strong(field({name: 'supplement'}), ' ', field({name: 'number'}), '. ')), ...await body.blocks()))`

### Wrapping a built-in (a table that is always a captioned figure)

**Today**

$.region('table', fn) replaces tableBuild outright (executor.mjs:124-128). The default builder is a private closure, so it cannot be called, and rows arrive as codegen's nested-array encoding.

**After**

`$.region('table', async (body, ctx) => figure({label: ctx.label}, await ctx.next(body, {args: {cols: ctx.args.cols, align: ctx.args.align}}), para(ctx.args.caption)))`
The inner table receives an explicit attr subset, so the label is not registered twice.
The same table is buildable from code: `#table({cols: 2}, row(cell[姓名], cell[年龄]))`.

### Rebinding markup sugar structurally for one document

**Today**

`#let strong = (...k) => em(...k)` is a module SyntaxError that aborts the whole document (codegen.cc:209-212). `#{ strong = … }` silently reassigns the destructured parameter, an accidental show rule that misses m-fragments.

**After**

`#{ $.ctor('strong', next => (call, ctx) => group({role: 'key-term'}, next(call, ctx))) }`
From that point on, every `*…*`, every `#strong[…]` and every m-fragment yields an inline key-term group that T3 collectors can index and T4 rules can style. Colour belongs in a T4 rule, not in the override.
`#let strong = 1` is just a local: the sugar is unaffected, and an explicit `#strong[…]` then calls the user's binding.

### A user annotation fence whose margin notes contain markup

**Today**

Only the built-in `sidecar:` arg works. It is an api/ post-pass that handles plain-text bodies only (doc.h:88-150). ctx.m returns cooked text (executor.mjs:151), so a user fence has to forge group{role:'sidecar-lines'}, which emit matches by string (emit.cc:587-588).

**After**

`$.fence('annot', (src, ctx) => { const {code, notes} = splitNotes(ctx.lines, '//!'); const parsed = ctx.m.parseMany(notes.map(n => ({text: n.text, offset: n.start}))); return codeblock(ctx.args.lang ?? 'js', code, group({slot: 'margin'}, ...parsed.map(p => seq(...p)))); })`
- The text body keeps syntax highlighting.
- Notes are real footnotes, refs and math.
- Spans are exact, even in indented or CRLF fences.
- There is one parse crossing, and the path is the one the built-in sidecar uses.

### A data-driven block with custom parameters and async data (#!plot)

**Today**

An async region handler renders '[object Promise]'. A new parameter that must reach the renderer needs a new ARGK, which is an OPS_VERSION bump plus a full fixture re-record (opbuf.mjs:56-57). The blog lists #!plot as something the engine must ship (vscode-tsm.tsm:65).

**After**

`$.region('plot', async (body, ctx) => raw(svgFor(JSON.parse(await ctx.load(ctx.args.data))), {w: 400, h: 240, ext: {chart: ctx.args.kind}}))`
- The handler is awaited and contained.
- `ext.chart` travels string-keyed with a validated name and needs no ops bump; only buffers that use EXT advertise v9.
- T7 carries it into the display list and into semantic HTML as data-chart.
- emit and layout never interpret it.

### A documentation example rendered from one copy of its source

**Today**

The blog's docs pages write every sample twice, once as a ```tsm listing and once live (zball-io src/docs/figures.tsm:23-33), because handlers receive neither the region source nor markup re-entry.

**After**

`$.region('example', async (body, ctx) => seq(codeblock('tsm', body.source.text), ...await body.blocks()))`
The interior runs once, inside the handler's frame. Generated markup, such as a converter fence, uses ctx.m.parse instead.

### Content built from data, with arrays, a nullary helper and a typo

**Today**

`#(rows.map(r => em(r)))` renders 'a,[object Object]' (executor.mjs:61), and there is no table ctor. A bare `#sig` silently calls any function. `#x.here` renders the text 'undefined' (test/golden/splice/dot-rule.tree.txt:6).

**After**

- Arrays flatten, and `#table({cols: 2}, ...rows.map(r => row(cell(r.name), cell(String(r.n)))))` works.
- A helper marked with Symbol.for('tsm.nullary') is called on a bare splice, like #toc; any other function produces a 'splice-function' error node.
- `#x.here` renders nothing and raises a 'splice-undefined' warning at its span.

### A reusable #use library of callouts shared across documents

**Today**

No module ABI exists: ctors are closures over one execution's OpBuf (executor.mjs:58), and `#use` compiles to invalid JS (keyword-forms-uncompiled). The worker keeps several documents alive (worker.mjs:14), so any module-level capture of a ctor would write into a stale buffer or another document's buffer.

**After**

```
// callouts.mjs
import {group, para, strong} from 'tsm:std';
export const regions = {
  warning: async (body, ctx) => group({role: 'warning', label: ctx.label}, para(strong('Warning. ')), ...await body.blocks()),
};
```
`#use("./callouts.mjs")` registers the region in a contained prologue frame. Nodes the module builds belong to no buffer, so it is safe across executions and documents. A failing import produces an error block at the #use line instead of a dead document.

## Invariants

- **I1 su fixed-point determinism; byte-exact goldens**
  - Programs run strictly in block order. The interpreter awaits only thenables and never uses Promise.all, and registration happens in program order.
  - The version byte is a pure function of the vocabulary used. Normalization and InstLimits are pure. Attrs are written in the schema's per-kind order, which equals today's order (group: role, label, name; heading: level, label; …).
  - Recorded fixtures do not depend on the Node version: error nodes carry stable codes and JS error names, and tools/record-fixtures normalises V8-originated messages.
  - The fixture-wide normalizer replay finds no new rule firing.
  - Golden churn is called out per step:
    - S4: splice/dot-rule;
    - S5: JS goldens replaced once, .ops byte-identical;
    - S7: spans;
    - S8: region fixtures, plus math .ops;
    - S10: sidecar fixtures;
    - S11: cite .ops.
  - Every step is gated on `record-fixtures --check`, and CI asserts zero ops-arg/ops-invalid diags when decoding the committed fixtures.
- **I2 Measurement–render robustness contract (v2 §7)**
  No layout logic changes. The StyleVal domain hook calls T4's canonicaliser at decode, so unmeasured CSS cannot reach runs. Block-in-inline keeps today's rendering until T6/T7 honour para{cont} (S8b). anon/cont paragraphs are ordinary measured paragraphs, with nothing repaired at runtime.
- **I3 Ops contract: reader is a fuzz target; OPS_VERSION skew detection**
  The reader gets stronger:
  - per-(kind, key) validation with domains;
  - key range checks;
  - unknown vocabulary inside an accepted buffer becomes error nodes, as document-model.md:118 always specified; an unknown op still rejects the buffer;
  - validated EXT names;
  - an instantiation budget;
  - new fuzz targets for DIAG, AT, DECL and EXT.
  I argue for changing the exact-equality rule (architecture.md:127, ops.def:3). The byte now states the vocabulary the buffer needs, and the reader accepts MIN_COMPAT..OPS_VERSION. Skew detection is kept for every harmful case:
  - vocabulary ahead of the reader → rejected;
  - a meaning or encoding change → MIN_COMPAT bump;
  - program/runtime skew → the static `abi` export and the program header hash.
  Id and since immutability is enforced against schema.lock.json, and v2–v5 buffers are not accepted.
- **I4 Execution declares, resolver decides**
  Hooks and overrides run only during execution, and ctx carries no resolved values. `field` is opaque placeholder content that the resolver fills, which is how v2 §11.1:238 describes $.ref. User registries travel as typed DECL records with flow anchors for the resolver's phase 0. The resolver stays a pure pass over (tree, decls, config), and calls normalize() on what it synthesizes.
- **I5 Dual-target rule**
  The interpreter, registry and frames are host-agnostic JS in runtime/src/shared. Parse, parseMany, source and load are injected by worker.mjs, node/render.mjs and tools/record-fixtures.mjs. tsr_parse_fragment is engine-core code behind a thin api/ export. extractSidecars leaves api/, which removes an existing breach of architecture.md:39-44.
- **I6 Emission-time style binding with the DAG/schedule encoding**
  STYLE_PUSH/POP_TO and the per-EMIT copy are unchanged, and styled is the only node-level delta. Nested frames are height-neutral, so no push can restyle an enclosing block retroactively. Top-level content blocks pop after their EMIT (v2 §12:251), and statement blocks are the documented exception. DECL templates are instantiated style-neutral, so their styles bind where they are used. AT occurrences are copied per emission like any value.
- **I7 Block-granular containment**
  Containment is implemented, not merely claimed:
  - frames at every block depth, for content, statement and #use blocks;
  - hook and override frames with a recursion guard;
  - incremental SyntaxError isolation;
  - codec and normalizer error nodes.
  All errors are error nodes, reported once through DiagSink and deduped by (code, span).
- **I8 Resumable pull-loop; atomic per-paragraph swaps**
  No new engine states. Fragment parsing is synchronous on the same handle before ingest. N0 never merges EMIT roots, so there is still one pid per top-level block. Span containment keeps every span of a paragraph inside that paragraph's source range, so untouched paragraphs stay byte-identical across edits (editor-design.md:54; typeset_html.cc:613-619).
- **I9 Performance (hot path; editor fast path)**
  - The markup program arrives as one binary buffer (one crossing). Sugar CALLs carry pre-bound attrs, so they skip the JS binder. The interpreter awaits only thenables, and sync hook chains allocate no promises.
  - The hole module holds only user code: about 95 tiny holes for pbr Triangle_Meshes against a 127 KB module today. It is cached by hash, so prose keystrokes import nothing.
  - Measured with scratchpad/t2r/imp.mjs: a 0.8 KB module imports in 0.34 ms, an 11 KB module in 0.50 ms, and a 113 KB module in 3.5 ms. SyntaxError isolation costs about one extra small import per keystroke.
  - The reader does O(1) table lookups per arg, and normalization is fused into copy().
  - Budget: no more than +1 ms compile+execute+ingest at 87K. It is gated in S5, S6 and S7 on new tools/bench-edit.mjs variants: splice-, region- and #let-heavy documents, plus a typing session inside a splice that produces syntax errors.

## Interfaces

- **T1-surface-frontend** (consumes)
  A typed AST from ast.def, containing:
  - Call nodes with a sugar id, typed payloads, and the callee span plus the argument-list span;
  - content args;
  - SoftBreak nodes carrying T1's join text;
  - `|` inside region interiors as sep-flagged text;
  - region and fence headers as object-literal spans with a literal-only flag and the parsed literal values;
  - per-line provenance [{text, start}] for fence and region bodies and for multi-line content args;
  - #let binding patterns, i.e. the bound names;
  - Stmt nodes (#let, #{}, #if, #for, #use);
  - Error nodes;
  - universal trailing labels.
  Also parseContent(segments, mode), in which segment boundaries are holes.
- **T1-surface-frontend** (provides)
  - The static ctor manifest (names, params, options, level, Body) for tree-sitter builtins, completions and the .tsm printer, and the runtime manifest via $.std.manifest().
  - The guarantee that lower.mjs is the only executor of lowering semantics, with both the document and fragments going through LowerProgram.
  - `tsrc --stage=lower` dumps.
- **T3-semantics** (provides)
  - Universal single-valued `role`, plus `label`, `slot` and EXT, on every kind.
  - The DECL op with rows contributed by T3 (element with its attr schema, counter, collector, insert, entry), flow anchors and style-neutral templates.
  - The `field` placeholder kind (Inline, None).
  - A RESOLVED attr flag.
  - Adaptive group/term and opaque Data/Mixed models that keep today's shapes until T3's replacements land.
  - normalize(subtree, pos) plus a debug assertion: T3 must normalize every synthesized subtree.
  - group, figure, table and collect ctors, and the default region group{role: name}.
  - defineCtor plus $.declare, so $.element can register decl and ctor atomically.
  - Async ctors.
- **T3-semantics** (consumes)
  - Semantics for element classes, counters, collectors, insert flows (which replace note) and entry tables (which replace bibentry groups).
  - Caption SlotSpecs.
  - `field` semantics, including the default `of` (the nearest enclosing labelled element).
  - Collector expansion after all refs, so an in-place #bibliography sees the complete citation order.
- **T4-style-settings** (consumes)
  - The style property registry, used as the StyleVal domain validator and canonicaliser at decode.
  - Public flag members, each with its own since, for the bits mask.
  - The `class` attr row: multi-valued style tokens with their own wire key.
  - DECL{rule} rows and how anchors scope them.
  - An InstLimits setting, if author-visible.
  - Ownership of scoped-cascade semantics; the statement-frame exception is documented jointly.
- **T4-style-settings** (provides)
  - styled as the single node-level delta.
  - STYLE_PUSH patches validated by the same AttrSpecs, with the wire unchanged.
  - Frame height rules: content and nested frames pop at exit with 'style-in-value'; statements pop only on error.
  - The `style:` ctor option, and the legacy region aliases confined to the default region.
  - define() entries for sup, sub and style.
  - Documented guidance that $.ctor overrides are structural while presentation belongs to T4 rules.
- **T5-text-shaping** (provides)
  - A softbreak kind row. It is JS-only, serialized as today's join text, until T5's emit handles it; it then gets its own since.
  - An `overlay` SlotSpec on codeblock when T5/T9 need inline content over highlighted text (literate fragment refs).
  - T5's InlineObject protocol changes raw/image from Block to Inline-capable. That is a level change and therefore a MIN_COMPAT bump with a full re-record, not a data-only change.
- **T6-layout-pagination** (provides)
  - The codeblock margin slot (Lines model, index-aligned) for the composite code+margin layouter.
  - KindInfo level and body for the BlockLayouter registry.
  - para{cont} and para{anon}; the S8b split is gated on T6 honouring cont.
  - Structured string-encoded domains (rangeset, and T6's own tracks/spans domains) that the reader decodes once.
  - Any change of the tcell body model from Inline to Blocks is a MIN_COMPAT bump.
- **T7-render-runtime** (provides)
  - EXT attrs through extAttr, as an opaque handle that emit and layout carry into the display list for every backend.
  - role, for the role→element map.
  - Span containment and AT occurrence spans, so data-s/e stay paragraph-local.
  - Error nodes that are already mirrored in diagnostics.
  T2 consumes T7's copy policy for synthesized nodes that inherit real spans.
- **T8-math** (provides)
  - DECL rows for math macros, symbols and operators, with flow anchors, so a redefinition applies from its position on.
  - The mathblock INLINE_FALLBACK row, i.e. the mid-paragraph display policy. It currently preserves the documented degrade-to-inline.
- **T9-host-protocol** (consumes)
  - ctx.load / $.load as the JS face of T9's ResourceLocator, with editor caching and the failure policy. Bibliography data moves to it from the engine-side resource list, while formatting stays in JS.
  - #use specifier resolution.
  - The C ABI: tsr_compile → {program, js}, tsr_parse_fragment and tsr_parse_fragment_many, plus the `--stage=lower|js|fragment` dumps.
  - One handshake reporting the abi export, the program hash and the ops version window.
  - The worker's last-good hole-hash set and its hole-module cache.
  - InstLimits config.

## Migration

### S1 Schema extraction and reader validation  → plan P0-06

- Add schema.json and schema.lock.json (today's ids; every row since 6; per-kind attr write order equal to today's ctor order) and gen-schema.mjs, with the generated outputs committed. Generate KIND_COUNT; opbuf uses KIND.text.
- Reader:
  - range-check ArgK;
  - validate per (kind, key) against domains, so TOKEN accepts "" and group.name is listed;
  - canonicalise numbers;
  - reject ARG_NODE;
  - turn an unknown kind into an error node;
  - mask bits to the public members (EM, BOLD, UNDER, OVER, STRIKE);
  - validate STYLE_PUSH patches with styled's specs;
  - add the StyleVal hook as a no-op until T4.
- Typed accessors replace the findArg loops and every (int) cast (incl. emit.cc:656), and the reader parses hl as a rangeset.
- Generate argName/kindName and the document-model §2.1 table; build with -Werror=switch.

**Golden impact:** None: fixtures are valid by construction, and CI asserts zero ops-arg/ops-invalid diags on all 48 fixtures.

**OPS bump (as designed):** False

**Fixes:** `codegen-ops-model/missed:3`, `codegen-ops-model/argtag-node-dangling`, `codegen-ops-model/contract-doc-drift`, `codegen-ops-model/global-argk-namespace`, `real-world-evidence/missed:3`, `resolver/argk-overloading`, `codegen-ops-model/two-style-encodings-and-stack`

### S2 Per-buffer vocabulary versioning  → plan P1-01

- Writer: version = max(MIN_COMPAT = 6, since of every op, kind, (kind, attr) row, member, slot and body variant used).
- Reader: accept 6..OPS_VERSION; anything above the buffer's version is malformed.
- gen-schema refuses edits to ids or since values in the lock file.
- Documented: each later landing takes the next version, and meaning changes bump MIN_COMPAT.
- Amend architecture.md:127 and ops.def:3.

**Golden impact:** None: every existing buffer still encodes version 6, byte-identical.

**OPS bump (as designed):** False

**Fixes:** `codegen-ops-model/version-bump-per-vocabulary`, `real-world-evidence/open-arg-schema`

### S3 Instantiation hardening and normalizer relocation  → plan P0-07

- copy() moves to an explicit stack with InstLimits.
- normalize.cc becomes a standalone API holding N0–N3 (today's unwrap and emptyPara, moved from resolve.cc:469-492). It is driven by KindInfo levels (raw Block, group/term Adaptive, comment Trivia).
- isInlineKind is removed.

**Golden impact:** None. The replay (scratchpad/t2r/norm.py) fires exactly today's 7 unwraps and 2 empty-para removals; the only kind whose level changes, raw, never appears alone in a para in any fixture.

**OPS bump (as designed):** False

**Fixes:** `codegen-ops-model/exponential-instantiation`, `resolver/rewrite-normalizations`

### S4 Node values and the warning channel  → plan P2-01

- Node class: frozen, private brand, belongs to no buffer, serialized eagerly by the execution's std and on demand otherwise; spans in a per-buffer side table.
- toContent, plain, and the NULLARY/CONTENT symbols. undefined/null → [] plus 'splice-undefined'.
- Interim m builds seq(text, content, …).
- DIAG op (since 7) for warnings only. Error nodes carry stable codes. invoke() frames for fences and the bib formatter.
- Raw-number $.style.push removed; popTo clamps in JS too.

**Golden impact:** splice/dot-rule (.ops, tree, blocks, breaks, layout, html): the 'undefined' text node disappears and a DIAG op makes that buffer v7. No other fixture: none splices arrays, null, plain objects or non-nullary functions.

**OPS bump (as designed):** True

**Fixes:** `markup-language/value-coercion`, `codegen-ops-model/val-coercion-adhoc`, `codegen-ops-model/missed:4`, `codegen-ops-model/shadow-mutability-forgery`, `codegen-ops-model/popto-stack-divergence`, `codegen-ops-model/executor-errors-not-diagnostics`

### S5 Template lowering and frames  → plan P2-02

- Codegen emits the LowerProgram and the hole module; lower.mjs interprets them. In this step the interpreter adapts bound CALLs to today's ctor shapes and region encoding.
- Frames at every block depth, with the statement exception.
- Hoisted #let; the declaration-free #{} becomes a statement hole; a declaring #{} stays verbatim at its flow position.
- #use prologue frames.
- Static abi export and the program hash.
- Incremental SyntaxError isolation (stub changed holes, bisection capped at depth 4) and the hole-module cache.
- Structural content args.
- New bench-edit variants.
- Amend v2 §2 (compilation model) and App A:319 (a repeated #let is a reassignment), v2 §12:251 and document-model.md:87 (statement exception).

**Golden impact:** All 48 test/golden/*/*.js.txt are replaced once by *.lower.txt (program dump) plus *.js.txt (hole module). .ops are expected byte-identical: the interpreter calls the same ctors in the same order, and hole results stay unspanned as with val(). This is verified with `record-fixtures --check`; any .ops diff is an S5 bug. From here on, adding a ctor or sugar never touches a JS golden.

**OPS bump (as designed):** False

**Fixes:** `parser-frontend/ctor-names-are-reserved-words`, `real-world-evidence/ctor-name-collision-fatal`, `codegen-ops-model/missed:0`, `parser-frontend/missed:3`, `markup-language/no-execution-containment`, `codegen-ops-model/no-per-block-containment`, `real-world-evidence/missed:0`, `markup-language/missed:3`, `parser-frontend/no-error-nodes`

### S6 Constructor ABI and registry  → plan P2-03

- Binder with param kinds (specs reproduce today's signatures); generated ctors for every public kind plus node(); derived ctors with specs; static and runtime manifests.
- Registry with trampolines, bound-call overrides, next and a depth guard.
- A region is a ctor with a Body param; the Body is lazy and memoized, fed by the legacy rows until S8.
- Built-ins registered through define(); the default region honours the legacy style aliases.
- figure forwards an explicit image attr subset.
- The interpreter now calls bound entries directly.

**Golden impact:** None expected. .ops are byte-identical because the schema write order equals today's order, the default region keeps its styled wrapper, figure forwards the same image attrs, and body evaluation order is unchanged (children, then builder nodes). Program dumps are unchanged because CALLs already carried bound attrs. Verified with `record-fixtures --check` over all 48 fixtures, including doc/refs (term), code/runs and code/hang (structured codeblock), figure/* and cite/*. New equivalence goldens: `*x*` ≡ `#strong[x]` ≡ m`*x*` at --stage=tree modulo spans, before and after one $.ctor override.

**OPS bump (as designed):** False

**Fixes:** `markup-language/closed-constructor-set`, `markup-language/ctor-signature-vs-content-args`, `codegen-ops-model/ctor-signatures-break-sugar-equivalence`, `codegen-ops-model/private-region-builders-and-missing-ctors`, `resolver/missed:0`, `markup-language/region-builtin-privilege`, `parser-frontend/region-fence-private-dispatch`, `codegen-ops-model/region-meta-args-hijack`, `real-world-evidence/sugar-dispatch-fixed`, `codegen-ops-model/missed:2`

### S7 Occurrence spans  → plan P2-04

- Hole results get SPAN when fresh and AT (since 8) when reused.
- copy() applies span containment and inheritance; N2 unwrap passes the para span to the block.
- The default fence stamps the code body span.
- New editor regression test: an edit between a #let and its splice re-patches only the edited paragraph.

**Golden impact:** Tree goldens: @[0,0) becomes real, paragraph-contained spans for splice results, region and builder children, unwrapped block splices (e.g. the doc/refs term group) and code bodies. html and semantic goldens gain the matching data-s/e. .ops are re-recorded for fixtures with splices or regions (extra SPAN ops). v8 appears only where a Node value is spliced twice, which no current fixture does.

**OPS bump (as designed):** True

**Fixes:** `codegen-ops-model/occurrence-spans-unsound`, `markup-language/span-loss`, `real-world-evidence/span-loss-synthesized-nodes`

### S8 Region bodies through ordinary lowering; level normal form (diagnose-only)  → plan P2-11

Requires T1's SoftBreak, sep and per-line provenance.
- Region interiors become block subprograms; body.rows() and body.blocks() replace the nested arrays and regionJoin.
- softbreak is a JS-only Node that serializes as its join text.
- Codegen emits mathblock for all display math; INLINE_FALLBACK reproduces the inline degrade.
- N4–N6 added: anon para, checked models, opaque and slot models, and the block-in-inline warning without a split.
- emit's default branches stay tolerant; debug builds assert the normal form (known violators: resolver-built term groups until T3).

**Golden impact:** - Non-table regions (region/figure, region aside): '|' text is now lossless instead of the ' | ' rejoin, and line joins come from T1's softbreak text. This affects tree, blocks, html, semantic and .ops.
- Table regions: tree expected identical; .ops string order may change.
- Math fixtures with mid-paragraph display math: .ops only (mathblock written, normalized back), tree identical.
- No other fixture: the replay shows no N4–N6 firing.

**OPS bump (as designed):** False

**Fixes:** `markup-language/block-inline-placement`, `codegen-ops-model/block-promotion-peepholes`, `codegen-ops-model/missed:1`, `markup-language/missed:0`, `codegen-ops-model/parse-time-pipe-segmentation`, `emitter/silent-drops-of-unhandled-kinds`

### S8b Block-in-inline split policy (gated on T6/T7 rendering para{cont})  → plan P3-17

N5 switches from warn-only to splitting a para at a Blocks position into [para, block, para{cont}]. Other inline contexts get error nodes. mathblock keeps its fallback unless T8 changes the row.

**Golden impact:** New fixtures only; no current fixture contains block-in-inline (replay).

**OPS bump (as designed):** False

**Fixes:** `markup-language/block-inline-placement`

### S9 Universal attributes, EXT, DECL and field  → plan P2-05

- role and label become universal, slot is a new key, and EXT names and values are added, all since 9. `class` stays reserved for T4.
- DECL op (since 9) with typed rows contributed by T3, T4 and T8, flow anchors and style-neutral templates; $.declare.
- RESOLVED flags enforced.
- The `field` kind row (since 9), with semantics landing with T3.

**Golden impact:** None: no fixture uses the new vocabulary, so every buffer stays at its version. The region style wrapper is kept, so style/patch is unchanged.

**OPS bump (as designed):** True

**Fixes:** `real-world-evidence/open-arg-schema`, `resolver/argk-overloading`, `codegen-ops-model/role-string-dispatch`

### S10 Fragment programs and sidecars in the default fence  → plan P2-13

- tsr_parse_fragment and tsr_parse_fragment_many with segment holes; the real m``, ctx.m.parse and ctx.m.parseMany; injected parse providers.
- The default fence splits sidecars in JS with offsets from T1 provenance, keeps the text body and adds group{slot:'margin'} (codeblock.margin SlotSpec, since 10).
- Delete fragment.cc and Doc::extractSidecars; emit and semantic_html test the slot id.
- Amend document-model §4.1 and architecture.md:120.

**Golden impact:** code/sidecar fixtures (tree, blocks, html, semantic, .ops re-recorded at v10):
- group{role:'sidecar-lines'} becomes group{slot:'margin'};
- margin spans become exact;
- ^[..] in a margin becomes a real note.
The code text body and its highlighting are unchanged.

**OPS bump (as designed):** True

**Fixes:** `parser-frontend/fragment-parallel-lowering`, `codegen-ops-model/duplicate-lowering-fragment`, `markup-language/sidecar-private-lowering`, `codegen-ops-model/sidecar-ingest-pass`, `api-measure-code/sidecar-api-layer-rewrite`, `emitter/sidecar-role-string`, `real-world-evidence/markup-reentry-missing`

### S11 Async bibliography in place  → plan P2-14

Needs T9's ctx.load and T3's collectors-after-refs. bibliography() awaits load, formats each entry through the 'format' entry inside a frame, and returns its collect node at the splice. finishBibliographies and the placeholder are removed.

**Golden impact:** cite fixtures: .ops order changes because the collector is emitted at its splice. The trees are identical, because #bibliography is the last block and the placeholder paragraph was already removed by N3.

**OPS bump (as designed):** False

**Fixes:** `codegen-ops-model/bibliography-placeholder-and-end-emission`

### S12 Keyword forms, content literals, #use exports and nested statements  → plan P2-12

Lower T1's Stmt nodes:
- #if → IF;
- #for → FOR, with a fresh hole table per iteration;
- #let x = [..] → a content subprogram assigned in a statement hole;
- #use → module exports registered;
- nested #let and #{} → statement holes inside nested frames.

**Golden impact:** New fixtures only.

**OPS bump (as designed):** False

**Fixes:** `codegen-ops-model/keyword-forms-uncompiled`

## Not generalized (kept special)

- **The kind set stays closed: there are no user-defined kinds** — document-model.md:66 keeps layout closed under the kind table. Users get typed ctors (defineCtor), role-based elements with declared attr schemas (DECL{element}), slots, EXT data and `field` placeholders. Together these carry semantics (T3) and data (T7) without teaching emit or layout new nodes.
- **The STYLE_PUSH/POP_TO schedule ops stay on the wire, and there is no universal per-node style attr** — The schedule is the documented v2 §12 commitment (design-decisions-v2.md:246-252). A universal STYLE_DELTA group would be a third encoding and would re-create the key-4 lang overload. styled is Transparent in the normal form, so wrappers cost nothing structurally.
- **Core attribute keys stay a closed compile-time enum; user data travels only as EXT** — Typed checks in emit and layout, varint size and the fuzz surface are worth more than per-document wire keys. EXT names are validated identifiers with scalar values. User element attrs are typed by their DECL schema in the binder and in T3's consumer, not by new wire keys.
- **Level and content-model changes are not versioned per row** — Supporting old semantics for old buffers would require multi-behaviour readers. Such changes are rare, for example T5's raw/image level and T6's tcell model. They are honest MIN_COMPAT bumps with a full re-record.
- **Region and fence headers keep the `k: v` object-literal body; positional header args are diagnosed** — v2 App A:318 and §2:48 keep the JS inside #f(…) unmodified, and every corpus opener uses k: v. Literal-only headers become program constants instead of holes.
- **MAKE_TEXT stays a dedicated op without args. `sep` stays JS-only, and softbreak stays JS-only until T5** — Text dominates the op stream (I9). T1's verifier showed the separator needs no wire form. softbreak gets a wire form only when its consumer, T5's emit resolution, exists; serializing it as today's join text keeps every multi-line paragraph byte-identical until then.
- **Fragments do not evaluate splices that carry JS arguments (#f(x) inside m.parse strings)** — That would need eval or new Function, which is CSP-hostile and outside the document module's scope. Holes plus scope-resolved bare names cover the real uses.
- **`#{…}` bodies with top-level declarations stay uncontained, verbatim at their flow position** — The corpus and blog contain 0 `#{` and 0 `#let` (tests: 3 and 5), and jslex is a bracket balancer. Containing declarations would need a JS parser. Declaration-free bodies, which include every test use, are contained. Declaring bodies get an info diag and can still be stubbed for syntax isolation; references that then fail are contained ReferenceErrors.
- **The keyword forms stay a closed set (#let/#if/#for/#use)** — This is documented (design-decisions-v2.md:89): loop variables must bind into content blocks. T2 only lowers them.
- **text, seq, error and node are sealed, and built-in builders call base ctors, so overrides do not reach builder internals** — toContent, the interpreter and the builders depend on their exact meaning. Letting a document override them would allow it to break engine invariants, or to recurse through a builder, without adding expressiveness.
- **One document scope, not let-in nesting per #let** — v2 §2:71 and App A:319 promise a single lexical scope with forward references from closures. Hoisting keeps that and still gives each statement a frame.
- **No hardbreak ctor yet** — The kind is reserved, but emit has no forced-break handling: hardbreak hits the inlineWalk default and is dropped (emit.cc:170-172). Exposing a ctor that renders nothing would be a lie. Its row becomes public when T6 implements forced breaks.
- **Bibliography formatting remains a JS function** — v2 §11.1:241 deliberately leaves CSL complexity to userland. The formatter is a registry entry with the common ctx and frame.

## Risks

- Template lowering amends a documented decision (v2 §2: 'compiles the whole document into a single JS function'). Its performance claims must be proven by the new bench variants before S5 lands. If the owners reject the amendment, the fallback is today's JS printer, driven by the same SUGAR table, with a conformance gate in which lower.mjs and codegen agree at --stage=tree on every fixture. That fallback keeps two lowering engines.
- Domain-directed positional binding keeps today's signatures, including heading(1, 'x') = label 'x'. Direct JS callers must learn that a string after the level is a label while a Node is content. This is documented and covered by the manifest and completions.
- Lazy region bodies change when interior side effects run: an unread interior never runs. That is intended for draft regions but could surprise handlers that rely on interior #{…} side effects. It is documented, and a handler can call body.blocks() to force evaluation.
- Span containment gives many synthesized nodes real source spans. T7's copy policy (data-syn) must keep handler- and resolver-synthesized text out of 'copy source' where it does not belong.
- Per-buffer versioning depends on discipline. A meaning change shipped without a MIN_COMPAT bump would mis-decode old buffers. The lock file, the CI decode of every fixture and review of schema.json diffs are mandatory.
- On-demand serialization of foreign Nodes adds a private-field check per child reference. It is cheap, but it sits on the hot path and is covered by the bench gate.
- Registry overrides can recurse or change built-in behaviour globally. Mitigations: a depth guard, the rule that overrides apply from their registration point on, and the runtime manifest showing who overrode what.
- S5 replaces all JS goldens at once, so reviewers must rely on `.ops byte-identical` plus the new program dumps.
- The softbreak JS-only representation must stay in lock-step with T1's join classifier until T5 adds the wire form; otherwise region rows and paragraph text could disagree.

## Open questions (decided in PLAN.md §3)

- Do the owners accept amending v2 §2 so that the document compiles to a LowerProgram plus a user-code hole module, executed by one interpreter, instead of one JS function? This is the design's answer to the two-engine critique. The fallback is noted under risks.
- Block-in-inline policy for S8b: split into para/block/para{cont} (Typst-like, proposed) or always an error? This needs T6/T7 agreement on rendering cont, and T8's decision on mathblock's fallback row.
- Should T3 rename the single-valued element identity from `role` to `element`? The wire key stays 6 either way, and `class` is reserved for T4's multi-valued tokens.
- Default InstLimits: is max(1M, 64 × raw nodes) nodes / depth 256 right, and are the limits host-only config (T9) or an author-visible setting (T4)?
- The DIAG op versus no executor warnings at all: errors are already error nodes. DIAG gives warnings without a content node (style-in-value, ctor-arg, splice-undefined) a spanned channel, at the cost of one codec entry. Do the owners accept it?
- Is rendering nothing for undefined/null splices, with a warning, the preferred behaviour, or should `undefined` stay visible as text (today's dot-rule golden)?
- How should region-scoped declarations (T4 rules, T8 macros) bind? Option one: DECL flow anchors plus by-name references from content attrs, as proposed. Option two: decls carried as slot children of the scoping container.

## Changelog (critique responses)

- Critic 1 #1 (blocker, binder vs current call shapes): ACCEPTED. I re-checked executor.mjs:174-175 (term via shadowText), :196-206 (codeblock body array), :118 (figure forwards args), test/golden/code/runs.js.txt:3 and test/golden/doc/refs.js.txt:6.
  - The binder now has typed param kinds: Attr, Projected, Text, Lines, Slot, Body. Ctor specs reproduce today's signatures: codeblock [lang, body: Lines], term [name: Projected], heading [level, label], list [ordered, start], mathblock [src, label]. Lines are never flattened.
  - Builders forward explicit attr subsets.
  - S6 is gated on record-fixtures --check over all 48 fixtures.
- Critic 1 #2 (blocker, group's single content model collides with bibentry and the sidecar margin): ACCEPTED, and the abstraction is restructured.
  - New level classes: Adaptive (group, term, error) and Trivia (comment). New opaque body models (Code, Lines, Data, Mixed), which are never wrapped or pruned.
  - SlotSpecs give slots their own models, and slot children are exempt from the parent's body model.
  - The full table for all kinds is published.
  - A replay over the .ops of all 48 fixtures (scratchpad/t2r/norm.py) shows only today's 7 unwraps and 2 empty-para removals; bibentry groups sit under collect (Data) and are untouched.
- Critic 1 #3 (major, resolver synthesizes non-normal shapes after instantiation): ACCEPTED.
  - normalize() is a standalone API that the resolver must call on synthesized subtrees.
  - term and group are Adaptive, so a mid-paragraph term stays inline as today (emit.cc:161-168).
  - emit's default branches stay tolerant, with only a debug-build 'emit-unnormal' assertion; they are no longer turned into errors.
- Critic 1 #4 (major, versioning: no floor, value-level vocabulary, since-7 clump, silent drops): ACCEPTED in full.
  - version = max(MIN_COMPAT = 6, the largest since used), with every current row at since 6.
  - since now also covers enum and flag members, slots, body variants and (kind, attr) applicability. I re-checked e04c0db: it added bits 16-18 and the lines body variant.
  - Each landing takes the next number: v7 DIAG, v8 AT, v9 universals/EXT/DECL/field, v10 margin slot.
  - Unknown vocabulary in an accepted buffer becomes an error node, or rejects the buffer for an unknown op; it is never dropped.
- Critic 1 #5 (major, arg write order is golden-visible): ACCEPTED. I re-checked model.cc:157-171 (the dump prints args in node order) and executor.mjs:120, :130, :260. Each kind's attr list in schema.json references the universals at today's position (group: role, label, name; heading: level, label; table: cols, align, label), and this is verified with record-fixtures --check.
- Critic 1 #6 (major, content frames defer pushes but never pop): ACCEPTED.
  - Deferral is dropped. Content blocks and all nested frames pop to h0 at exit, with a 'style-in-value' warning (v2 §12:251, document-model.md:87).
  - Statement blocks are the explicit, cited exception (document-model.md:83), with amendments scheduled in S5.
  - Region bodies are lazy and run inside the hook frame after h0 is captured; a nested statement push is popped with a diagnostic.
- Critic 1 #7 (major, bisection costs about 19 ms and 10 imports per keystroke): ACCEPTED. The restructured lowering mostly supersedes the problem.
  - The hole module contains only user code (about 95 tiny holes for pbr instead of 127 KB) and is cached by hash, so prose keystrokes import nothing.
  - Isolation stubs changed holes first (about one extra 0.3-0.5 ms import, measured with scratchpad/t2r/imp.mjs), caps bisection at depth 4, and falls back to a whole-document error while keeping the last good render.
  - A bench variant types inside a splice.
- Critic 1 #8 (major, AT re-spans only the root, breaking paragraph patch locality): ACCEPTED. I re-checked layout.cc:336-351 and typeset_html.cc:613-619. copy() applies a general span-containment rule: a node whose span is empty or outside its parent's takes the parent's span. AT gives the root the exact splice span. S7 adds an editor regression test that edits between a #let and its use.
- Critic 1 #9 (major, fence and m`` offsets): ACCEPTED. I re-checked inline.cc:583-588 (the body is a lineSpans join). ctx.lines and body.source.lines carry per-line {text, start} from T1. m`` spans are empty and inherit the occurrence span. Fragments take segment arrays, so the U+FFFC sentinel and its collision case are gone.
- Critic 1 #10 (major, ctx.next and async overrides): ACCEPTED.
  - invoke() stays synchronous for sync chains.
  - The interpreter awaits any thenable, so async overrides work for sugar and regions.
  - The examples now `await ctx.next(...)`.
  - A Promise passed into a ctor from user JS becomes error{content-thenable} naming the missing await.
- Critic 1 #11 (major, decl position lost, templates folded with the declaration-site style, decls duplicated when spliced twice): ACCEPTED. decl is no longer a content kind. It is a typed DECL schedule op carrying a flow anchor (EMIT index plus span), with templates instantiated style-neutral, issued once per $.declare call and never duplicated by splicing.
- Critic 1 #12 (minor, S4 dot-rule churn): ACCEPTED. I re-checked test/golden/splice/dot-rule.tree.txt:6. undefined renders nothing plus a 'splice-undefined' warning, the churn is called out in S4, and the policy is listed as an open question.
- Critic 1 #13 (minor, the bench has no splices): ACCEPTED. I re-checked tools/bench-edit.mjs:28-57. New splice-, region- and #let-heavy variants plus a syntax-error typing session gate S5, S6 and S7.
- Critic 1 #14 (minor, frozen nodes vs post-hoc stamping): ACCEPTED. Spans and aliases live in a per-buffer side table. Region style uses today's styled wrapper, never root mutation. Multi-root results are defined: each root is stamped, and one styled wrapper covers all of them.
- Critic 1 #15 (minor, EXT key overflow and data-* injection): ACCEPTED. EXT uses a marker key plus a name strRef (no u16 arithmetic), and names are validated as [a-z][a-z0-9-]{0,31}. Fuzz cases are added.
- Critic 1 #16 (minor, RT_ABI unreachable for old modules; sugar ids not covered): ACCEPTED. A static `export const abi` is checked before default() runs, and the program header carries the same hash = hash(schema, sugar.def, program opcodes, protocol). CALLs name ctors by string, so there is no sugar-id skew.
- Critic 1 #17 (minor, rule precedence, document-level runs, cont regression): ACCEPTED. Precedence is N1 fallback before any split. N0 never merges EMIT roots. The split moves to S8b, gated on T6/T7, and S8 only diagnoses.
- Critic 1 #18 (minor, S5 vs S7 __at inconsistency): ACCEPTED. In S5 hole results stay unspanned (today's val()); S7 adds the SPAN and AT ops.
- Critic 1 #19 (minor, duplicate diagnostics and V8 message nondeterminism): ACCEPTED. Errors exist only as error nodes, and DIAG is for warnings. DiagSink dedupes by (code, span). Error nodes carry stable codes and JS error names, and the recorder normalises V8-originated messages.
- Critic 1 #20 (minor, S1 factual errors): ACCEPTED. I re-checked resolve.cc:193 and :208, and emit.cc:656.
  - The RESOLVED rows are note.name and mathblock.name; 'note.number' never existed.
  - emit.cc:656 is added to the replaced casts.
  - TOKEN accepts "" (code/hang), and group.name is listed.
  - CI asserts zero ops-arg diags.
- Critic 1 #21 (minor, #use outside any frame; undeclared doc amendments): ACCEPTED. #use runs in prologue frames that leave names undefined on failure. document-model §4.1, architecture.md:120, v2 §2 and v2 §12:251 are explicitly amended in S5 and S10.
- Critic 1 missing items:
  - markup-language/missed:3: addressed by #6.
  - sidecar-ingest-pass: addressed by #2 plus the Lines slot model, and the text body is kept.
  - resolver/rewrite-normalizations: addressed by #3.
  - version-bump-per-vocabulary: addressed by #4.
  - ctor-signatures-break-sugar-equivalence: addressed by #1.
- Critic 1 overlaps:
  - ACCEPTED: T1 owns per-line provenance; T3 relies on Adaptive group and opaque models plus DECL anchors; T6 gating for cont and the margin slot; T7 owns span policy and EXT handling; T9 owns one handshake (abi, program hash, version window).
  - PARTIALLY ACCEPTED for T4: frame semantics now pop as T4 asked and templates are style-neutral. STYLE_PUSH's wire does not change, because unification is validation-only, so no MIN_COMPAT bump is needed.
- Critic 2 #1 (major, #use libraries vs OpBuf-bound ctors): ACCEPTED, option (a).
  - Nodes belong to no buffer. The execution's std writes eagerly, so bytes are identical, and foreign Nodes are serialized on demand by whichever buffer references them.
  - `tsm:std` exports buffer-free ctors.
  - No ambient current buffer means concurrent documents are safe (worker.mjs:14).
  - Spans live in side tables, never on Nodes.
- Critic 2 #2 (major, override contract inconsistent; destructured std misses overrides): ACCEPTED. Overrides receive the bound call (call, ctx) and delegate with next(call, ctx). std exports are trampolines that resolve the registry at call time. A post-override equivalence golden is added in S6.
- Critic 2 #3 (major, binder breaks term, structured codeblock and heading('Intro')): ACCEPTED (see Critic 1 #1); heading('Intro') now binds as a kid at level 1, and the risk text is corrected. PARTIALLY REJECTED: making term.name a slot. The Projected param keeps today's wire shape (golden-neutral) until T3 replaces term. The caption slot is T3's, and the margin slot is adopted.
- Critic 2 #4 (major, sidecar structured body loses highlighting): ACCEPTED. I re-checked doc.h:152-155. The default fence keeps the code as a single text body plus the margin slot, which is exactly today's shape. Highlight eligibility becomes the schema predicate 'Code body with a text variant and a non-empty lang'. An overlay slot for literate refs is reserved for T5/T9.
- Critic 2 #5 (major, `class` collides with dynClasses): ACCEPTED. The single-valued element identity stays `role` (key 6), owned by T3. `class` is reserved for T4's multi-valued tokens with their own key. The default region produces group{role: name}.
- Critic 2 #6 (major, unequal footing for user elements and derived ctors): ACCEPTED.
  - defineCtor specs use the same param and Domain vocabulary for derived and user ctors, so figure gets a spec.
  - DECL{element} carries an attr schema that the binder and T3 validate against.
  - The manifest is generated from the registry, both statically and at runtime.
  PARTIALLY REJECTED: per-document wire keys. User attrs still travel as EXT on the wire (I3 fuzz surface).
- Critic 2 #7 (major, user regions not callable; 'regionable' flag): ACCEPTED. A region is a ctor that declares a Body param, and `regionable` is dropped. $.region is sugar that returns the callable trampoline. $.element (T3) registers decl and ctor atomically.
- Critic 2 #8 (major, decl stringly typed and hoisted out of scope): ACCEPTED. There are typed decls rows contributed by T3, T4 and T8, and a schedule-level DECL op that references template ids, with flow anchors. There is no Meta content kind. Content-scoped binding is listed as an open question with T4/T8.
- Critic 2 #9 (major, no resolver-filled placeholder for templates): ACCEPTED as a T2 wire row: `field{name, of}` (Inline, None, since 9), with semantics owned by T3. The theorem example now shows 'Theorem 2.1' via field.
- Critic 2 #10 (major, normalize fused into copy() cannot see resolver output): ACCEPTED. normalize() is standalone and contractually called by the resolver; the fused path is kept as an optimization, plus a debug assertion before emit.
- Critic 2 #11 (major, version byte 1 for plain docs; level changes; false append-only premise): ACCEPTED. The baseline since is 6. Level and content-model changes bump MIN_COMPAT, so the 'raw/image Both is a data change' claim is withdrawn. v2–v5 buffers are not accepted. The premise is corrected: 85312ee changed STYLE_PUSH payload meaning, and e04c0db added a codeblock body variant.
- Critic 2 #12 (major, three style encodings and the lang shadowing): ACCEPTED. The universal STYLE_DELTA group is dropped and styled is the only node-level delta. STYLE_PUSH patches are validated by styled's specs. codeblock.lang and styled.lang are separate per-kind specs on key 4, so nothing shadows.
- Critic 2 #13 (major, softbreak has no wire form): ACCEPTED. A softbreak kind row is added. It is JS-only, serialized as today's join text so .ops stay identical, until T5's emit resolution lands; then it gets its own since. rows() splits on it in JS.
- Critic 2 #14 (major, two lowering engines; document-model §4.1 silently replaced): ACCEPTED, and restructured rather than patched with a conformance test. The document and all fragments lower to one LowerProgram format, run by one interpreter, with user JS confined to a hole module. v2 §2 and document-model §4.1 are amended explicitly, and the fallback is recorded under risks.
- Critic 2 #15 (minor, #{} hoisting and bisection are heavy for the evidence): ACCEPTED. Only #let is hoisted. #{} is contained only when it has no top-level declarations; otherwise it stays verbatim at its flow position with a diagnostic. Isolation goes by changed-hole hashes. Declaring blocks can still be stubbed, and the resulting ReferenceErrors are contained.
- Critic 2 #16 (minor, frame style semantics contradict docs): ACCEPTED (see Critic 1 #6), with document-model §3 reconciled through the stated statement-block exception.
- Critic 2 #17 (minor, block table smuggled in a JS comment): ACCEPTED. The block table is in the LowerProgram header, tsr_compile returns {program, js} through the C ABI, and `tsrc --stage=lower` dumps it.
- Critic 2 #18 (minor, one parse crossing per sidecar note): ACCEPTED. ctx.m.parseMany issues one tsr_parse_fragment_many crossing per fence.
- Critic 2 #19 (minor, no list or range domains): PARTIALLY ACCEPTED. Structured domains are string-encoded on the wire and parsed once at decode into typed accessors: rangeset (hl) now, and T6's tracks/spans later, with no codec change. List-valued ArgVals are REJECTED: EXT stays scalar, so the codec and its fuzz surface do not grow.
- Critic 2 #20 (minor, nested X-macro rows too fragile for a regex generator): ACCEPTED. The source is schema.json plus schema.lock.json, and gen-schema.mjs emits committed C++ and JS that CI checks for freshness. Ids and since values are immutable against the lock.
- Critic 2 #21 (minor, one hook failure reported twice): ACCEPTED (see Critic 1 #19).
- Critic 2 #22 (minor, hardbreak omitted; group/term levels unspecified): ACCEPTED. Every kind now has a declared level (group and term Adaptive). hardbreak is recorded in not_generalized, because emit drops it today.
- Critic 2 missing items:
  - two-style-encodings: now listed in S1 fixes.
  - missed:2: moved to S6.
  - markup-language/missed:3: argued with citations.
  - open-arg-schema: level and model changes are honest MIN_COMPAT bumps.
  - sidecar: text body kept.
  - closed-constructor-set: user regions callable; hardbreak explained.
  - css-injection, sizepx, metric-key, tree-dump-omits-sup: added as a T4-owned subsumption row, with T2's StyleVal hook in S1.
- Critic 2 overlaps: all ACCEPTED as interface contracts.
  - role/class split across T3, T4 and T7.
  - T4 owns presentation rules, so the $.ctor restyle example became a structural one.
  - DECL anchors for T4 and T8.
  - softbreak row for T1 and T5.
  - T9 owns ctx.load, caching and bibliography resource policy.
  - EXT as a display-list handle for T7.
  - Overlay slot for T5 and T9.
  - normalize() and field for T3.
  - String-encoded structured domains and SlotSpecs for T6.
