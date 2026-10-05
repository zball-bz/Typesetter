# Surface syntax: syntax.def and the CallAST (design; as built from plan P1-05)

The `.tsm` front end — line pass, inline parser, AST, codegen — reads its
lexical and structural facts from one table and produces one generic tree.
Design source: `docs/remediation/design/T1-surface-frontend.md` (SyntaxTable,
CallAST, step S2). Generated reference: `docs/syntax-table.md`.

## 1. syntax.def

`engine/src/syntax/syntax.def` is the single source. `tools/gen-syntax.mjs`
(run by `tools/gen-all.mjs`, checked by gate G9) generates:

| output | contents |
|---|---|
| `engine/src/syntax/syntax.gen.h` | `SYNTAX_VERSION`; one predicate per character class (`isSpliceHead`, `isIdStart`, `isEscapable`, …); `SugarId` and `kSugarName`; the payload structs; `reservedSpliceHead`; `kTokenTags` |
| `engine/src/syntax/syntax.gen.cc` | `dumpAstNode`, the generic dump (§3) |
| `runtime/src/shared/syntax.gen.mjs` | `SYNTAX_VERSION`, `TOKEN_TAGS` (the worker's token provider) |
| `runtime/src/shared/syntax.gen.json` | every row, for tooling (editor grammars, plan P1-09) |
| `docs/syntax-table.md` | the rows as tables |

Rows are data; behaviour is a closed set implemented in code. The row kinds:

- `SYNTAX_VERSION(n)` — raised whenever a row changes what a document means
  or how the AST prints. It is part of the ABI handshake (`tsr2_abi()` →
  `syntaxVersion`; `runtime/src/shared/abi.mjs` refuses an engine whose
  version differs from the runtime's generated one).
- `CLASS(name, ranges)` — byte classes. Generated as predicates; the
  table-driven lexer (plan P1-06) consumes them.
- `INLINE(id, open, close, body, params, guard, prec, slot, capture)` — inline
  delimiters. The body mode (`Verbatim`, `Pair`, `CallChain`, `LinkText`,
  `Content`, `Ident`, `IdList`), the guard (`Intraword`, `PrevIdent`) and the
  precedence (`Island` > `Comment` > `Markup`) name behaviour the lexer
  implements; `slot` is the sugar the form produces and `capture` the
  highlight class editors give it.
- `BLOCK(id, shape, starter, interrupts, ownership, slot)` — block forms.
  Shapes `Prefix` / `Column` / `Explicit` / `Verbatim` / `Leaf` / `Stmt` are
  the container protocol of the block automaton (plan P1-07).
- `KEYWORD(word, params…)` — keyword forms (`#if`/`#for`/`#while`, plan
  P2-12).
- `RESERVED_UNSUPPORTED(…)`, `RESERVED(…)` — words a bare splice head may not
  be (`reservedSpliceHead`: `keyword-unsupported` / `reserved-word`, plan
  P0-05).
- `TOKEN_TAGS(…)` — the highlight tag set shared by the engine's token fold
  (`code/tokens.h`), the native and worker token providers and the editor
  grammars. It used to be two hand-synchronised lists.
- `SUGAR(slot, form, payload, dump)` — the built-in calls (§2). The payload
  is a field list `name:type` with type `u8 | bool | i32 | u32 | str` (an
  interned string) `| src` (a source span); the dump format is a template:
  `{field}` prints a value, `"…{field}…"` an escaped string, `{?field …}` its
  body only when the field is set, `{field?yes:no}` picks by a boolean, and
  `$str` is the node's own string.
- `NODE(kind, payload, dump)` — the same for the non-call kinds.

## 2. The CallAST

`engine/src/ast/ast.h`. Everything built in is a **Call** whose meaning is
its slot; a user call is a **Splice** (its callee is JS text); statements are
**Stmt**; malformed input is **Error** (`str` = the code, payload = message).

```
enum class AstKind : u8 { Doc, Text, Comment, Call, Splice, Stmt, Error };
struct AstNode {            // 32 bytes native (static_assert), 28 in wasm32
  AstNode** kidv; u32 nkids;              // kids(): an arena slice
  AstKind kind; u8 flags; SugarId sugar;  // sugar: Call only
  Span span; StrRef str; u32 side;        // side: bytes of the side record
};
```

A node's typed payload — `HeadingP{level, label}`, `ListP{ordered, start}`,
`FenceP{lang, args, bodyOffset}`, `RegionP{args}`, `LinkP{url}`,
`MathP{display, label}`, `SpliceP{expr, lastCallStart}`,
`StmtP{let, js}`, `ErrorP{message}` — is a **side record** allocated in
the same arena allocation right behind the node (`AstAlloc::node<P>`), read
with `side<P>(n)`. Nodes whose slot has no payload (para, item, quote, rule,
strong, em, code, note, ref, the legacy slots) pay nothing. `str` carries the
one string most forms have (text, comment body, code, ref target, math
source, fence body, region name, error code).

Mapping from the per-feature kinds it replaced:

| before | CallAST |
|---|---|
| `Para`, `Heading`, `ListB`, `Item`, `Quote`, `Rule` | `Call{para, heading, list, item, quote, rule}` |
| `CodeBlockB` | `Call{fence}` |
| `Region` | `Call{region}` |
| `Styled '*'` / `'_'` | `Call{strong}` / `Call{em}` |
| `Code`, `Link`, `Note`, `Ref`, `Math` | `Call{code, link, note, ref, math}` |
| `SpliceArg`, `Row`, `Cell` | `Call{arg, row, cell}` — legacy shapes until plans P2-13 (splice bodies) and P2-11 (region provenance) |
| `CodeStmt` | `Stmt` |
| `Splice`, `Text`, `Comment`, `Error`, `Doc` | unchanged kinds |

The AST takes no placement decision: padded math is `Call{math, display=1}`
wherever it stands. Front-end translation units (`linepass`, `inline`,
`fragment`, `syntax.gen.cc`, `codegen`) compile with `-Werror=switch-enum`
and have no `default:` arms, so a new kind or slot fails the build until every
consumer handles it.

**Size.** The old node was 64 bytes plus an `std::vector` of kids. Over the
fixtures the AST now takes 49,888 bytes for 1,214 nodes where the old layout
took 86,624 (`unitAstBytes` in the native runner checks it never exceeds the
old layout's cost); on the 87K bench document 64,664 bytes instead of
116,680 (−45%).

## 3. One generic dump

`tsrc --stage=ast` prints `dumpAst`: two spaces per depth, then
`dumpAstNode` — `<name> @[start,end)<fields>` from the row's dump template —
for every node. The templates reproduce the old per-kind output byte for
byte (`styled marker=*`, `codeblock lang=… body=…`, `code-let`/`code-block`,
`arg`/`row`/`cell`), so no `.ast` golden changed.

## 4. Codegen

`codegen.cc` dispatches on the kind and then on the slot: one adapter per
slot prints today's constructor call (`__p`, `__hd`, `__l`, `__fence`,
`__region`, …), so the program text is byte-identical. The paragraph that is
exactly one display formula still prints `__mb` (the interim L1 rule; T2's
normalisation retires it). `fragment.cc` (inline fragments: sidecars,
`m.parse`) converts the same tree to content nodes.

## 5. Next steps

- P1-06: the table-driven lexer reads `CLASS` and `INLINE`.
- P1-07: the block automaton reads `BLOCK`.
- P1-09: editor grammars from `syntax.gen.json`.
- P2-11 / P2-13: region provenance and splice bodies delete the legacy
  `row`/`cell`/`arg` slots.
- P2-12: keyword forms from `KEYWORD`.
