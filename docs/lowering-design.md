# Lowering: the LowerProgram and the hole module

Status: as built in plan P2-02 (design T2 S5, `docs/remediation/design/
T2-constructor-ir.md`; decision MD-04, which amends v2 §2), with P2-03 (the
bound constructor ABI and the registry: CALL attributes bind by key), P2-04
(SPAN/AT on hole results), P2-12 (statements anywhere, keyword forms,
content literals: IF/SCOPE/LOOP/LET, §3.1) and P2-13 (fragments: `m```,
`m.parse`, `ctx.m.parse`, sidecar notes, §5.1). Later step: S10 (`#use`
prologue frames).

## 1. Shape

```
AST ──► codegen (engine/src/codegen/codegen.cc)
          ├─► LowerProgram  binary: the markup as constructor calls + holes
          └─► hole module   JavaScript: the user's code only ("" if none)
host:  tsr2_program(doc) → program bytes (one copy)
       tsr_get_js(doc)    → the module, read only on a cache miss
executor (runtime/src/worker/executor.mjs)
       decodeProgram → loadModule (cache, SyntaxError isolation)
       → Lowering.run (runtime/src/shared/lower.mjs) → OpBuf → ops
```

Static markup never becomes JavaScript text, so it cannot fail to parse, it
is never parsed by V8, and a generated name can never meet a user binding.
User code lives in the hole module: each splice, `#let`, statement and
fence/region argument list is one *hole*, a small function in the module's
`__h` array.

## 2. The program (`lower.def`, `lower.h`)

`engine/src/codegen/lower.def` is the one source of the opcodes, constant
tags, block kinds and flags; `tools/gen-lower.mjs` generates
`lower.gen.h` and `runtime/src/shared/lower.gen.mjs` (G9 checks them).
`PROGRAM_ABI` is an FNV-1a hash of the def's rows (not its comments).
Vocabulary is not part of it — the `schemaHash` handshake covers that — so a
new constructor touches no program and no hole module.

Layout (varints unless sized):

```
"TSLP" version:u8 abi:u32le hash:u64le moduleFlag docEnd
nStr (byteLen u16Len)* blob          strings, valid UTF-8 (sanitized)
nCtor strRef*                        constructor names CALL refers to
nBlock (kind flags s e holeLo holeHi pc)*   the top-level blocks
nHole
nPiece (kind ref js0 js1)*           user code in the module (UTF-16 range)
bodyLen body                         preorder ops
```

The block table makes every top-level block addressable: its kind
(`Content`, `Stmt` — a contained statement hole —, `Verbatim` — a statement
left unframed, D-I10), its flags (`User`: runs user code; `Framed`;
`Async`: awaits), its span, its holes and its first op.

Body ops (the high bit of an op byte marks a subtree that awaits; a FENCE
and a REGION always do):

| op | operands | runs as |
|---|---|---|
| TEXT | str s e nMap (cooked raw)* nSep sep* | `span(text(str))`, and its cooked→raw map (RAWMAP, document-model §4.3); (P2-11) the cooked offsets of its cell cuts — with its span, map and soft breaks, JS-only provenance (`ob.prov`) for `body.rows()` |
| CALL | flags ctor [s e] nAttrs (key const)* nKids value* | `ctor(...attrs, ...kids)`, spanned if flagged |
| HOLE | hole s e nKids value* | `val(h[hole](__k))` |
| FRAME | s e holeLo holeHi value | the value inside a frame (§4) |
| FENCE | lang args body bodyOffset lines s e | `span(val(await fence(lang, args, body, off, lines)))` |
| REGION | name args s e nItems value* | `span(await region(name, args, items))` (always awaits: a handler may be async); (P2-11) its interior is ordinary blocks — the ROWS op of table paragraphs is gone |
| STMT | hole | a statement hole; as a value (P2-12, nested) it is no content |
| VERBATIM | ordinal | the module runs the statement (top-level block only) |
| IF | nBranch (cond+1 value)* | (P2-12) the first branch whose condition hole holds (0: else) — the others skipped; none: no content |
| SCOPE | hole nHoles value | (P2-12) `h[hole](__b)`: the value against the scope's own hole table (§3.1) |
| LOOP | hole nHoles s e value | (P2-12) `h[hole](__b)`: the value once per iteration, the results as one value (§3.1) |
| LET | hole value | (P2-12) `h[hole](value)`: binds a content literal; no content |

