# Constructors: one calling convention, one registry

Status: as built in plan P2-03 (design T2 S6, `docs/remediation/design/
T2-constructor-ir.md`). Later steps: P2-04 (a splice's fresh result gets its
span), P2-05 (universal options — role, label, slot, ext, style — and the
`field` kind), P2-13 (markup re-entry: `m```, `m.parse`, `ctx.m.parse`; a lazy
Body). Related: `docs/lowering-design.md` (how markup reaches a constructor).

## 1. Specs

Every public kind has a constructor, described in `engine/schema/schema.json`:

```json
"heading": {…, "ctor": {"params": ["attr:level", "attr:label"]}, "attrs": {…}}
"trow":    {…, "ctor": {"name": "row"}, …}
"rule":    {…, "ctor": {"nullary": true}, …}
"seq":     {…, "ctor": {"sealed": true}, …}
```

A param is `attr:NAME[:DOMAIN]`, `projected:NAME`, `text`, `lines` or `body`;
every attribute not bound positionally is an option. `doc` and `hardbreak`
have none (P3-33). The `stdlib` section adds the derived constructors
(`strong`, `em`, `style`, `figure`, `toc`, `glossary`, `notes`,
`bibliography`, `node`; options `"raw"` = the options object reaches the
implementation unvalidated), the std functions (`val`, `m`, `plain`) and the
option aliases (`float`→`side`, `width`/`height`→`w`/`h`).

`tools/gen-schema.mjs` generates `runtime/src/shared/ctors.gen.mjs` (the specs
the binder reads), `engine/src/codegen/stdnames.gen.h` (the sorted names a hole
module may bind — a module binds those its code mentions, so a new constructor
changes no module) and the constructor column of `docs/schema-table.md`.

## 2. The binder

`bind(spec, args)` → a bound call `{attrs, kids, lines, body, options}`:

1. Positional params bind left to right while the param accepts the
   argument; the first refusal ends positional binding.
   - Attr: a scalar of its domain's type (number for int/num, boolean for
     bool, string otherwise; `str` also takes a number); null or undefined
     is "present but absent".
   - Projected: a string, number or node, projected through `plain()`.
   - Text: a string or number, as one text kid.
   - Lines: a string (one text body) or an array of lines (each a node, a
     string or an array of runs).
   - Body: a Body value.
2. A plain object next is the options object: attributes by name, aliases
   applied (an alias wins over the plain key). An unknown option is dropped
   with a `ctor-arg` warning. Values are checked by the ops reader at decode
   (`ops-arg`), as before.
3. The remaining arguments are kids, through `toContent`. A constructor with
   a Body param called with content receives its kids as the Body.

The specs reproduce today's call shapes — `codeblock(lang, lines, opts)`,
`image(src, {w, h})`, `term(name, …)`, `heading(1, 'label', …)` — so every
recorded document binds as before, while `#heading(2)[T]` and
`#list(false)[a]` no longer bind their content as the label or the start.
`text(x)` and `error(code, message)` keep their coercing signatures.

## 3. The registry

`runtime/src/shared/registry.mjs`: one table per namespace (`ctor`, `fence`,
`format`); `define(ns, name, next => fn, {spec, sealed, user})` captures the
entry it replaces as `next`. The std the hole module sees is made of
*trampolines*: stable functions that resolve the entry at call time, so an
override applies from its registration on — to explicit calls, to markup and
to code that captured the function earlier.

- `$.ctor(name, next => (call, ctx) => content)` overrides (or defines) a
  constructor; it returns the trampoline. `text`, `seq`, `error`, `node` are
  sealed.
- `$.region(name, (body, ctx) => content)` defines a constructor with a Body
  param; `ctx.args` is the header's options, `ctx.next(body, {args})`
  delegates (to the default region when nothing was defined before). It
  returns the trampoline, so `#let callout = $.region('callout', …)` makes
  `#callout({…})[…]` callable.
