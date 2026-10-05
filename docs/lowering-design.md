# Lowering: the LowerProgram and the hole module

Status: as built in plan P2-02 (design T2 S5, `docs/remediation/design/
T2-constructor-ir.md`; decision MD-04, which amends v2 §2). Later steps:
P2-03 (the bound constructor ABI and the registry: CALL attributes bind by
key), P2-04 (SPAN/AT on hole results), P2-12 (keyword forms: IF/FOR ops),
S10 (`#use` prologue frames), and the fragment entry points (`m```,
`ctx.m.parse`, sidecar notes), which will produce programs in this format.

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

Body ops (the high bit of an op byte marks a subtree that awaits):

| op | operands | runs as |
|---|---|---|
| TEXT | str s e | `span(text(str))` |
| CALL | flags ctor [s e] nAttrs (key const)* nKids value* | `ctor(...attrs, ...kids)`, spanned if flagged |
| HOLE | hole s e nKids value* | `val(h[hole](__k))` |
| FRAME | s e holeLo holeHi value | the value inside a frame (§4) |
| FENCE | lang args body bodyOffset lines s e | `span(val(await fence(lang, args, body, off, lines)))` |
| REGION | name args s e nItems item* | `span(region(name, args, items))` |
| ROWS | nRows (nCells value*)* | a table paragraph, as a region item |
| STMT | hole | a statement hole (top-level block only) |
| VERBATIM | ordinal | the module runs the statement (top-level block only) |

`args` is 0 (none) or `(hole + 1) << 1 | awaits`. Holes are numbered in
preorder, so the holes of any frame are one contiguous range; the reader
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
const {para, text, em, …} = __rt.std;     // the user-visible names
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

- **Scopes.** The outer function binds every user-visible constructor name;
  the inner one holds the hoisted names and the user's declarations, which
  may shadow them (`#let list = 1`). Generated names are only `__rt`, `$`,
  `__h` and `__k`; user bindings may not start with `__` (`reserved-name`).
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
  sees template `${…}` holes) is an `async` function and its op carries the
  async bit; so does every op above it. Nothing else is awaited — a hole
  that merely returns a promise splices it as an object, as before.
- **No user code, no module.** The program then runs as segment 0 alone,
  and nothing is imported.
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

Diagnostics of a running block (`splice-undefined`, `splice-object`,
`region-error`, `bib-load`) point at the *current* block: the last top-level
block with user code that started.

The interpreter calls the executor's constructors in the order the printed
JavaScript of P0-05 evaluated them (arguments left to right, children before
parents), and a hole's result stays unspanned, as `val()` left it — so every
recording was byte-identical across the change (G3).

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

- The module destructures every user-visible name rather than those its
  holes mention: the two scopes make that safe, and a missed name could
  never become a ReferenceError.
- `__rt.run(__h, k)` takes a segment number instead of a block range, which
  keeps the module text independent of prose (cache hits).
- The NAME, IF, FOR, SOFTBREAK and ERROR ops are not needed yet (fragments,
  keyword forms, S13 soft breaks; an error node is `CALL error`).
- Isolation follows D-I11's import budget rather than "depth 4".
- Table paragraphs are framed too; a frame's error ahead of a table's first
  row starts a row instead of being dropped.