`args` is 0 (none) or `(hole + 1) << 1 | awaits`. Holes are numbered in
preorder — per hole table: a SCOPE or LOOP value numbers its own from 0 —,
so the holes of any frame are one contiguous range; the reader
checks this exactly, together with every count, reference and span, the
strings' UTF-8 and UTF-16 lengths, the blocks tiling the body, and the async
bits agreeing with their subtrees (`readLowerProgram`; the fuzz target
`fuzz_lower` feeds it arbitrary bytes, and `fuzz_inline` asserts that every
program codegen writes reads back valid).

CALL attributes are keyed (`heading level=2 label=null`). In this step the
interpreter passes them positionally in written order — today's constructor
shapes; P2-03's binder will bind them by key.

Dumps: `tsrc --stage=lower` (the `*.lower.txt` goldens), `--stage=js` (the
module, `*.js.txt`), `--stage=program` (the bytes; the recorder runs them).

## 3. The hole module

```js
export const abi = 0x3c18dc46;
export default async (__rt, $) => {
const {seq, text} = __rt.std;             // the std names its code mentions
return (async () => {
let avg;                                   // hoisted #let names
const __h = [
() => { avg = (
 (a, b) => (a + b) / 2
); },                                      // #let avg = …
() => (avg(3, 5)),                         // #avg(3, 5)
(__k) => (wrap(...["tip: "], ...__k())),   // #wrap("tip: ")[content]
() => ({lang: "zh"}),                      // a fence's arguments
];
await __rt.run(__h, 0);                    // the blocks up to the first verbatim statement
function helper() {}                       // a declaring #{…}: verbatim (D-I10)
await __rt.run(__h, 1);
})();
};
//# sourceURL=tsm:doc
```