- `$.fence(tag, (body, ctx) => content)`; `ctx.next(body)` delegates (to the
  tag's code block).
- `$.bib.format = (entry, std) => content` is the `format` entry `bib`.
- `$.std`: `base` (the built-in constructors, which an override does not
  reach — `figure` builds its image with `base.image`), `plain`, and
  `manifest()`.

An entry the document defined runs in a *hook frame*: its result goes
through `toContent`; a throw becomes an error node in place with a
diagnostic (`ctor-error`, or `region-error` when it ran as a region); the
style stack returns to the frame's entry height; nesting deeper than 64 is
`hook-recursion`. Built-in entries run without a frame (they cannot throw on
their own; a block frame is still around them).

Region and fence handlers may be async: a REGION op, like a FENCE, always
awaits (lowering-design §2), and a handler's promise is contained when it
settles — one contract for every hook. A synchronous result allocates no
promise. Inline constructor overrides stay synchronous (markup inside a
paragraph is built in one pass); a region constructor called from code
returns its handler's promise when that handler is async, to be awaited —
`#(await callout({label: "c"}, para("…")))`.

## 4. Markup and regions

The LowerProgram's CALL ops carry their attributes bound by name
(`CALL heading level=2 label=null`), and the interpreter hands them to the
registry entry without the binder (`stdlib.call`). Sugar and explicit calls
therefore meet in the same entry: `*x*` ≡ `#strong[x]`, before and after a
`$.ctor('strong', …)` (fixture `lower/ctor-override`).

A region `#!name(args) … #name!` calls the constructor `name` when its spec
takes a Body, else the default region, a `group` of role `name` with the
interior as blocks (P2-07: the header's options other than `label` and the
style keys become its EXT data — an element instance, as the constructor
`$.element` returns builds it). The built-ins `table` and `figure` are such
constructors, registered like any other; nothing dispatches on their names
any more. The header's `style: {…}` (plan P2-08; it replaced sniffing the
style keys `font`, `lang`, `color`, `sizePx`) is the result's own style
change — the universal `style` attribute when the result is one node, else a
`styled` wrapper — and never reaches the handler; a style key at the top of
a default region's header is a `ctor-arg` warning with the fix. A Body is
`{blocks(), rows()}`: blocks() is the interior as blocks (the rows of a
table paragraph rejoined with ` | `; a constructor's inline content kids as
one paragraph, so `#!f(H) x #f!` ≡ `#f(H)[x]` — fixture `lower/region-call`),
rows() the table's reading of it.

## 5. Universal options, EXT and declarations (as built in P2-05)

Every kind's constructor takes the universal attributes as options (`label`,
`role`, `slot`, `syn`, `copy`, `class`; document-model §4.3) and two
universal options more: `ext: {name: scalar}` (EXT data; a name outside
`[a-z][a-z0-9-]*` or a non-scalar value is a `ctor-arg` warning) and
`style: {patch}` (P2-08: the result's own style change, its universal `style`
attribute — a delta node; a text result is wrapped in a `styled` node).
`field(name, {of})` builds the `field` placeholder.
`$.declare(type, name, data, ...templates)` writes a declaration (DECL) of a
schema `decls` type at the current point of the flow: `data` is EXT, the
templates are content (style-neutral until used).

As built (P2-07): the semantic constructors — `counterUpdate(name, {set,
step, add, numbering, supplement})` (a derived constructor of the `event`
kind; `$.counter.update`), `slot(name, {or})`, `when(of, …kids)`,
`each(of, {sep})`, `entry({role, key}, …kids)`, `ref(target, {form,
supplement})`, `collect({what, cited})` — and the declaring functions
`$.element`, `$.counter`, `$.counter.system`, `$.collector` (canonical
registry rows, docs/semantics-design.md §6) and the `$.labels.import`
placeholder. The declaring functions are write-only: `$.element` and
`$.collector` return constructors, never numbers.

## 6. Manifest

- static: `staticManifest()` (stdlib.mjs) — every built-in constructor's
  name, params, options, Body, nullary, sealed. The VS Code extension offers
  the Body constructors as region names (`features.regionNames`), together
  with the regions the document registers (`$.region("name", …)`) and those
  it uses.
- runtime: `$.std.manifest()` adds what the document defined so far, and
  the fence tags.

## 7. Deltas from the design (T2 S6)

- The Body is eager: the interpreter builds the interior before the
  constructor runs (as the recorded documents require), and blocks()/rows()
  return arrays — awaiting them works, and keeps working when the Body
  becomes lazy (P2-13).
- `m```'s markup re-entry is P2-13, so "m`*x*` ≡ `*x*`" is checked there.
- The universal options (role, label, slot, ext, style) and `field` came in
  P2-05 (§5).
- Attribute values are checked by the ops reader (`ops-arg`), not by the
  binder; the binder checks names and, for positional params, types.
- The legacy header style keys still scope every region (the design limits
  them to the default region; that, and `ownsStyle`, come with T4's style
  work).
