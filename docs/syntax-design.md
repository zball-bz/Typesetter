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

- `SYNTAX_VERSION(n)` (4 since P1-08) — raised whenever a row changes what a document means
  or how the AST prints. It is part of the ABI handshake (`tsr2_abi()` →
  `syntaxVersion`; `runtime/src/shared/abi.mjs` refuses an engine whose
  version differs from the runtime's generated one).
- `CLASS(name, ranges)` — byte classes, generated as predicates.
  `isSpliceHead`/`isSpliceCont` drive the splice and reference lexers (P1-06);
  the id and escape classes take over with the unified label grammar (P2-06)
  and the escape rule (P3-33).
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
  `$str` is the node's own string. A trailing `?` on a field's type
  (`rawmap:str?`, plan P2-04) makes the JSON AST omit it when empty.
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
`MathP{display, label}`, `SpliceP{expr, lastCall}` (the joined JS text and the offset of its trailing call),
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

## 5. The inline lexer (as built from plan P1-06)

`inline/inline.cc` parses a leaf (a paragraph, heading or table cell)
over its **joined text** (`syntax/cursor.h`, `LeafText`): the leaf's line
slices joined by one `\n`. A join is structural — CRLF, trailing blanks and
container prefixes (`> `, list indentation) between the lines never reach the
lexer — and every scan runs over this view (or a prefix of it), so no scan can
leave its leaf. When the lines are adjacent in the source the view is the raw
text itself; offsets map back to raw offsets for every span.

A Text node records its **cooked→raw map** (plan P2-04; payload `TextP{rawmap}`,
`"c:r,…"` with raw offsets relative to the span start) whenever its cooked
string is not positionally its raw slice: an escape (`\*`: one cooked byte for
two raw), a run of blanks collapsed to one space, a line join across a
container prefix or indentation. Breakpoints are (cooked offset, raw offset)
with identity between them; the transport to text content nodes is
document-model §4.3.

Dispatch is the INLINE rows: `inlineOpener(t, i)` (generated) names the rule
whose literal opener starts at `i`, longest first, and `kInlineOpenerByte`
marks the bytes that can start one — runs of other bytes are copied as plain
text in one step (87K bench: parse 0.43 → 0.19 ms). Rule behaviour is code:

| rule | behaviour |
|---|---|
| `code` | a run of N backticks closes at the next run of exactly N; joins read as spaces; one leading and one trailing space are stripped when both are present; an unclosed run is literal as a whole |
| `math` | to the next unescaped `$` within the bound; `\$` is decoded, every other `\x` passes to the math parser; padded is display; a ` <id>` right after the closer is the label |
| `comment` | `%--` … `--%`, nesting, within the bound; unclosed is literal with `parse-inline` |
| `splice` | `#(` JS `)` or a head chain `ident ('.' ident \| '(' JS ')')*` (JS scanned by `jslex.h` across joins); directly adjacent `[…]` are content arguments; `;` ends it. The payload keeps the joined JS text (`SpliceP.expr`), so a splice in a quote compiles without the `> ` prefixes |
| `strong`, `em` | strict pairs: an opener needs a glyph after it, a closer a glyph before it and an open frame of its marker on top |
| `link` | `[text](url)`, see brackets below; the URL is a plain paren match on its line |
| `note` | `^[…]` content body |
| `ref`, `refs` | `@id` (not after an identifier character); `@[a, b]` an id list on one line (`\]` escapes; ids trimmed and joined by `, `) |

**Atoms and brackets.** `syntax/lexer.h` holds the primitives the parser,
the bracket counter and the region cell splitter share: `lexCodeSpan`,
`lexMath`, `lexComment`, `lexSplice` and `atomEnd` (escape, code span, math,
comment, splice head with its JS). A bracket body — link text, a content
argument, a note — closes at the `]` its **island-aware** match finds (atoms
skipped: `` #strong[code `a]b` here] ``, `[range $[0,1)$](u)`). When an
island would swallow that closer, the body falls back to the **plain** match
(escapes only) and the islands inside it are bounded by it, so
`[price $5](u) and $x$` stays a link followed by a formula (the plan P0-04
acceptance). `BracketMatcher` memoises one scan per opener and records every
bracket it passes, so matching is linear. (The design's weak `[` frames on
the inline stack were not adopted: they lose that acceptance case, let
emphasis win over links — `*a [b* c](u)` — and change today's text node
boundaries; see PROGRESS, deviations.)

**Cells.** `splitCells` cuts a region line at `|` outside atoms and outside a
splice's content arguments.

## 6. The block automaton (as built from plan P1-07)

`linepass/linepass.cc` walks physical lines under one container protocol:

| shape | container | continues on |
|---|---|---|
| Prefix | quote | lines with its `>` prefix (a blank line ends it) |
| Column | list item | blank lines, and lines indented to its content column |
| Explicit | region | every line until its named closer |

Columns are computed in one place: a tab advances to the next multiple of 4
(App B). Every non-blank line a container continues extends its span (and its
list's), so containers cover all their lines.

Per line: match the open containers (outermost first); a verbatim **carry**
(a fence, a block comment, or a comment owned by the open paragraph) takes the
line while the containers it opened in continue, and container exit ends it
(`unterminated fence` / `unterminated comment`). Otherwise unmatched
containers close (a region closed this way reports `region-unclosed`), a
`#name!` line closes the innermost open region of that name — regions opened
inside it end unclosed — and the starters and the leaf rules run:

- **Interruption.** Headings, fences, regions, quotes, rules and statements
  always interrupt a paragraph; a list item only when it is not empty, and an
  `N.` item only when N is 1 (`1984. Then` continues the paragraph).
- **List identity** is (marker class, column): `-`, `+` and `N.` lists never
  merge; an `N.` that does not continue the numbering gets an info
  diagnostic (`list-number`).
- **Fences** dedent their content by the opener's indentation relative to the
  container's content column (no double dedent in items). A fence inside a
  quote or item passes each body line's source offset to its handler
  (`ctx.lineOffsets`, beside `ctx.offset`).
- **Comments.** A `%--` line while a paragraph is open belongs to the
  paragraph (an inline comment: invisible); elsewhere it is a block comment
  whose body is its lines with container prefixes stripped.
- **Statements.** `#let …` (to the end of the line or `;`) and `#{…}`
  (balanced) continue over the following lines of their containers — EOF at
  the root, container exit or the enclosing region's closer inside one. The
  lines are joined structurally and scanned in doubling windows (linear in
  the statement's length). A broken statement is an Error block up to the
  first blank line, the first line that starts a block, or container exit.
- **Line remainders.** Text after `--%`, `}` or `;` on the same line re-enters
  (another comment or statement, else paragraph text).
- **Orphan closers.** A `#name!` line with no open region of that name is an
  Error block (`region-orphan`), never a splice.

## 7. Line ownership and content bodies (as built from plan P1-08)

**Ownership.** Each paragraph line is scanned with the inline lexer's
primitives (`atomEnd`'s rules, `lexSplice`, the bracket counter). When the
line leaves a construct open, the line pass looks ahead once to the
construct's structural bound:

| owner | constructs | bound |
|---|---|---|
| Leaf | code spans, math islands, splice JS (`#(`, `#f(`), inline-form content bodies (`#f[x…`, `^[x…`) | a blank line or container exit |
| Container | inline comments (`%--`), block-form content bodies (`#f[` ending its line) | container exit (EOF at the root) |
| none | emphasis pairs, link text | — |

If the construct closes before its bound, the lines up to its closer belong to
the paragraph and no block starts on them: a formula over `- b` and `+ c`
lines, a call whose arguments wrap onto a `- 1` line, a comment opened
mid-line that hides `= …` and `- …` lines. Otherwise the opener is literal
text — the skeleton records it (`SkelNode::literalAt`, which phase 2 honours)
and a `RevertedWindow` {opener line, bound line} for incremental re-lexing —
and the scan continues after it, so `A stray $ sign⏎= Heading` keeps its
heading. Lookahead runs in doubling windows; no line is processed twice. A
paragraph of n lines whose openers never close costs O(n²) in the worst case
(32 ms for a 4 KB adversarial input), real documents nothing measurable.

**Block-form bodies.** A content body whose `[` ends its line (blanks or a
comment after it) closes at the first line whose first non-blank character is
`]` at or left of the opener line's indent — a `]` in prose never closes it,
and nested block bodies must be indented. Its lines keep their indentation;
the remainder of the closer line continues the paragraph (`][` starts another
argument). The skeleton records the body (`SkelNode::bodies`) and phase 2
uses that closer instead of the bracket counter.

**Blocks mode.** Every content body — `#f[…]` arguments and `^[…]` notes,
inline or block form — re-enters the line pass (`linepassLines` over the
body's lines, common indentation stripped: App B rule 4) and the AST builder.
A body that is one paragraph unwraps to its inline content, so single-line
bodies keep their old shape; `#quote[- x]` is a list. Several blocks lower to
a `seq` of blocks, which the model's normal form splices into the enclosing
block (document-model §3, N3); footnotes keep block bodies as blocks.

`@id[…]` supplements and `#let x = […]` content literals become content
bodies with their owning steps (P2-06, P2-12).

## 8. Front-end exports and grammars (as built from plan P1-09)

`syntax/exports.{h,cc}` — pure functions of a source (a scratch parse) or of
a parsed document:

- `syntaxTokens`: the 14-tag tokens — markers keyword, splice heads and
  statements function (call arguments embedded), code string, math type,
  references constant, labels label, emphasis and notes attribute, links
  property, cell bars operator, rules punctuation, fence bodies embedded,
  comments comment.
- `outlineJson`: headings (level, title, label), regions (name, `label:`
  argument), fences (lang), labels (heading / region / math) and the
  diagnostics, all with byte spans.
- `astJson`: the CallAST, one object per node; its members come from the
  generated `jsonAstNode` (the same payload rows as the dump).

C ABI `tsr_syntax_tokens` / `tsr_outline` / `tsr_parse_json` (stateless,
JSON out); `tsrc --stage=tokens|outline|astjson` and their goldens for
every fixture. ```` ```tsm ```` blocks are tokenized by the engine at
Resolve (code-design §2). The VS Code extension runs the engine in its host
(editor-design §5).

**Grammars.** `grammar/tree-sitter-tsm/syntax-regex.js` derives the regex
sources of the hand-written grammars from `syntax.gen.json`;
`tools/gen-grammars.mjs` (in gen-all) builds the TextMate grammar and
vendors `tree-sitter generate`'s parser into `third_party/grammars/tsm`. The
highlight query has one copy, `grammar/tree-sitter-tsm/highlights.scm`.
Conformance (b): `unitTokenConformance` compares tree-sitter's and the
engine's tags byte by byte over every fixture; differences are allowed only
in listed classes (splice JS, strict pairs and opaque footnotes, multi-line
islands, container-unaware fences) and agreement must stay ≥ 85% (88.9% at
P1-09). Conformance (e): `fuzz_inline` runs the exports and checks token
order.

## 9. Next steps

- P1-09: editor grammars from `syntax.gen.json`.
- P2-11 / P2-13: region provenance and splice bodies delete the legacy
  `row`/`cell`/`arg` slots.
- P2-12: keyword forms from `KEYWORD`.