- **Scopes.** The outer function binds the user-visible std names the
  module's code mentions (an identifier scan that also sees template `${…}`
  holes — a name never mentioned cannot be referenced, so a new constructor
  changes no module); the inner one holds the hoisted names and the user's
  declarations, which may shadow them (`#let list = 1`). Generated names are only `__rt`, `$`,
  `__h` and `__k` (and §3.1's); user bindings may not start with `__` (`reserved-name`).
- **`#let x = e`** hoists `x` (once: a repeated `#let` reassigns, D-L02)
  and becomes the statement hole `x = (e)`. A reserved word (`#let class =
  1`) is not hoisted: the statement stays verbatim and is isolated as a
  syntax error.
- **Statements.** A `#{…}` without top-level declarations is a contained
  statement hole. A declaring `#{…}`, and a `#let` of a pattern or of several
  declarators, stay verbatim at their place (D-I10); the program runs in
  *segments* between them, `__rt.run(__h, k)`. Segments are numbered by
  statement, not by block, so the module text depends on the user code only:
  an edit to prose leaves it byte-identical.
- **Content arguments are structural.** `#f(args)[c]` is the hole
  `(__k) => (f(...[args], ...__k()))`: the interpreter supplies `__k`, which
  builds the content arguments once, at the point the argument list reaches
  them (after the call's own arguments, as written). An empty list, a
  trailing comma or a comment needs no text surgery. If the call never gets
  there (an argument threw), the content is skipped.
- **Awaiting.** A hole whose code mentions `await` (a token scan that also
  sees template `${…}` holes), or names a constructor that loads (plan
  P2-14: `kStdAsync`, `bibliography`), is an `async` function and its op
  carries the async bit; so does every op above it. Nothing else is awaited — a hole
  that merely returns a promise splices it as an object, as before.
- **No user code, no module.** The program then runs as segment 0 alone,
  and nothing is imported.

### 3.1 Statements anywhere, keyword forms, content literals (P2-12)

```js
let xs, n;                                 // the document scope's names
const __h = [
() => (                                    // #if (n > 2) [ … ] — a condition
n > 2
),
(__b) => {                                 // its body declares: a SCOPE
const __s0 = typeof n === "undefined" ? undefined : n;
{
let n = __s0;                              // starts as the n it shadows
return __b([                               // the body's own hole table
() => { n = (n + 1); },                    //   #let n = n + 1
() => (n),                                 //   #n
]);
}
},
(__b) => {                                 // #for (const x of xs) [ - #x ]
const __r = [];
for (
const x of xs
) {
{
__r.push(__b([
() => (x),
]));
}
}
return __r;
},
(__v) => { card = __v; },                  // #let card = [ *Card* … ]
];
```

- **Scopes.** The document is one scope; each keyword body is a scope of
  its own. A scope's names are the `#let x = e` and `#let x = [ … ]`
  statements anywhere in it — a list item, a quote, a region, a content
  argument, a content literal — except inside a nested keyword body: they
  are hoisted to the scope (once, D-L02). A body that declares names is a
  SCOPE, and a loop body is always one: the scope's hole is the scope in JS
  (the loop, the names it declares) and hands its table to `__b`, which
  evaluates the body against it — the table's functions close over the
  scope's names and the loop's bindings, made afresh per iteration. A
  scope's names start as the names they shadow, read outside the block that
  declares them, so `#let n = n + 1` reads the outer `n`, as before its
  `#let`; the outer `n` is unchanged. A body that declares nothing (an `if`
  branch) keeps its holes in the table around it.
- **Statements anywhere.** A `#let` or `#{…}` nested in content is a STMT
  (no content: a parent drops `undefined`) in its own frame. Only a
  scope-level `#let x = e` binds beyond itself; a declaring `#{…}` or a
  `#let` of a pattern nested in content keeps its bindings to itself (info
  `statement-local`) — at top level it stays verbatim (D-I10). A nested
  statement that leaves styles pushed has them popped, with
  `style-in-value` (D-L12: scoped style is `$.set`, P3-01).
- **Keyword forms.** `#if (c) [A] else if (d) [B] else [C]` is IF: the
  conditions are holes evaluated in order until one holds, the other bodies
  skipped; no branch taken is no content. `#for (head) [B]` and `#while
  (c) [B]` are LOOPs: the loop is the user's JS (`for (head)`, `while (c)`),
  the body is evaluated once per iteration, and the iterations become one
  value (`env.loop`): their content in order, a body's seq opened, and
  adjacent lists of one kind joined into one list (`#for (…) [- #x]` is one
  list); no iteration, no content. A body's blocks — and a content
  literal's — are each framed, so an error stays in the iteration it
  happens in. A body that is inline content keeps its edge whitespace (a
  space, or a line break: a soft break), so iterations do not run together
  (`[#x, ]`).
- **Content literals.** `#let x = [ … ]` is LET: its body is built where it
  stands and bound to `x` (a content value; splicing it again is an AT
  alias). This is a behavior change: `[…]` after `#let x =` was a JS array
  — write `Array.of(…)` or `#{ let x = […] }` (tsm-changes).
- **Generated names** add `__b`, `__r`, `__v` and `__s<i>`. A SCOPE or LOOP
  hole is one piece of the SyntaxError isolation (§5): a broken condition or
  loop head stubs its form, whose frame reports `script-syntax`.
  `LOWER_PROTOCOL` is 2.
- `export const abi` is checked against the runtime's `PROGRAM_ABI` before
  `default()` runs.

## 4. Execution and frames

`Lowering.run(h, k)` runs segment k's blocks in order. A block that runs
user code executes in a frame, and so does every block-level node below it
that does: each child of a list item, a quote or a region interior —
including a table paragraph — gets a FRAME op when its subtree has holes.
Blocks without holes cannot throw and get none (fence and region *handlers*
are contained where they are invoked: `fence-error`, `region-error`).

A frame records the style stack height on entry. When its value throws:

- the style stack pops back to that height (only then — `#{ $.style.push(…)
  }` deliberately outlives its block; v2 §12 as amended);
- at top level, an error block takes the block's place
  (`script-error`, or `script-syntax` for a stubbed hole, §5);
- deeper, an error node takes the child's place in its parent, and the
  interpreter resumes after that child.

A verbatim statement runs outside any frame (its bindings must reach later
code). If it throws, the module function fails: the ops written so far stay,
and one error block covers the rest of the document (D-I10).

Diagnostics of the run (`splice-undefined`, `splice-object`,
`splice-function`, `region-error`, `bib-load`) point at the innermost
construct the interpreter is running — the splice, the region, else the
frame or block. (Until the follow-up commit of P2-02 they pointed at the
last top-level block with user code that started, so a region handler's
error was reported at the statement that registered the handler.)

The interpreter calls the executor's constructors in the order the printed
JavaScript of P0-05 evaluated them (arguments left to right, children before
parents), and in P2-02 a hole's result stayed unspanned, as `val()` left it
— so every recording was byte-identical across that change (G3). Since
P2-04 every construct's result is placed at its occurrence: a result made
during the construct gets SPAN, an earlier value spliced again an AT alias
(document-model §4.3).

## 5. The module cache and SyntaxError isolation (D-I11)

The program header carries the FNV-1a 64 hash of the module text. The
executor keeps the 16 most recent modules by hash: a keystroke in prose
imports nothing, and the host does not even copy the module out of the
engine (`compiledOf` passes a getter).

On a SyntaxError (failure path only):

1. The suspects are the pieces (holes and verbatim statements, located by
   the piece table) whose text has not compiled in an earlier module; if
   all are known, every piece is a suspect.
2. The module is imported with the suspects stubbed — a hole as `null`
   (its frame reports `script-syntax`), a verbatim statement as
   `__rt.syntax(block)` (an error block). If that still fails, every piece
   is stubbed; if even that fails, the document fails.
3. The stubs are restored by bisection: a group that imports stays
   restored, a single culprit stays stubbed; when one half restores cleanly
   the other is known bad and is not re-tried. At most 2⌈log2 n⌉+4 imports
   for n suspects, after which the remaining suspects stay stubbed.

Typing inside one splice therefore costs two imports per keystroke while the
expression is broken (the failing module and the stubbed one), and none once
it compiles again and its text is cached.

## 5.1 Fragments (as built from plan P2-13; design T1 S11, T2 S10; D-L07)

Markup parsed at run time — `` m`…` ``, `m.parse`, a fence handler's
`ctx.m.parse`, a code block's sidecar notes — lowers to the same program
format and runs on the same interpreter; there is no second lowering
(`fragment.cc`, with its own AST→content switch, is gone).

```
JS: host.fragments(texts, {bases | clamp, scope, vals})
    → opts.parse(request)            the host's engine: tsr2_fragments (WASM),
                                     `tsrc --fragments=-` (the native tools)
    → [program][{"holes": […], "diags": […]}]
    → new Lowering(program, env).fragment(i, holes)   one value per text
```

- **One program, one block per text** (`codegenFragments`,
  engine/src/codegen/codegen.h has the wire form). A text is parsed as a
  document — line pass, AST — and its content is a content body's: one
  paragraph (statements aside) is its inline content; each block of it that
  has holes is framed, so a failing hole is an error node in place.
- **Spans.** `bases`: each text's source offset, so every span is exact (a
  sidecar note, `m.parse(src, {offset})`, `ctx.m.parse(…, {offset:
  ctx.offset})`); else every span is the clamp — the construct running it
  (`here`) — and texts carry no raw maps.
- **No JavaScript.** Holes are descriptors, out of band: `{"v": k}` the k-th
  interpolation of `` m`…` `` (written `#(__mk);` into the text, so markup
  may span an interpolation), `{"p": "a.b"}` a bare value head — looked up
  on `scope`, then the std (D-L07: no eval, CSP-safe) — with `"k": 1` when it
  takes content arguments. Any other splice (`#f(x)`), a statement, a
  keyword form or a fence/region argument list stays text, with info
  `fragment-splice`. The program has no module (`readLowerProgram` skips
  the piece count then).
- **Diagnostics** of the parse come back mapped (exact or clamped) and are
  emitted as `fragment-parse` (`code: message`) or `fragment-splice`.
- **Awaiting.** A fragment whose value awaits (a fence in it) returns a
  promise; the blocks of one request run in turn.
- `` m`…` `` reads the template's **raw** strings (a backslash is markup's
  escape). `m.parseMany(srcs, {scope, offsets})` parses several in one
  crossing.
- The fuzz target `fuzz_fragment` takes any bytes as a request and as one
  text (at a base, clamped): the decoder rejects or the program reads back,
  with JSON naming one descriptor per hole (D-H08).

## 6. The handshake

`tsr2_abi()` reports `programAbi` = `PROGRAM_ABI`; `checkAbi` (worker, Node)
refuses an engine of another program ABI. The program header and the hole
module carry it too, so a stale cached module or program fails with one
message.

## 7. Performance

Measured with `tools/bench.sh` (update median, min of 3 runs; the 87K plain
document and the user-code variants of `tools/bench-edit.mjs --variant`):

| document | printed JS (P1 end) | LowerProgram |
|---|---|---|
| plain 87K: compile + execute | 1.3 + 4.2 ms | 1.3 + 2.4 ms |
| plain 87K: edit | 27.7 ms | 25.6 ms |
| splice | 41.7 ms | 36.5 ms |
| region | 36.2 ms | 31.3 ms |
| let | 38.8 ms | 30.7 ms |
| typing inside a splice (every other keystroke a SyntaxError) | 262.4 ms | 41.1 ms |

Execution is cheaper because static markup is decoded from a compact buffer
instead of parsed by V8, and a document without user code imports nothing;
the SyntaxError case because only the changed piece is a suspect.

The worker yields one turn of its event loop after executing (a MessagePort
hop): the per-document mailbox (P0-11) relies on newer messages arriving
while a job runs, and the module import used to provide that turn.

## 8. Deltas from the design (T2 S5)

- `__rt.run(__h, k)` takes a segment number instead of a block range, which
  keeps the module text independent of prose (cache hits).
- The NAME, IF, FOR, SOFTBREAK and ERROR ops are not needed yet (fragments,
  keyword forms, S13 soft breaks; an error node is `CALL error`).
- Isolation follows D-I11's import budget rather than "depth 4".
- Table paragraphs are framed too; a frame's error ahead of a table's first
  row starts a row instead of being dropped.
