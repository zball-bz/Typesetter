# T1-surface-frontend

> **Audit design input (generated, English).** Produced by the 2026-10-05 audit (architect → two adversarial critics → revision). Where it conflicts with `docs/remediation/PLAN.md` (decision register §3) or with `INTEGRATION.md` (conflict resolutions), PLAN.md wins, then INTEGRATION.md. Step ids are mapped to plan steps in the Migration section. Line numbers refer to commit ecc3a89.

## Thesis

The front end recognises syntax and records it without loss. It decides nothing that belongs to a later layer, and inline delimiter pairing never decides block structure.
- One table, engine/src/syntax/syntax.def, holds the lexical classes, the delimiter rows (opener, closer, and a body mode from a closed set), the block rules, the keyword forms, and each sugar's slot and payload schema. It contains no JS; lowering belongs to T2.
- One island-first lexer is shared by phase 1, phase 2 and every tool. Phase 1 lexes each paragraph line once into an atom tape.
  - Line ownership means that block starters on following lines are suppressed. Only verbatim islands, JS bodies and content bodies get it. Each has an explicit structural bound and commits tentatively, reverting at the bound if no closer appears.
  - Emphasis, link text and id lists are phase-2 only.
- The output is a compact generic Call AST.
  - Built-in sugar is a Call with a slot. A user call is a Splice of the same shape whose callee is JS text. A statement is a Stmt, and malformed input is an Error.
  - Provenance is zero-width side data, not extra nodes: exact spans, cut points at `|` and line joins, line columns, and cooked-to-raw maps.
- One argument grammar gives one equivalence: `#!name(H) … #name!` ≡ `#name(H)[…]`, and a fence `name(H)` passes the same H to its handler.
  - A named-only argument list is the opts object.
  - Any other argument list is verbatim JS.
- One scope rule: `#let` binds in the nearest statement scope, which is the document or a keyword-form body. Content opens no scope, whether built-in or user.
- One re-entrant entry, parseContent, serves documents, content bodies (always parsed as blocks, then a sole paragraph unwraps), fragments (m`…`/m.parse with out-of-band holes) and tools. The inverse direction comes from the same table: tokens, outline, AST JSON, printer and escaper.
Decisions the parser makes today move to the layers that own them:
- `|` cells to the table constructor (T2);
- CJK joins and note attachment to T5;
- display placement and list grouping to L3 (T2);
- label registration to T3.

## Diagnosis

The ad-hoc items reduce to seven root causes.

(1) There is no shared lexical layer. Delimiting is re-implemented for each construct:
- five bracket matchers (inline.cc:114, :130, :387, :411, :477);
- two comment scanners (linepass.cc:203, inline.cc:240);
- two splice-head scanners (inline.cc:170, jslex.h:97);
- four `$` lexers.
v2 §5's 'islands first' therefore holds only by branch order.

(2) No rule says which constructs may own the following physical lines. The answer today is whichever raw scan runs first: `hardEnd = all.size()` (inline.cc:245, :269) and contiguous() returning true past the last span (inline.cc:68-72). Content either leaks out of its block or is forbidden: single-line content args (inline.cc:204), and multi-line #let only when `open.empty()` (linepass.cc:364/376).

(3) The parser makes decisions that belong downstream:
- `|` cells for every region (inline.cc:601-627);
- CJK joins on raw bytes (inline.cc:48-59);
- moving the note space (inline.cc:384-397);
- display placement by AST shape (codegen.cc:95-117).

(4) The AST mirrors features. It has 22 kinds with overloaded fields (ast.h:6-24), read by three switches with `default:` arms (codegen.cc:198, fragment.cc:89, and a dump that lacks Note). fragment.cc is a second lowering.

(5) Errors cannot be represented. `({3})`, `val((if))` and `f(a,, c)` are pasted into the module as invalid JS.

(6) Arguments and statements have no single rule:
- region headers are pasted object bodies (codegen.cc:150-162);
- splice arguments are edited as text (codegen.cc:73-84);
- statements are legal only at the document root (codegen.cc:214-229);
- keyword forms are unimplemented.

(7) The language has no inverse and no export. Tokens, outline and escaping are re-derived by tree-sitter, TextMate, extension regexes, the converters and translate-tsm, and these copies already disagree with the engine.

## Abstractions

### SyntaxTable (syntax.def)

**owner_layer**

L0: engine/src/syntax/syntax.def (X-macro, same pattern as ops.def). It generates engine/src/syntax/syntax_gen.h, plus runtime/src/shared/syntax.gen.json (constants for tooling) and docs/syntax-table.md via tools/gen-syntax.mjs.

**purpose**

The single source of truth for the surface language, limited to lexical and structural facts:
- character classes;
- delimiter rows, each with a body mode from a closed set;
- block rules and their paragraph-interruption policy;
- keyword and reserved words;
- one row per sugar, giving its slot name, typed payload schema, dump format and highlight capture.
Behaviour lives in code as a closed set of body modes, ownership classes and block shapes; rows are data. Parameters that only one row uses are documented parameters of a body mode, not free-floating flags. There are no JS templates: how a slot is called is T2's lowering convention, so switching to T2's uniform constructor convention never edits L0.

**definition**

```
// engine/src/syntax/syntax.def   SYNTAX_VERSION(1)
CLASS(SpliceHead,'A-Za-z_$') CLASS(SpliceCont,'A-Za-z0-9_$')    // App A l.330 / jslex.h:90-92; also region names (as today, linepass.cc:304-306)
CLASS(IdStart,'A-Za-z_') CLASS(IdCont,'A-Za-z0-9_') CLASS(IdJoin,'-.:')   // bare ref := IdStart IdCont* (IdJoin IdCont+)*
CLASS(LabelChar, NOT(White_Space | '<>[]@,;' | BACKSLASH))      // <id> and @[..]: any Unicode incl. CJK, never whitespace
CLASS(Escapable, ASCII_PUNCT)   // '\'+Escapable -> literal; '\'+EOL -> linebreak; '\'+anything else -> the backslash stays

// Body modes: a CLOSED set implemented in code; their parameters are documented here.
enum class Body : u8 {
  Verbatim,   // params: closer, nest, esc set, pad (None | Strip | Display)
  Js,         // params: closer (')' | '}' | EOL-at-depth-0-or-';'); jslex frames
  CallChain,  // head class; '.ident', '(' Js ')', '[' Content ']' continuations; ';' terminator; ElseChain for keyword heads
  Pair,       // strict pairing (v2 §5)
  LinkText,   // weak '[' on the inline stack; becomes a link only on '](' url ')'
  Content,    // '[' .. ']' balanced over non-escaped, non-atom brackets; body -> parseContent(Blocks) + sole-paragraph unwrap
  IdList,     // '@[' ids: only ']' ',' '\' are significant
  Ident };    // '@' bare id
// Line ownership (phase 1) is a property of the body mode and form, never of a sugar row:
enum class Own : u8 {
  None,       // Pair, LinkText, IdList, Ident: may cross a join inside the leaf (phase 2 only); never suppress starters;
              //   open entries flush as literal at a structural boundary
  Leaf,       // inline Verbatim islands (code, math), inline Js ('#f(', '#(', keyword heads), inline-form Content
              //   ('#f[x..', '^[', '@id['): suppress starters until the closer; bound = end of the leaf (blank line / container exit)
  Container   // comments, statements ('#{', '#let'), block-form Content ('[' ends the line; closer = a ']'-led line at
              //   col <= the opener line's indent), verbatim blocks: bound = container exit (EOF at the root)
};  // Every Own != None carry is TENTATIVE: no closer within the bound -> revert (see BlockAutomaton step 4).
enum class Shape : u8 { Prefix, Column, Explicit, Verbatim, Stmt, Leaf };
enum class Interrupt : u8 { Always, Never, LeafOwned, ListRule /* '-'/'+' only if non-empty; 'N.' only if N==1 */ };
GUARD(PrevIdent)  // '#' bare VALUE head (no '(' or '[' continuation) and '@': literal right after [A-Za-z0-9_$]
GUARD(Intraword)  // '*' and '_': literal between two ASCII alphanumerics

//      id       open          close          body       params                         guard      prec     slot       capture
INLINE(code,     RUN('`'),     SAME_RUN,      Verbatim,  pad=Strip,                     -,         Island,  code,      'string')
INLINE(math,     '$',          '$',           Verbatim,  esc={'$'} pad=Display suffix,  -,         Island,  math,      'embedded')
INLINE(comment,  '%--',        '--%',         Verbatim,  nest,                          -,         Comment, comment,   'comment')
INLINE(url,      SCHEME '://', URL_END,       Verbatim,  -,                             -,         Island,  link,      'link')
INLINE(splice,   '#' HEAD,     HEAD_CHAIN,    CallChain, -,                             PrevIdent, Markup,  -,         'function')
INLINE(kwform,   '#' KEYWORD,  HEAD_CHAIN,    CallChain, elseChain,                     -,         Markup,  -,         'keyword')
INLINE(strong,   '*',          '*',           Pair,      -,                             Intraword, Markup,  strong,    'strong')
INLINE(em,       '_',          '_',           Pair,      -,                             Intraword, Markup,  em,        'emphasis')
INLINE(link,     '[',          '](' URL ')',  LinkText,  -,                             -,         Markup,  link,      'link')
INLINE(note,     '^[',         ']',           Content,   -,                             -,         Markup,  note,      'note')
INLINE(ref,      '@',          BARE_ID,       Ident,     supplement='[' (gated on T3),  PrevIdent, Markup,  ref,       'label')
INLINE(refs,     '@[',         ']',           IdList,    -,                             -,         Markup,  ref,       'label')
INLINE(brk,      '\' EOL,      -,             -,         -,                             -,         Markup,  linebreak, 'punctuation')

//    id       shape     starter                        interrupts  own        slot
BLOCK(quote,   Prefix,   '>' ' '?,                      Always,     None,      quote)
BLOCK(item,    Column,   LIST_MARKER,                   ListRule,   None,      item)
BLOCK(region,  Explicit, '#!' SpliceId ARGS? SUFFIX?,   Always,     Container, region)
BLOCK(fence,   Verbatim, RUN('`',3) INFO SUFFIX?,       Always,     Container, fence)
BLOCK(comment, Verbatim, '%--',                         LeafOwned,  Container, comment)
BLOCK(front,   Verbatim, '---' AT_OFFSET0,              Never,      Container, comment)   // exists only if FrontEndOptions.frontMatter
BLOCK(heading, Leaf,     '='{1,6} ' ' .. SUFFIX?,       Always,     None,      heading)
BLOCK(rule,    Leaf,     '-'{3,} EOL,                   Always,     None,      rule)
BLOCK(let,     Stmt,     '#let' WS,                     Always,     Container, -)   // RHS: Js (EOL at depth 0 or ';') | Content literal
BLOCK(stmt,    Stmt,     '#{',                          Always,     Container, -)
BLOCK(use,     Stmt,     '#use' '(',                    Always,     None,      -)
BLOCK(para,    Leaf,     DEFAULT,                       -,          None,      para)
KEYWORD(if, elseChain) KEYWORD(for) KEYWORD(while)
RESERVED(if, else, for, while, do, switch, try, use, let, const, var, function, class, return, new, typeof, await, yield)

// Sugar rows: slot + typed payload + dump format.  NO JS: T2 lowers a slot by its own convention.
SUGAR(para,'Block',P())  SUGAR(item,'Block',P())  SUGAR(quote,'Block',P())  SUGAR(rule,'Block',P())
SUGAR(heading,'Block',P(level:u8), LABEL)
SUGAR(list,'Block',P(marker:ListMarker{Bullet,Auto,Explicit}, start:i32))
SUGAR(strong,'Inline',P())  SUGAR(em,'Inline',P())  SUGAR(code,'Inline',P(str:Str))  SUGAR(linebreak,'Inline',P())
SUGAR(link,'Inline',P(url:Str))  SUGAR(note,'Inline',P())
SUGAR(ref,'Inline',P(targets:IdList), KIDS(supplement))
SUGAR(math,'Both',P(src:Str, display:bool), LABEL)          // display = padding rule (Typst); placement is L3's (T2)
SUGAR(fence,'Block',P(tag:Str, info:Str), ARGS, LABEL, BODY(lines))
SUGAR(region,'Block',P(name:Str), ARGS, LABEL, KIDS(blocks))
// Consumers: lexer/linepass first-byte dispatch; the AstNode payload union; the generic dump;
// T2's lowering (slot + payload); gen-syntax.mjs -> syntax.gen.json (classes, delimiters, escape set, keywords,
// reserved words, slot names, the 14 token tags shared with tokens.h) + docs/syntax-table.md.
```

**surface**

No new syntax by itself. User-visible effects:
- One escape rule outside verbatim islands: backslash + ASCII punctuation gives the literal character; backslash at end of line is a line break (the `hardbreak` kind already exists, ops.def:38); backslash before anything else stays a backslash. Today `\x` silently loses its backslash and `\` before end of line prints a literal backslash (verified with tsrc); no fixture uses either.
- `H#sub[2]O` stays a splice; `word#todo` is literal (the guard applies only to a bare value head).
- A generated syntax reference in docs/syntax-table.md.
- Optional (S15): rows declared in config.

**replaces**

- engine/src/inline/inline.cc:219-447 (first-byte if-cascade whose branch order is the only encoding of v2 §4.2/§5 precedence)
- engine/src/linepass/linepass.cc:253-386 (leaf if-cascade)
- engine/src/inline/inline.cc:407,416 (raw-byte '@' lookbehind; ref charset borrowed from jslex)
- engine/src/code/tokens.h:10-17 + runtime/src/worker/tokens.mjs:5-8 (14 tags kept in sync by hand)
- grammar/tree-sitter-tsm/grammar.js:40-98 constants (comment, math, footnote, label and cell regexes), now read from syntax.gen.json; the structural approximations stay hand-written and conformance-tested
- tools/convert/html2tsm.mjs:68, pbr2tsm.mjs:499, wiki2tsm.mjs:108 (copied partial escapers)
- docs/architecture.md:15,57-61,175 (a PackCC grammar that does not exist)

### SurfaceLexer (SpanCursor, AtomTape, two-level inline structure, shared primitives)

**owner_layer**

L0: engine/src/syntax/{cursor.h,lexer.h,lexer.cc}; engine/src/inline/jslex.h becomes resumable and cursor-based

**purpose**

One island-first tokenizer driven by the SyntaxTable.
- Phase 1 lexes each leaf line once and records an AtomTape: islands, comments, splice heads, JS arguments, content bodies, escapes and holes. Phase 2 reads the atoms from the tape and scans only the markup bytes between them.
- The SpanCursor is the only way phase 2 reads source. No scan can leave its leaf, and a line join is a structural event rather than byte adjacency.
Inline structure has two levels, so a stray bracket can never block emphasis and an unmatched bracket can never swallow a body:
- Content bodies (`#f[`, `^[`, `@id[`, keyword bodies) are matched by a bracket counter that skips atoms and escapes, then parsed recursively. They are never pushed on the inline stack.
- The inline delimiter stack holds only Pair openers and weak `[` link candidates.
The lexer also provides the shared primitives: splice heads, bare ids, labels, the attribute suffix, id lists, and the single ArgList grammar.

**definition**

```
struct LineSlice { u32 start, end; u32 col0; };             // one physical line after container-prefix stripping
struct SpanCursor { const SourceText* src; std::span<const LineSlice> lines; u32 line, off;
  int peek(u32 k = 0) const;   // virtual '\n' at each join (join = next slice of the same leaf; CRLF / trailing blanks never matter)
  bool atJoin() const; bool atEnd() const;
  JsText slice(const SpanCursor& from) const; };           // SmallVec<Span> pieces joined by '\n'
struct Atom { u32 start, end; u16 rule; Own own; };         // island | comment | splice head/args/body | escape | hole
struct AtomTape { SmallVec<Atom, 8> atoms; };               // produced ONCE per leaf by phase 1
struct JsLexState { SmallVec<u8, 8> frames; u8 inString; };
struct LexState { u16 openRule = 0; u8 commentDepth = 0; JsLexState js; u32 openerLine = 0, openerPos = 0; bool empty() const; };
class SurfaceLexer { public:
  LexState skimLine(LineSlice, LexState in, AtomTape&);     // phase 1; allocation-free on the common path
  void inlineParse(SpanCursor&, const AtomTape&, AstBuilder&); };   // phase 2
std::optional<Span> lexSpliceHead(SpanCursor&);   // SpliceHead SpliceCont* ('.' SpliceHead SpliceCont*)*  (v2 §3 rule 3)
std::optional<Span> lexBareId(SpanCursor&);       // IdStart IdCont* (IdJoin IdCont+)*  ('@x.' keeps the '.' in prose)
std::optional<Span> lexLabel(SpanCursor&);        // '<' LabelChar+ '>'
std::optional<Span> lexAttrSuffix(SpanCursor&);   // ' <id>' as the LAST token of a heading / region / fence opener line, or right
                                                  // after a math island; '\<' suppresses it
IdList lexIdList(SpanCursor&);                    // '@[' id (',' id)* ']'; '\]' '\,' escapes
struct Arg { Span text; Span key; enum K : u8 { Positional, Named, Shorthand, Spread } k; };
struct ArgList { Span whole; SmallVec<Arg, 4> items; bool trailingComma, emptyModuloComments;
                 enum Form : u8 { Empty, Positional, Named, Mixed } form; };
ArgList lexArgList(SpanCursor&);   // jslex-balanced '(' .. ')', split at depth-0 commas.
// Named form: every depth-0 item is 'PropertyName: expr' | '...expr' | shorthand ident, AND at least one is 'PropertyName: expr'
//   (PropertyName := IdentifierName | String | Number | '[' Js ']').  An item of that shape is never a valid JS argument,
//   so this is a conservative extension of JS: every valid JS argument list keeps its meaning.
// Mixed (a named item next to a positional expression) -> Error 'mixed-args'.
// Inline structure:
//  * Content bodies: depth count over '[' / ']' that are not escaped and not inside an atom;
//    body -> parseContent(Blocks) + sole-paragraph unwrap.
//  * Inline stack (Pair + weak LinkText only):
//    - a Pair closer searches DOWN past weak '[' entries for its opener; skipped weak entries become literal (CommonMark 'look for opener');
//    - ']' pops to the nearest weak '[' and becomes a link only on '](' url ')';
//    - at the end of the leaf, or at a block starter that interrupts, open entries flush as literal.
// Holes (m`..${x}..`) arrive OUT OF BAND as ranges over the joined text:
//    markup context -> an atom -> Hole{i};  inside a verbatim island -> the stringified text is used.
// Math island: '\' escapes the next char for delimiting; only '\$' is decoded (to '$'); every other '\x' passes
//    to T8 verbatim (today's src bytes, inline.cc:272).
```

**surface**

- `` ``a`b`` `` multi-backtick code spans, using the fence run-length rule.
- `\$` inside math is a literal `$`.
- `\]` works in `^[..]`, `@[..]`, `#f[..]` and link text alike.
- `[range $[0,1)$](u)` is a link, and `#f[code `a]b` here]` closes after the code span.
- `*range [0, 1) only*` and `_see [sic_` keep today's emphasis (scratchpad/t/bracket.tsm).
- One label charset: `<标签>` is valid and `<my eq>` is not.
- `@sec:intro` resolves `sec:intro`, and `@x.` keeps the period in prose. `@fig-pipe` is unchanged.
- `#$.ref("x")` stays a splice.
- `#callout(kind: "warn")[..]` and `#!figure(src: s, ...opts)` share one argument grammar.

**replaces**

- engine/src/inline/inline.cc:68-77 (contiguous()/seekTo: true past the last span; byte-adjacency joins broken by CRLF and trailing blanks)
- engine/src/inline/inline.cc:114-142 (matchBracket/matchParen: island-unaware pre-matching)
- engine/src/inline/inline.cc:240-265 (inline comment scan to all.size())
- engine/src/inline/inline.cc:266-313 (math raw scan; label lookahead :292-306 allowing spaces)
- engine/src/inline/inline.cc:314-330 (single-backtick code span)
- engine/src/inline/inline.cc:384-403 (footnote bracket loop: nests, ignores escapes and islands)
- engine/src/inline/inline.cc:404-432 ('@'/'@[' raw-byte scanning; '@[' stops at the first ']')
- engine/src/inline/inline.cc:153-217 (handleSplice head chain duplicating jslex.h:97-116; content args confined to the first line :204)
- engine/src/inline/inline.cc:454-508 (splitCells masking lexer: knows code and splices, not math)
- engine/src/linepass/linepass.cc:203-229 (blockComment raw scan, the second comment scanner)
- engine/src/linepass/linepass.cc:272-283 (heading label right-to-left scan with its own charset)
- engine/src/inline/inline.cc:566-592 (fence info split at the first '('; trailing junk silently dropped)

### BlockAutomaton (container protocol, line ownership, tentative carries)

**owner_layer**

L0: engine/src/linepass/{blocks.h,linepass.cc}; a re-entrant line pass over any LineSlice list

**purpose**

One container protocol covers prefix containers (quote), column containers (item) and explicit-close containers (region). Verbatim blocks, statements and leaves all run under the same per-line container matching.
- Line ownership has one rule: a line is a continuation only while an Own != None carry is open, and Own comes from the body mode.
- Every carry is tentative, with an explicit structural bound. Block structure therefore never depends on emphasis or link pairing (v2 §4 l.97).
- Recovery is block-granular, and each opener is re-lexed at most once.
Paragraph interruption, tabs, list identity, container spans, line remainders and body hand-off are handled once, not per kind.

**definition**

```
struct LineCursor { u32 ln, pos, col; u32 columnOf(u32 p) const; };  // the ONLY column computation; tab -> next multiple of 4
                                                                     // (a language constant in App B, not a host option)
struct OpenContainer { SkelNode* node; Shape shape; u32 contentCol; StrRef name /*Explicit*/; };
struct BlockRule { u16 id; Shape shape; Interrupt interrupts; Own own;      // generated from BLOCK rows
  bool (*tryOpen)(LineCursor&, BlockCtx&);
  bool (*continues)(LineCursor&, const OpenContainer&);   // Prefix: '>'; Column: col >= contentCol (blank lines continue); Explicit: always
  void (*onForcedClose)(OpenContainer&, BlockCtx&); };    // Explicit: 'region-unclosed' + Error wrapper (App B rule 5)
struct RevertedWindow { u32 openerLine, boundLine; };     // exported for incremental re-lex (T9)
struct Skeleton { SkelNode* root; SmallVec<LexState> boundaryStates /*empty by construction*/; SmallVec<RevertedWindow> windows; };
Skeleton linepass(const ContentInput&, Arena&, DiagSink&);
Per physical line:
 1. Match open containers with continues() and extend span.end of EVERY matched container.
 2. If an Own != None carry is open, the line is a continuation and starters are suppressed. Container prefixes are still
    stripped; the dedent is relative to the container's content column.
 3. Otherwise try BlockRules by first byte. While a paragraph is open, a rule opens only if its Interrupt allows it
    ('1984. Then' continues the paragraph).
 4. Tentative carry. When a skimmed line ends with a carry open, the automaton records the opener and continues.
    Reaching the bound without the closer reverts it:
      inline opener -> literal text + diagnostic;
      statement     -> Error block from the opener to the first blank line (the recovery bound);
    then the span from the opener is re-lexed ONCE with that opener disabled, and a RevertedWindow is recorded.
    Bounds: Leaf = blank line or container exit; Container = container exit (EOF at the root).
    Hence: an unclosed '^[' / '$' / '#f[x' cannot pass a blank line; a top-level '#{ .. }' with blank lines still works,
    as today (linepass.cc:376).
 5. Block-form content body: '[' ends the opener line (only blanks or a comment after it).
    - The closer is a line whose first non-blank char is ']' at a column <= the opener line's indent. A ']' in prose can
      never close it.
    - The remainder after ']' goes first to the owning row ('else [', 'else if (', another '[', ';'), then re-enters as
      paragraph text.
 6. A closer '#name!' searches the stack BY NAME and closes intermediate containers with diagnostics.
    An unmatched closer becomes an Error leaf, never a splice.
 7. Line remainder: when a construct closes mid-line ('--%', '}', ';', ']'), a non-blank remainder re-enters at that offset.
 8. Leaf-owned comments: a line-start '%--' while a paragraph is open becomes an inline Comment of that paragraph.
 9. List identity is keyed on (marker class, column). An 'N.' discontinuity gives an info diagnostic. The marker class is
    kept on the node.
10. Body hand-off:
    - Content bodies go to parseContent after common-indent dedent (App B rule 4).
    - Js bodies become JsText pieces (multi-line #let / #{ inside containers).
    - Fence bodies inside prefix or column containers carry per-line offsets.
11. Front matter: only when FrontEndOptions.frontMatter is set, and only at offset 0. It yields a Comment-class node.
```

**surface**

Behaviour, not new syntax. Normative additions to App B:
- A tab advances to the next multiple of 4 columns.
- A comment line inside a paragraph is invisible.
- Paragraph interruption: headings, fences, regions, quotes, rules and statements always interrupt; '-'/'+' items interrupt only when non-empty; 'N.' items only when N is 1.
- '+' and 'N.' lists never merge with each other.
- A fence or comment inside a quote or item ends at the container's end.
- `#!a … #b! … #a!` resyncs at the matching closer.
- Text after `--%`, `}`, `;` or `]` on the same line is kept.
- Block-form bodies `#f[⏎ … ⏎]` may contain blank lines and any block markup. Inline-form bodies may wrap but not span a blank line.
- Emphasis and links never change block structure: `请注意*以下事项：⏎- 第一项*重要*` keeps its list (scratchpad/t/cjkstar.tsm).

**replaces**

- engine/src/linepass/linepass.cc:9-12,35-38 (OpenC stack; closeTo pops regions silently)
- engine/src/linepass/linepass.cc:48-72 (matchPrefixes: Region always matches :51; columns count spaces only :54,:66,:80)
- engine/src/linepass/linepass.cc:76-142 (tryStarters: list identity on the ordered flag only :117-130; first-line spans :85,:128,:132)
- engine/src/linepass/linepass.cc:163-200 (fence: discards the matchPrefixes result :178; absolute dedent :194; runs to EOF :198)
- engine/src/linepass/linepass.cc:257-261 (a comment line closes the leaf; the closing line's remainder is skipped :259)
- engine/src/linepass/linepass.cc:332-357 (a closer closes the innermost region even on mismatch :340-353)
- engine/src/linepass/linepass.cc:358-385 (#let/#{: open.empty() switch :364/:376; asymmetric recovery; line skipping :371/:383)
- engine/src/linepass/linepass.cc:388-392 (region-unclosed reported only at EOF)
- engine/src/linepass/linepass.h:14-25 (SkelNode fields repurposed per kind)
- editors/vscode-tsm/src/preview.js:182-186 (front-matter blanking in one host only)

### CallAST (compact generic AST + zero-width provenance side records)

**owner_layer**

L0 output: engine/src/ast/ast.h (the payload union and side-record pools are generated from SUGAR rows); one generic dump in engine/src/ast/dump.cc

**purpose**

An AST closed under the governing principle (v2 §4 l.107):
- Built-in sugar is a typed Call whose meaning is a slot.
- A user call is a Splice of the same shape whose callee is JS text.
- Statements are Stmt, malformed input is Error, and holes are Hole.
Provenance is kept without loss, but as zero-width side data rather than extra nodes, so node boundaries (and therefore emission) are the same inside and outside regions. The side data is:
- exact spans and label spans;
- per-line starts and columns;
- cut points at unescaped `|` and line joins;
- cooked-to-raw maps on Text.
The parser takes no placement or typographic decision. The common node stays small; rare data lives in side records.

**definition**

```
enum class AstKind : u8 { Doc, Text, Comment, Call, Splice, Stmt, Error, Hole /* + SoftBreak from S13 */ };
enum class SugarId : u16 { /* generated */ };   enum class StmtKind : u8 { Let, Block, Use };
struct AstNode {                       // <= 32 bytes; static_assert'ed
  AstKind kind; u8 flags; SugarId sugar; Span span; StrRef str; u32 side; AstSlice kids; };
// Side records (typed arena pools; 'side' indexes the pool of the node's kind/sugar; 0 = none):
struct CallSide   { Payload p /* HeadingP{level}, ListP{marker,start}, MathP{display}, LinkP{url}, ... */; Span label; };
struct SpliceSide { JsText callee; ArgList args; SmallVec<u32, 2> bodies; };  // bodies = content-body kid ranges
struct HeaderSide { ArgList args; Span label; Span info; StrRef name; };      // region / fence
struct StmtSide   { StmtKind k; JsText js; SmallVec<Span, 2> binds; };        // binds: names from a jslex pattern scan
struct KwSide     { u8 keyword; JsText head; SmallVec<u32, 2> bodies; SmallVec<JsText, 1> elseHeads; };
struct ParaProv   { SmallVec<LineInfo, 4> lines /*start, col*/; SmallVec<Cut, 2> cuts; };
struct Cut        { u16 kid; u32 cookedOff; u8 kind /*Sep|Join*/; bool nested; };
struct TextRaw    { SmallVec<RawMap, 2> map; };   // only when cooked != raw
Rules:
- '|' stays a literal char in Text. Each unescaped '|' outside atoms and the line join between consecutive lines is
  recorded as a Cut:
  - at frame depth 0 with its (kid, cooked offset);
  - inside a Pair or link frame with nested = true, so a constructor can diagnose instead of merging silently.
- Until S13, line joins merge into Text exactly as today: a space, or nothing between two chars of today's cjkish class
  (inline.cc:50). There is no SoftBreak node, so .ast/.js stay byte-identical.
- Padded math is Call{math, display=1} wherever it occurs (no promotion in L0). A label on any math island is kept.
- Error{code, message, kids = best-effort salvage}.
- One generic dump driven by the SUGAR dump formats, byte-identical to today's per-kind output.
  The SpliceArg/Row/Cell lines are reproduced by legacy dump formats until S9 deletes them.
- No 'default:' arms over AstKind/SugarId; front-end TUs compile with -Werror=switch-enum.
```

**surface**

No new syntax. `tsrc --stage=ast` prints the generic form, and `--stage=astjson` (S6) exports it. Provenance cuts print only for paragraphs that have them.

**replaces**

- engine/src/ast/ast.h:6-24 (22 kinds; str/aux/tag/num/expr/lastCallStart overloaded per kind)
- engine/src/inline/inline.cc:658-765 (per-kind dumpNode without a Note case; test/golden/notes/basic.ast.txt enshrines blank lines)
- engine/src/inline/inline.cc:601-627 (Row/Cell construction for every region paragraph)
- engine/src/codegen/codegen.cc:198-200 (default: text("") silently drops nested statements)
- engine/src/inline/inline.cc:48-59 (parser-side CJK join on raw bytes; removed in S13)
- engine/src/inline/inline.cc:98-106 (spaceBeforeItem with stale spans; removed in S14 together with T5's attach property)

### ParseContent (one re-entrant front-end entry)

**owner_layer**

L0: engine/src/syntax/parse.{h,cc}; thin stateless C ABI in engine/src/api/

**purpose**

One entry point runs the whole front end over any list of line slices. It serves:
- the document;
- every content body (`#f[..]`, `^[..]`, `@id[..]`, keyword bodies, `#let x = [..]`);
- runtime re-entry (m`…`, m.parse, fence ctx.m.parse, code sidecars);
- tools.
A content body is always parsed in Blocks mode, and a body that is exactly one paragraph unwraps to its inline kids (v2 §4 l.104: `#for (…) [- #x]` yields list items). User constructors therefore receive block content exactly as built-in containers do. Fragments get the full markup language. Splices inside a fragment stay literal, because a tag function cannot see the document's lexical scope; values enter through holes.

**definition**

```
struct FrontEndOptions { bool frontMatter = false; /* S15: SmallVec<UserInlineRow> rows */ };  // tab stop is a language constant
struct HoleRange { u32 start, end; };            // OUT OF BAND: ranges of the joined text that are ${..} holes
struct ContentInput {
  const SourceText* text; SmallVec<LineSlice> lines;
  u32 spanBase; bool synthetic;                  // two-tier offset contract (v2 §4.1 l.143)
  bool body;                                     // true for content bodies: Blocks, then sole-paragraph unwrap
  std::span<const HoleRange> holes; const FrontEndOptions* opts; };
AstNode* parseContent(const ContentInput&, Arena&, Interner&, DiagSink&);
// Fragment ABI (stateless: fresh scratch arena, never touches the document's interner or diagnostics; safe mid-execution):
const char* tsr_parse_fragment(const char* utf8, u32 len, const u32* holeRanges, u16 nHoles,
                               u32 spanBase, u8 mode /*blocks|inline*/, const char* optsJson, u32* outLen);
// Returns UTF-8 JSON {syntaxVersion, tree: CallTree, diagnostics}. CallTree is produced by T2's ONE lowering
// (see LoweringContract). The buffer stays valid until the next tsr_* call; the caller copies it out.
// Fragment limits (deliberate, documented):
//   Splice / Stmt -> literal source text + info 'fragment-splice' (today's fragment.cc:81-88 behaviour);
//   labels / headings register in the host document unless T3 provides a fragment scope.
```

**surface**

#callout(kind: "warn")[
  Install first.

  - step one
  - step two
]
#let intro = [*Hello* $x$ world]
JS: m.parse(str, {offset, mode: 'blocks'|'inline'}), m`*bold* ${value}`, ctx.m.parse(body, {offset: ctx.offset})

**replaces**

- engine/src/inline/inline.cc:144-151 (parseSub: single-span, inline-only sub-parser)
- engine/src/inline/inline.cc:648-656 (parseInlineSpans, used only by fragments)
- engine/src/inline/fragment.cc:9-111 (Conv: second lowering straight to ContentNodes; '*'→CLS_BOLD :47; display and label dropped :59-64; comments dropped :78; Note flattened :89-91)
- engine/src/api/doc.h:85-150 (extractSidecars in the API layer; moves to the codeblock constructor with T2)
- runtime/src/worker/executor.mjs:151 (ctx.m stub) and :231-236 (m as cooked text)

### LoweringContract (AST→L1 semantics: T1 decides, T2 implements)

**owner_layer**

The L0→L1 seam. The semantics below (the scope rule, the argument equivalence, keyword-form meaning, provenance emission and error lowering) are T1's normative spec plus conformance fixtures. The mechanics belong to T2: the hygienic `__s` namespace, the constructor convention, per-block units, printing JS and interpreting fragments, all in engine/src/codegen/ and runtime/src/worker/.

**purpose**

Fix what every AST shape means as a call, once, so that:
- sugar, user calls, regions, fences, statements, keyword forms and errors share one lowering;
- the region and splice forms are provably equivalent;
- statements behave the same in built-in and user content.
There is a single lowering, AST → CallTree (T2). Documents print the CallTree as JS. Fragments return it as JSON to a generic interpreter (`__s[slot](opts, ...kids)`, where a Hole becomes its value). No per-sugar walker exists anywhere.

**definition**

```
Call(sugar)   -> slot `sugar` with payload as opts and kids as content (T2's convention; rebinding via $.sugar.<slot>).
                 The list payload carries the marker class (Bullet|Auto|Explicit), not just 'ordered'.
Splice        -> callee chain applied to its ArgList, followed by the content bodies:
                 Named form -> ONE argument ({ <items verbatim> });  Positional -> verbatim JS;  Empty -> none.
                 Content bodies are appended structurally (ArgList.trailingComma / emptyModuloComments), never by text
                 surgery:  #f(a,)[c] -> f(a, c);  #g(/* c */)[d] -> g(/* c */ d);  #f(k: 1)[c] -> f(({k: 1}), c)
Region        -> `#!name(H) <id>` + interior  ≡  `#name(H)[interior]`:
                 - opts = ({label: "id", H})   (byte-identical to today's ({H}) when there is no suffix; an explicit
                   'label:' in H wins at runtime; both literal -> 'label-conflict' warning);
                 - a Positional or Mixed header -> Error 'header-positional' (the region keeps its interior, with empty opts).
Fence         -> handler(body, {args: ({H}), info, label, offset}).
                 offset is today's scalar except for bodies inside prefix or column containers, which get per-line offsets.
                 A fence with no handler falls back to codeblock (T2).
Provenance    -> paragraphs that are direct children of a CALL BODY (region interior, content body, m.parse result) carry
                 ParaProv as JS-only shadow metadata: no ops, no extra text nodes. Direct-child blocks carry their column.
                 T2's prov.rows uses it: rows at depth-0 Join cuts, cells at depth-0 Sep cuts; a line indented past the
                 content column continues the previous row (v2 §4.1); a nested cut -> 'row-spans-markup' diagnostic.
Error         -> __s.error(code, message, ...salvage)
Reserved word as a bare head -> Error 'reserved-head'  (never val((if)))
SCOPE RULE    -> Statement scopes are the document and keyword-form bodies.
                 '#let' and '#{..}' bind in the NEAREST statement scope wherever they appear: list item, quote, region
                 interior, any content body, or '#let x = [..]'. Content never opens a scope (v2 §2 l.71), whether
                 built-in or user. Keyword bodies are JS blocks (v2 §3 l.89).
Statement lowering (T2 mechanics; normative shape):
  - A top-level block that contains a statement or keyword form lowers to an ANF sequence in source order, one guarded
    unit per top-level block. Blocks without them keep today's nested expression (byte-identical).
  - Names declared in a statement scope (StmtSide.binds, from a jslex pattern scan of the '#let' LHS and the depth-0
    declarations in '#{}') are hoisted to the head of that scope as 'let n = __s.unset'. Statements become assignments
    inside their unit, so T2's per-block try-wrapping never block-scopes a binding. val(__s.unset) -> 'used-before-let'.
  - A same-scope redeclaration -> compile-time Error 'let-redeclared'; only that statement fails.
  - An unscannable pattern -> the statement stays unguarded (today's behaviour) + an info diagnostic.
  Example:
    '- a\n  #let n = 3\n  #n'  =>
      let n = __s.unset;  { const __k0 = __at(para(..)); n = 3; const __k1 = __at(para(val(n)));
                            __emit(__at(list(.., item(__k0, __k1)))); }
Keyword forms, inline or at line start (no IIFE; ANF-hoisted into the unit):
  - '#if (c) [A] else if (d) [B] else [C]' -> let __kN; if (c) { __kN = seq(A) } else if (d) { .. } else { .. };
    then val(__kN) at the source position.
  - '#for (h) [B]' / '#while (c) [B]' -> const __kN = []; for (h) { __kN.push(seq(B)) }; then val(seq(...__kN)).
  - A keyword value that is the whole content of a paragraph is a block value (T2 level normalization flattens a seq of
    blocks).
'#let x = [C]'   -> x = seq(C). '#let xs = [1,2]' is content (App A l.333); an info diagnostic fires when the body looks
                    like a JS array literal.
'#use(spec)'     -> hoisted import + auto-registration of the module's 'fences' export (architecture §4.1).
                    Bindings are ordinary JS: '#let h = await $.use(spec)' (same module instance; T2/T9 resolve the base URL).
Fragments        -> CallTree JSON. The interpreter maps Call -> __s[slot], Hole -> holes[i], Val -> literal text + info.
                    CI checks that, on splice-free fixtures, printing then executing and interpreting give the same ops.
```

**surface**

#if (draft) [*DRAFT*] else [Final]
Status: #if (ok) [yes] else [no].
#for (const s of sections) [
  == #s.title
  #s.body
] 
#use("./lib.js")
#let h = await $.use("./helpers.js")
- item with a binding
  #let n = 3
  uses #n
#callout[
  #let k = 1
]
After #k   (the same rule as the list item)
^[…] lowers to slot 'note', so #{ $.sugar.note = sidenote } (T2) re-targets it.

**replaces**

- engine/src/codegen/codegen.cc:67-94 (Splice lowering by text surgery on lastCallStart :73-84: f(a,, c))
- engine/src/codegen/codegen.cc:145-193 (__fence/__region bespoke encodings; nested row/cell arrays :166-185)
- engine/src/codegen/codegen.cc:214-229 (CodeStmt handled only among the document's direct children)
- engine/src/inline/inline.cc:170-176 (reserved words accepted as heads: #if gives val((if)), a module-wide SyntaxError)
- engine/src/inline/fragment.cc:9-111 (second lowering; replaced by interpreting T2's CallTree)
- runtime/src/worker/executor.mjs:94-113 (regionJoin text rewriting ' | ' and ' '; deleted together with T2's table constructor)

### FrontEndExports (tokens, outline, AST JSON, printer, escaper, conformance)

**owner_layer**

Cross-cutting tooling:
- engine/src/syntax/exports.{h,cc} (pure functions of source and FrontEndOptions);
- thin api/ wrappers;
- tsrc stages;
- runtime/src/shared/tsm-print.mjs;
- tools/gen-syntax.mjs.

**purpose**

Make the engine the only authority on .tsm structure, and make the language invertible.
- Editors, the 'tsm' fence highlighter, translate-tsm and the converters consume engine-derived tokens, outline and AST.
- The hand-written tree-sitter and TextMate grammars become documented approximations. They read their constants from syntax.gen.json and are held to the engine by conformance tests with an explicit allow-list.
- The printer and escaper invert the AST exactly.

**definition**

```
Tokens syntaxTokens(std::string_view src, const FrontEndOptions&);  // (start, end, tag) in the 14-tag contract
Json   outline(std::string_view src, const FrontEndOptions&);
       // {headings:[{level,label,span}], regions:[{name,label,span}], fences:[{tag,label,span}], labels:[{id,rule,span}],
       //  frontMatter?, diagnostics}
Json   parseJson(std::string_view src, const FrontEndOptions&);    // generic AST incl. ParaProv, markers, labels
C ABI: tsr_syntax_tokens / tsr_outline / tsr_parse_json (stateless, scratch arena, copy-out).
       tsrc --stage=tokens|outline|astjson for native goldens.
Engine-internal token provider: a 'tsm' fence is tokenized by syntaxTokens inside the engine, with no NEED_TOKENS round
  trip. code-design.md §2 gains one stated exception: the engine tokenizes its own language only.
Lint (syntactic, info level): 'label-like-text' for a trailing ' <LabelChar+>' on a paragraph or item's last line
  (suppressed by '\<'); 'label-orphan' for a label that cannot attach.
JS (constants from syntax.gen.json):
  print(ast: AstJson) -> string    // lossless; sugar only where its inverse parse is exact, otherwise '#ctor(..)[..]';
                                   // '|' printed as '\|' unless at a Sep cut
  printShadow(node)                // requires T2's constructor-per-kind schema + a sugar-inverse map
                                   // (table/trow/tcell, group roles, list marker class)
  escapeTsm(text, ctx: 'para'|'line-start'|'heading'|'call-body'|'link-text'|'note'|'label') -> string
VS Code: the extension host loads its own WASM instance (the preview engine lives in an iframe served from loopback,
  preview.js:1, server.js:1-4) and converts byte offsets to UTF-16. Semantic tokens, outline, folding and completion
  come from the engine; the generated tree-sitter grammar is the cold-start fallback.
translate-tsm: masks atoms (islands, refs, labels, splices, URLs, note bodies' markers) by engine token spans inside
  whole paragraph source, keeps Pair/Frame markup visible to the model, validates by parseJson equality modulo Text,
  and patches the source in place.
Conformance (CI):
  (a) executable App A/B fixtures;
  (b) tree-sitter vs engine token boundaries on all fixtures, with an allow-list (strict pairs, jslex);
  (c) parse(print(parse(x))) == parse(x) ignoring spans, over fixtures and examples/real-world;
  (d) CallTree print+execute vs interpret ops equality (cheap; one lowering);
  (e) libFuzzer on the C++ entries (lexer + linepass + parseContent, tsr_parse_fragment, print/parse round trip via
      Node), plus property tests of the JS CallTree interpreter.
```

**surface**

- VS Code semantic tokens, outline, folding and completion come from the engine. Completion offers region names found in the document plus handler names registered at execution (T2).
- A ```` ```tsm ```` fence is highlighted by the engine itself.
- Converters use print(ast) or printShadow and escapeTsm(text, ctx).
- translate-tsm masks atoms by engine spans and validates by AST equality.

**replaces**

- editors/vscode-tsm/src/tokens.js:1-6 (tree-sitter promoted to the editor's semantic-token source)
- highlights.scm in three copies (grammar/tree-sitter-tsm, third_party/grammars/tsm, runtime/assets/hl/tsm.scm): one source; tsm fences no longer need the runtime copy
- editors/vscode-tsm/src/extension.js:9-10 (HEADING/FENCE regexes ignoring containers), :123 (BUILDERS list incl. center/right/columns that do nothing), :140 (ASCII-only label regex)
- tools/translate-tsm.mjs:46-70 (INLINE_RE line masking that misses @ref, <label>, ^[ and #splice; the '//' keep rule for syntax .tsm does not have)
- tools/convert/*.mjs escapers and copied entity tables (html2tsm.mjs:19-22 vs pbr2tsm.mjs:382-389)
- docs/editor-design.md:95 (claims outline and folding come from tree-sitter)

## Subsumption (finding → mechanism)

- **subsumed** by *CallAST ParaProv cuts + BlockAutomaton (regions are explicit-close containers) + LoweringContract (region ≡ splice; children through the ordinary path)*: `markup-language/region-pipe-segmentation`, `parser-frontend/region-pipe-segmentation-in-parser`, `math/missed:0`, `markup-language/missed:0`
  Deleted at S9:
  - splitCells (inline.cc:454-508) and the Row/Cell kinds;
  - the codegen Row branch (codegen.cc:166-185);
  - regionJoin (executor.mjs:94-113).

  Region interiors become ordinary markup with real spans, so display math and labels work inside regions.

  `|` stays literal text. Separators and line joins are recorded as zero-width cuts:
  - no ops change and no extra text nodes;
  - emission is identical inside and outside regions;
  - the cuts are attached for every call body, so `#table({cols: 2})[⏎a | b⏎]` gets them too.

  Pair×Sep rule: no barrier, as v2 §4.1 defines segmentation at tree level. `*a | b*` is one cell containing strong('a | b'), a semantic change listed at S9. A cut inside a frame is flagged `nested`, so the table constructor reports 'row-spans-markup' instead of merging silently. Line columns let T2 implement the indented-continuation rule.

  T2 supplies prov.rows and public table/figure constructors. T3 owns the caption slot.
- **subsumed** by *SurfaceLexer (AtomTape, one bracket matcher for content bodies, Pair/weak-[ stack) + BlockAutomaton (Own classes, tentative carries)*: `markup-language/inline-delimiter-scanners`, `parser-frontend/cross-line-raw-scans`
  Islands are carved out before line structure. Phase 1 lexes atoms and grants line ownership by body mode:
  - inline islands, inline JS and inline-form bodies: Leaf;
  - comments, statements and block-form bodies: Container.
  Pair and link frames never own lines (v2 §4 l.97). Every carry is tentative and bounded, which replaces the raw-buffer scans and the open.empty() switch.
- **subsumed** by *SyntaxTable first-byte dispatch + SurfaceLexer (SpanCursor, structural joins, run-length code spans)*: `parser-frontend/inline-recognizer-cascade`, `parser-frontend/missed:0`, `parser-frontend/missed:1`, `parser-frontend/missed:4`
  There is one recognizer driven by the table. Brackets are matched by one island-aware counter, and a bare `[` is a weak entry that a Pair closer may skip. CRLF is the SourceText line terminator, and joins are structural. Inline code shares the fence run-length rule.
- **subsumed** by *SurfaceLexer.ArgList (named-only form = opts object) + LoweringContract (region ≡ splice ≡ fence args)*: `markup-language/arg-grammar-unification`, `parser-frontend/region-fence-private-dispatch`, `parser-frontend/missed:3`
  One lexical ArgList and one semantic rule serve splices, region headers and fence info: a named-only list is the opts object, and anything else is verbatim JS (a conservative extension, since a `k: v` item is never a valid JS argument).
  - Region headers keep today's `({…})` byte for byte; all 795 corpus openers are `k: v` or bare.
  - Shorthand-only or positional headers become a contained Error with a fix-it.
  - Content literals are not legal in argument values (App A l.333); m`…` is the content-value path.
  - Content insertion is structural.
  - Region names keep the splice-head class, so `#!x` and `#x` always name the same constructor.

  T2 owns the registry and the ctor(opts, ...content) convention that consumes this record.
- **subsumed** by *SurfaceLexer id/label primitives + CallSide/HeaderSide label spans → the `label` option of any constructor*: `markup-language/universal-labels`, `parser-frontend/label-and-id-lexing-scattered`, `real-world-evidence/missed:2`
  One class serves each use, exported to tooling:
  - LabelChar: no whitespace, CJK allowed;
  - a bare-id grammar with IdJoin for refs and labels;
  - the splice-head class, which keeps `$` (App A l.330).

  The ` <id>` suffix is accepted where the tail is not prose: headings, region openers, fence openers and every math island. The suffix is recognised on lexed tokens, so `\<` and islands are respected. A label that cannot attach gives 'label-orphan'.

  A trailing `<x>` on a paragraph or item gets the 'label-like-text' lint instead of staying silently literal, which covers the HoTT `<tab-pov>` case.

  There is no sugar for labelling an item. That is a recorded gap; the function form `#item({label: "x"})[..]` works under T2's convention.

  Registration and namespaces belong to T3.
- **subsumed** by *CallAST joins (the front end stops deciding at S13) + T5 TextRules join policy*: `markup-language/cjk-softbreak-classifier`, `parser-frontend/parser-owned-cjk-line-join`
  Staging:
  - Until S13, line joins merge into Text exactly as today.
  - At S10 the interim predicate is extracted unchanged into support/: today's cjkish, isCjk || U+2014 || U+2026 (inline.cc:50). It is applied to cooked Text neighbours, with today's raw-byte rule kept for non-Text neighbours. This fixes `这是*强调*⏎中文` without touching style/patch's `……⏎照` or English curly quotes, which T5 handles by language.
  - At S13, softbreak becomes an ops kind that T5 resolves.

  emit.cc:396 is App C's defined-width em-dash/ellipsis emission, not a join predicate. It stays, sharing the extracted class until T5 turns it into TextRules data.
- **subsumed** by *SyntaxTable `note` row (Content body, same as `#note[..]`) + slot `note`; attachment → T5*: `markup-language/footnote-sugar-oneoff`
  The private scanner is replaced by the shared Content body, so escapes, islands, multi-line bodies and blocks now work. `^[` and `#note[` get identical bounds and parse mode.

  The spacing rule leaves the parser. Today's relocation (inline.cc:384-397, spaceBeforeItem) stays only until T5's attach-left property lands (S14). That property applies to note, #note and sidenote however they were written, and it fixes ' . end'.

  Numbering, marks and flows belong to T3; rebinding via $.sugar belongs to T2.
- **owned-by-other-theme** by *T3-semantics (ref forms, supplements, locators, groups); T1 supplies the surface in S7*: `markup-language/reference-forms-closed`
  T1's surface part:
  - `@id` with the IdJoin grammar;
  - `@[a, b]` parsed into an IdList, with ',' kept as the separator (cite/basic.tsm:3);
  - `@id[supplement]` as a Content body attached to the ref.

  The ref constructor accepts only (target) today (executor.mjs:173). The structured lowering and the supplement therefore switch on together with T3's ref constructor; until then codegen keeps ref("a, b").
- **subsumed** by *SyntaxTable + FrontEndExports (engine tokens, outline and AST JSON; syntax.gen.json constants; conformance tests)*: `markup-language/surface-grammar-drift`, `parser-frontend/multiple-tsm-grammars`, `real-world-evidence/lexical-syntax-copies`
  Structure and semantic tokens come from the engine, including tsm fences and the VS Code extension host. Tree-sitter and TextMate stay hand-written approximations: they read their constants from syntax.gen.json and are pinned to the engine by conformance test (b) with an allow-list. One highlights.scm remains. The PackCC claims are corrected.
- **subsumed** by *SyntaxTable math row (one island spec) + S6 tooling constants; splitCells deleted at S9*: `math/math-span-lexer-triplication`
  The island delimiter and its escape set ({$}, decoded; every other `\x` passed verbatim to T8) are declared once. The engine lexer uses the row from S3; tooling reads it from S6; the fourth lexer, splitCells, goes at S9.
- **owned-by-other-theme** by *T2-constructor-ir (level normalization at L3, including list grouping)*: `markup-language/block-inline-placement`, `parser-frontend/display-math-by-ast-shape`
  T1 keeps no placement rule. Padded math is always Call{math, display=1, label}, and the AST never promotes; the earlier PROMOTE(SoleChild) row is dropped. The codegen Para→mathblock peephole (codegen.cc:95-117) stays as an interim L1 rule until T2's normalization retires it. That keeps S2 byte-identical, including .ast.

  T2 also merges adjacent same-marker-class lists produced by code (`#for … [- #x]`). The padding rule itself is the documented display syntax (not_generalized).
- **subsumed** by *SyntaxTable math row (pad=Display, suffix on every island) + label-orphan diagnostic*: `math/math-island-oneoff-syntax`
  Labels attach on every math island: inline, mid-paragraph or standalone. They reach the lowering, or they give 'label-orphan' while the interim mathinline cannot carry them. They are never dropped silently. A multi-line math island owns lines inside its leaf, so a display wrapped inside an item no longer breaks block structure. A single math kind is T8/T2's decision.
- **subsumed** by *BlockAutomaton (container protocol, columnOf, Interrupt, list identity key, leaf-owned comments)*: `markup-language/missed:1`, `markup-language/missed:2`, `markup-language/missed:4`, `markup-language/missed:5`, `parser-frontend/missed:2`, `parser-frontend/missed:5`, `parser-frontend/region-container-special-case`
  Each item is handled once in the protocol:
  - container spans are extended on every attributed line;
  - one tab rule, fixed at 4 in App B;
  - explicit-close containers with onForcedClose and by-name resync;
  - verbatim blocks end at container exit;
  - a per-rule interruption policy;
  - list identity on (marker class, column).
- **subsumed** by *CallAST (compact node, generated payloads and side records, generic dump, no default arms)*: `parser-frontend/per-feature-ast-kinds`
  This follows the verifier's design: generated enum ids, typed payloads and side records, with no name strings. Splice stays distinct because its callee is JS. The node is 32 bytes or less, with a static_assert. The lowering side belongs to T2.
- **subsumed** by *BlockAutomaton (inline-form / block-form content bodies) + ParseContent (always Blocks, then sole-paragraph unwrap)*: `parser-frontend/content-args-inline-only`
  Multi-line bodies are owned by phase 1.
  - Block form: `[` ends the line and a `]`-led line closes it; blank lines are allowed.
  - Inline form: bounded by the leaf.
  A stray `]` in prose cannot close a body (scratchpad/t/swallow.tsm keeps its heading). `#for (…) [- #x]` yields a list, as v2 l.104 says. List grouping across iterations is T2's.
- **subsumed** by *ParseContent (tsr_parse_fragment, out-of-band holes) + LoweringContract (one AST→CallTree lowering; fragments interpreted generically)*: `parser-frontend/fragment-parallel-lowering`, `real-world-evidence/markup-reentry-missing`
  fragment.cc's Conv and the planned per-sugar JS walker are both gone. m.parse, m`` and ctx.m.parse return content through the same slots, so user overrides apply.

  Splices in fragments stay literal plus info. This is deliberate: a tag function cannot see document bindings, and the executor loads code only through async Blob-URL import (executor.mjs:290-308).

  Live documentation examples need T3's fragment scope, or they register labels in the host document. T2 owns the executor plumbing, the sidecars in the codeblock constructor and ctx.source().
- **subsumed** by *SyntaxTable kwform/let/stmt/use rows + LoweringContract (one scope rule; ANF units; hoisted declarations)*: `parser-frontend/keyword-forms-closed-set-missing`, `parser-frontend/code-statements-top-level-only`
  The keyword set stays closed (v2 §3 l.89). Keyword forms are inline-capable CallChain rows with ElseChain, so `Status: #if (ok) [yes] else [no].` works and multi-line `] else [` continues the chain.

  Statements are legal in every content list under one rule: the nearest statement scope, with content never opening one. Declarations are hoisted so T2's per-block units cannot block-scope them. Reserved heads are a contained Error.

  The statement semantics are T1's; the mechanics are T2's.
- **subsumed** by *SyntaxTable guards (PrevIdent on a '#' bare value head and on '@'; Intraword on '*' and '_') + url Verbatim row*: `parser-frontend/sigil-context-rules`
  The guards fire only next to ASCII identifier characters, so the CJK-first strict pairing of v2 §5 l.157 is preserved. Call splices are never guarded: `H#sub[2]O` and `10#super[th]` stay splices. The autolink row outranks markup, which fixes test/golden/doc/url-break.*.
- **subsumed** by *BlockAutomaton `front` rule gated by FrontEndOptions.frontMatter (one host option, passed to every export)*: `parser-frontend/front-matter-editor-only`
  Front matter is a host option, not a language form. It yields a Comment-class node with exact offsets, its text is exposed as outline().frontMatter, and tokens and outline see the same option as rendering.
- **subsumed** by *FrontEndExports (outline + registered handler names; print/printShadow/escapeTsm)*: `parser-frontend/editor-region-builder-list`, `real-world-evidence/no-tsm-printer`
  Completion comes from the document's own regions plus the handler names registered at execution (T2 registry). The printer inverts the AST JSON losslessly (cuts, markers, labels). Printing shadow trees depends on T2's constructor-per-kind schema. Converters that only typeset need no printer.
- **owned-by-other-theme** by *T3-semantics (description list form + term slot), with T2 schema and T7 dl/dt/dd mapping*: `real-world-evidence/description-list-missing`
  T1 adds no line sugar, because v2 §11.1 l.240 drops `/ term:`. T1 contributes block-content bodies and the named-only argument form. After S11, `#item(term: m`…`)[…]` and a user `#dl[…]` with nested blocks are expressible.
- **bug-fix-only** by *SurfaceLexer/SpanCursor (S3)*: `parser-frontend/contiguous-escapes-blocks`, `markup-language/inline-scanner-overrun`
  No scan reads past its leaf, so no content is duplicated.
- **bug-fix-only** by *BlockAutomaton Container-owned comments (S5)*: `parser-frontend/inline-comment-leaks-block-structure`
  A mid-line `%--` hides the block markers it covers, up to its closer within the container (v2 §4.2 l.151). Without a closer it reverts to literal text with a diagnostic.
- **bug-fix-only** by *BlockAutomaton (S4)*: `parser-frontend/unterminated-let-swallows-document`, `markup-language/region-error-recovery`, `parser-frontend/trailing-text-after-block-closers-dropped`, `markup-language/same-line-trailing-text-dropped`, `parser-frontend/fence-double-dedent-in-containers`, `parser-frontend/span-fidelity`
  The fixes are:
  - statement carries balanced to container end with recovery at the first blank line, so a valid `#{` containing blank lines still works;
  - by-name resync;
  - line remainders;
  - relative dedent, with per-line offsets inside containers;
  - container span extension.
  Cooked-to-raw maps are recorded; exposing them as run-level spans is T5/T7's job.
- **bug-fix-only** by *LoweringContract + ParseContent (owning step S8)*: `markup-language/nested-code-statements-dropped`, `markup-language/spec-features-unimplemented`, `parser-frontend/no-error-nodes`
  spec-features-unimplemented lands in parts:
  - multi-line content blocks (S5);
  - autolink (S10);
  - m re-entry (S11);
  - keyword forms and content literals (S8, the owning step);
  - $.ref and user counters (T3);
  - config JSON (T9).
- **bug-fix-only** by *SyntaxTable guards (S10), BlockAutomaton list identity (S4)*: `markup-language/ambiguity-hazards`
  Fixed: intraword `_`/`*`, a bare `word#todo`, `user@x`, and list merging. Kept, as documented lexical choices (see not_generalized): `$5 and $10`, and a heading ending in ` <T>`, which can be escaped with `\<`. The fatal runtime case is contained by T2's units.
- **bug-fix-only** by *S1 (switch-enum guard + Note dump case)*: `parser-frontend/ast-dump-missing-note`
  Corrects test/golden/notes/{basic,explicit,cjk-glue}.ast.txt.
- **bug-fix-only** by *S6 (docs corrected alongside the exports)*: `parser-frontend/docs-drift`
  Covers PackCC, the SourceMap builder, the provenance location, and editor-design §5 and l.95.
- **bug-fix-only** by *FrontEndExports (S12): App A/B conformance, generated syntax doc, printer round trip, shared converter kit*: `markup-language/doc-drift`, `real-world-evidence/converter-fidelity-unchecked`, `real-world-evidence/converter-code-duplication`
  Corpus checks:
  - the ref-unresolved count against an allow-list;
  - no surviving `<label>` or backslash before a letter in Text;
  - link-count parity;
  - no 'label-like-text' lints.
- **owned-by-other-theme** by *T2-constructor-ir (hygienic `__s` namespace; public names shadowable)*: `parser-frontend/ctor-names-are-reserved-words`
  This is a prerequisite of S8, not one of its fixes. ANF temporaries (`__kN`), the hoisted sentinel `__s.unset` and user `#let list` must not collide.

## User extension examples

### Theorem-like environment with a block body, its own label and a numbered equation

**Today**

#!theorem(label: "thm-a")
Let $ x = 1 $ <eq-x>
#theorem!
- Every interior line is split at `|` into Row/Cell (inline.cc:601-627) and re-glued by regionJoin (executor.mjs:94-113).
- The display becomes mathinline and <eq-x> is lost.
- @thm-a renders '??', because only role 'figure' registers (resolve.cc:166).

**After**

#!theorem(title: m`Fermat`) <thm-fermat>
Let

$ x^n + y^n = z^n $ <eq-flt>

have no solution.
#theorem!
- The interior is ordinary markup with real spans. The region is exactly `#theorem(title: m`Fermat`)[…]`, with opts ({label: 'thm-fermat', title: …}).
- A user handler (T2 registry) and an EnvSpec counter (T3) render @thm-fermat as 'Theorem 2'. No engine change is needed.

### A user constructor that receives block content

**Today**

#callout[first line
- second]
→ 'unclosed content argument', and '- second]' becomes a real list item (inline.cc:204 scans one line only; verified with tsrc).

**After**

#callout(kind: "warn")[
  Install first.

  - step one
  - step two
]
- Block form: the line pass owns the body until the `]`-led line.
- The named-only arguments become the opts object, so this lowers to callout(({kind: "warn"}), para(..), list(..)).
- `#callout[short]` gets inline kids through sole-paragraph unwrap.
- `Intro #note[forgot⏎⏎== Section⏎(0, 1]` keeps its heading: the inline form is bounded by the blank line.

### Content generated by loops and conditionals, inline and as blocks

**Today**

#for (const s of secs) [== #s.t]
→ val((for)) plus literal text: a SyntaxError that kills the whole document, with empty diagnostics (inline.cc:170-176, codegen.cc:67-72).

**After**

#for (const s of secs) [
  == #s.t
  #s.body
] 
Status: #if (ok) [yes] else [no].
- Each form lowers to ANF inside its top-level unit (no IIFE). Loop variables bind by JS block scoping (v2 §3 l.89).
- `[- #x]` bodies yield list items; T2 merges the items across iterations into one list.

### A binding inside a user body behaves like one inside a list item

**Today**

- #let n = 3
#callout[#let k = 1]
→ both are silently dropped (codegen.cc:198: text(""); scratchpad/t/nestlet.tsm).

**After**

Both bind at document scope (the nearest statement scope), so `After #n #k` works. Declarations are hoisted (`let n = __s.unset`) and assigned inside T2's guarded unit, so a throw in one block cannot hide the binding from later ones. Redeclaring `#let n` in the same scope is a contained 'let-redeclared' error.

### A fence or region DSL whose output is markup (documentation examples written once)

**Today**

ctx.m is a cooked-text stub (executor.mjs:151, :231-236), so the blog writes every example twice.

**After**

#{ $.fence('example', (body, ctx) => seq(codeblock('tsm', body), ctx.m.parse(body, {offset: ctx.offset, mode: 'blocks'}))) }
- tsr_parse_fragment returns T2's CallTree, which is interpreted through the same __s slots, so user sugar overrides apply.
- Spans map back through the offset.
- Splices inside the example stay literal with an info diagnostic.
- A heading or label in the example registers in the host document unless T3 provides a fragment scope (documented limitation).

### Rebinding built-in sugar (footnotes become sidenotes)

**Today**

^[…] compiles to the destructured constant `note` (codegen.cc:48-52, :209-212). `#let note = …` is a fatal redeclaration.

**After**

#{ $.sugar.note = (o, ...kids) => sidenote(o, ...kids) }
- Every ^[…] lowers through slot 'note' (T2 provides $.sugar and the hygienic namespace).
- Attachment to the preceding word is a node property in T5, so `word ^[n]`, `word #note[n]` and the sidenote all attach the same way.

### A table built with the splice form gets the same provenance as the region form

**Today**

Only `#!table` regions get rows and cells. Even there, `a|b 中文|中文` in a non-table region is split into three text shadows, then re-glued as `a | b …`.

**After**

#table(cols: 3)[
  Name | Qty | Price
  Alpha | 2 | 3.50
]
- The body paragraph carries zero-width Sep/Join cuts and line columns.
- T2's table constructor builds rows from them exactly as for `#!table(cols: 3)`.
- In a non-table region, `a|b` stays one Text node, emitted as at top level.

### A converter or translation tool emitting .tsm

**Today**

html2tsm escapes only $ # @ (html2tsm.mjs:68), so x_i becomes emphasis. wiki2tsm leaves 8 broken footnotes. translate-tsm misses @ref, <label> and ^[ (translate-tsm.mjs:48).

**After**

import {print, printShadow, escapeTsm} from 'runtime/src/shared/tsm-print.mjs'
- Converters build an AST JSON or shadows and print them. `x\_i` is escaped; a note body containing '](' prints as #note[..].
- translate-tsm masks atoms by engine token spans inside whole paragraphs, validates by parseJson equality, and patches the source in place.
- CI asserts the round trip on fixtures and the corpus.

### Editor support for a user-defined region

**Today**

Completion offers a literal BUILDERS list (extension.js:123). Outline and folding are regexes that ignore containers (extension.js:9-10). CJK labels are missed (extension.js:140).

**After**

- The extension host loads its own WASM. tsr_outline(src, opts) returns headings, regions, labels and fences with UTF-16-converted spans.
- Handler names registered at execution come back in the outline/diagnostics payload.
- Semantic tokens match the published page by construction; tree-sitter is used only at cold start.

### Labelling a diagram fence or a table

**Today**

```` ```dot(rankdir: "LR") ```` cannot carry a label. `#!table(..) <tab-pov>` is not syntax, so the HoTT converter's trailing <tab-pov> stays literal, silently (hott-introduction.tsm:161).

**After**

```` ```dot(rankdir: "LR") <fig-graph> ```` and `#!table(cols: 3) <tab-pov>`. The suffix is sugar for `label:` (an explicit label in H wins; both literal gives 'label-conflict'). A trailing `<x>` on a paragraph raises 'label-like-text' instead of staying silently literal.

### (Optional, S15) A site declares a new inline delimiter

**Today**

Requires editing inline.cc, tree-sitter, TextMate and the converter escapers.

**After**

Site config {"syntax":{"inline":[{"body":"pair","open":"==","slot":"mark"}]}} plus #{ $.sugar.mark = (o, ...k) => style({class:'mark'}, ...k) }.
- Only the Pair and Content body modes are allowed (Own None or Leaf; never line-level).
- The row is validated against existing openers.
- Tokens, outline, printer and m.parse see it, because every export takes FrontEndOptions.

## Invariants

- **I1 su fixed-point determinism; byte-exact goldens**
  The front end is a pure function of (source bytes, FrontEndOptions). The tab stop is a language constant, not a host option, so document structure never depends on the host. Dispatch is a fixed first-byte table with fixed precedence. Reverts are deterministic: each opener is re-lexed at most once, in source order. Nothing iterates in hash order.

  S1–S8 are gated on byte-identical .skeleton/.ast/.js/.tree goldens, except for named fixtures:
  - S1: the three notes .ast goldens, which currently enshrine blank lines;
  - S4: container spans in doc/structure, notes/cjk-glue, region/figure and code/tsm-hl.

  The churn-heavy steps are isolated and listed: S9 (10 region fixtures), S10, S13 (OPS bump) and S14 (notes). The earlier claim that S2 is byte-identical was false while PROMOTE moved into L0. PROMOTE is now dropped, so the claim holds.
- **I2 Measurement–render robustness contract (v2 §7)**
  T1 adds no metric decision.
  - The CJK join stays today's predicate until T5 resolves softbreak at S13.
  - Note attachment moves to T5 (S14).
  - Provenance cuts are zero-width metadata: Text nodes are not split, so emitText sees the same strings inside and outside regions.
  - Autolinks are ordinary link nodes with the same text.
  - `\` + EOL lowers to the existing hardbreak kind.
- **I3 Ops contract; OPS_VERSION discipline; reader is a fuzz target**
  No OPS_VERSION change until S13, the softbreak kind, coordinated with T2's codec and vocabulary versioning.
  - Provenance (S9) is JS-only shadow metadata.
  - Sidecars from JS (S11) reuse existing kinds.
  - The binary AST buffer from the earlier design is gone. The fragment result is JSON carrying syntaxVersion, so JS/WASM skew is detected as for OPS_VERSION.
  - The fuzzed surfaces are the C++ entries (lexer, linepass, parseContent, tsr_parse_fragment) plus a property-tested JS interpreter.
- **I4 Execution declares, resolver decides; resolver is a pure single pass**
  Labels, ref targets and supplements leave the front end as declarations, and the parser never resolves. Cite groups are parsed at parse time, which moves syntax earlier, not resolution later. m.parse is pure syntax re-entry. Keyword forms are plain JS control flow over declarations, and the 'used-before-let' sentinel concerns JS bindings, not resolved values.
- **I5 Dual-target rule (architecture §2.1)**
  All front-end code lives in engine/src/{syntax,linepass,inline,ast,codegen} and is host-neutral. api/ gains only thin stateless exports and loses extractSidecars (doc.h:85-150), which moves to T2's codeblock constructor. The VS Code extension host gets its own WASM instance through the existing Node loader path. Native CI gets tsrc --stage=tokens|outline|astjson and round-trip tests.
- **I6 Emission-time style binding (v2 §12) with the DAG/schedule encoding**
  Content bodies and keyword bodies are content-value expressions evaluated in source order within their top-level unit. ANF temporaries are built before the unit's single EMIT, just as nested arguments are, so op ids stay post-order and styles bind at EMIT.

  Recorded caveat: $.style calls in container statements still act at top-level-block granularity. Scoping them is T4's job.
- **I7 Block-granular containment of errors (v2 §11)**
  Parse-level containment is implemented. The front end never emits JS it knows to be malformed: `({3})`, `val((if))`, `f(a,, c)`, an unterminated `#let`, mixed args and reserved heads all become Error nodes.

  Every line-owning carry is tentative and bounded:
  - inline openers stop at the end of the leaf;
  - block-form bodies close only on a `]`-led line at or above the opener's indent, so prose brackets cannot close them;
  - statements and comments stop at container exit;
  - a failed statement recovers at its first blank line.
  This corrects the earlier claim that an unclosed `#f[` never swallows later blocks; that claim was false for a root-level Container carry with free closer lookahead. Regions resync by name.

  Containment of runtime and user-JS errors needs T2's per-block units. T1's hoisted-declaration contract makes those units compatible with the document scope.
- **I8 Resumable pull-loop state machine; atomic per-paragraph swaps**
  No new pull states are added. m.parse is a synchronous, stateless re-entry with a scratch arena and copy-out. tsm fence tokens are computed inside the engine, so no NEED_TOKENS round trip is needed. Paragraph ids keep their derivation.

  Incremental re-lex contract for T9:
  - LexState is empty at every top-level block boundary, by construction.
  - Each reverted opener records a RevertedWindow [openerLine, boundLine].
  - An edit at line N must re-lex from the earliest boundary at or before both N and the opener of any window containing N. It then runs forward until the boundary and text match again.
- **I9 Performance: hot path parse/codegen/execute/emit; editor fast path**
  - Each byte is lexed once for atoms: phase 1 records the AtomTape, and phase 2 scans only the markup bytes between atoms.
  - Reverts are bounded: each unclosed opener costs one bounded re-lex and already emits a diagnostic.
  - AstNode is 32 bytes or less (static_assert); args, headers, JS pieces, provenance and raw maps live in side records only on the nodes that have them. The earlier inline-field layout modelled at 376 B per node against today's 64 B is withdrawn.
  - ANF applies only to top-level blocks that contain statements or keyword forms.

  Gate: tools/bench-edit.mjs compile time within +10% at 87K, and an AST-bytes metric no larger than today's (editor-design §1: compile+execute+ingest under 6 ms).

## Interfaces

- **T2-constructor-ir** (consumes)
  Ownership seam: T1 owns the CallAST, the slot and payload schema, parseContent and tsr_parse_fragment, and the LoweringContract semantics. T2 owns all lowering mechanics. T1 relies on T2 for:
  - one lowering AST→CallTree, printed for documents and interpreted generically for fragments;
  - the hygienic `__s` namespace with $.sugar.<slot> rebinding, `__s.unset` and `__kN` temporaries;
  - the uniform ctor(opts, ...content) convention, which consumes the named-form ArgList as opts unchanged;
  - the region/fence handler registry: `#!name(H)` ≡ `#name(H)[…]`;
  - per-block guarded units that wrap assignments, not declarations;
  - prov.rows(para) over ParaProv cuts: rows at depth-0 Join, cells at depth-0 Sep, the indented-continuation rule, and 'row-spans-markup' on nested cuts; plus public table/figure constructors, landing atomically with S9;
  - level normalization: display math placement, flattening a seq of blocks in block position, and merging adjacent same-marker-class lists (retires the codegen peephole);
  - __s.error and __s.linebreak; sidecars inside the codeblock constructor; ctx.source() for interior spans; $.use(spec).
- **T2-constructor-ir** (provides)
  T1 supplies the parse side:
  - CallAST with SugarId rows (slot and typed payload, list marker class), Splice nodes with ArgList {form, items, trailingComma, emptyModuloComments}, HeaderSide for regions and fences, KwSide, Error and Hole nodes;
  - StmtSide.binds, the declared names, for hoisting;
  - ParaProv for call-body paragraphs;
  - the conformance fixtures that define the LoweringContract, including the scope rule, redeclaration, ElseChain and named/positional argument cases.
- **T3-semantics** (provides)
  T1 supplies label and ref declarations in parsed form:
  - a `label` on any Call or region/fence, from the one LabelChar grammar through the ` <id>` suffix (headings, region and fence openers, every math island) or `label:`;
  - the syntactic diagnostics 'label-orphan', 'label-conflict' and 'label-like-text';
  - ref Calls with an IdList of targets and an optional supplement body;
  - `^[..]` lowered to slot 'note'.
  T1 assumes T3 provides:
  - registration and namespaces, and a semantic 'label on non-labelable kind' diagnostic;
  - a ref constructor taking structured targets and a supplement (this gates the S7 lowering);
  - EnvSpec numbering and a caption slot;
  - note flows;
  - description lists;
  - a fragment scope (label namespace, counter and outline isolation) for live examples.
- **T5-text-shaping** (provides)
  T1 supplies:
  - line-join provenance: ParaProv Join cuts in call bodies from S9, and the ops kind `softbreak` (next to `hardbreak`) everywhere from S13;
  - Text cooked-to-raw maps;
  - lossless spacing around note markers from S14 (the blank before `^[` stays in the text).
- **T5-text-shaping** (consumes)
  T1 relies on T5 for:
  - one join policy in TextRules over (script class × lang), resolving softbreak at instantiation or paragraph flattening;
  - an attach-left property on note-like kinds (sup-attach) that trims the preceding blank at emit, uniformly for ^[ ], #note and sidenote;
  - the em-dash/ellipsis CJK class as TextRules data.
  Until then, the parser and emit.cc:396 share the support/ predicate extracted unchanged at S10.
- **T8-math** (provides)
  T1 supplies the island source with only `\$` decoded (every other `\x` verbatim, as today's bytes), display from the padding rule, the label from the suffix, and the island and label spans. T1 never parses inside `$`. If T8 decides values may appear inside math, the math row's body mode gains a 'holes' parameter: `#` heads become atoms inside the island, and T8 owns their meaning. One `math` kind versus mathinline/mathblock is decided by T8/T2.
- **T9-host-protocol** (consumes)
  T1 relies on T9 (with T4) for:
  - FrontEndOptions {frontMatter, optional syntax rows} in the settings JSON ABI, passed to EVERY export (tokens, outline, parse_json, parse_fragment) as well as to the document parse; the tab stop is not a setting;
  - the export ABI for front-end products, or T9's generic stage-dump export.
- **T9-host-protocol** (provides)
  T1 supplies:
  - the incremental re-lex contract: LexState snapshots at top-level boundaries (empty by construction) plus RevertedWindows, as specified under I8;
  - outline, labels and diagnostics JSON;
  - syntaxVersion on fragment results;
  - engine-internal 'tsm' tokens, so NEED_TOKENS never sees lang 'tsm'.
- **T7-render-runtime** (provides)
  T1 supplies:
  - container spans covering their content, and exact label spans for data-s/data-e and click-to-source;
  - autolinks as ordinary link nodes;
  - outline().labels, which lets the editor and shell resolve anchors without scraping the DOM;
  - the list marker class, for semantic `ol`/`ul` and start attributes.

## Migration

### S1 Compiler guards  → plan P0-02

- Remove the default: arms at codegen.cc:198-200 and fragment.cc:89-91 in favour of explicit cases. The fragment Note case keeps flattening, plus an info diagnostic, until S11.
- Add the Note case to dumpAst.
- Add -Werror=switch-enum for the front-end translation units only (syntax/linepass/inline/ast/codegen), not PUBLIC on tsr_core. -Wall already enables -Wswitch (engine/CMakeLists.txt:27).
- Add the executable App A/B conformance fixtures (new files only).

**Golden impact:** test/golden/notes/{basic,explicit,cjk-glue}.ast.txt: the blank lines become `note @[s,e)` lines. These goldens were wrong. Nothing else changes.

**OPS bump (as designed):** False

**Fixes:** `parser-frontend/ast-dump-missing-note`

### S2 syntax.def + CallAST + generic dump (byte-identical refactor)  → plan P1-05

- syntax.def with CLASS/INLINE/BLOCK/KEYWORD/SUGAR rows; the generated payload union and side-record pools; AstNode of 32 bytes or less.
- The AstBuilder produces Call/Splice/Stmt nodes. Row/Cell/SpliceArg stay as legacy dump formats.
- The generic dump.
- Codegen dispatches on slot and payload through per-slot legacy adapters (this is T2's file). The Para→mathblock peephole stays as an interim L1 rule; there is no PROMOTE in L0.

**Golden impact:** None. CI gate: every .skeleton/.ast/.js/.tree golden is byte-identical, including math/display, eqref and stretch .ast, since the AST shape is unchanged. Bench gate: AST bytes are no larger than today's.

**OPS bump (as designed):** False

**Fixes:** `parser-frontend/per-feature-ast-kinds`

### S3 SurfaceLexer + SpanCursor + structural joins (phase 2)  → plan P1-06

- Replace the inline scanners with the table-driven lexer: one island-aware bracket counter for content bodies, and an inline stack holding only Pair and weak `[` entries (a closer skips weak entries).
- `@[` becomes an IdList body.
- Multi-backtick code spans.
- Math: only `\$` is decoded.
- SourceText treats \r\n as the line terminator, joins are structural, and every scan is bounded at its leaf.
- Charsets stay parameterised at today's values until S7.
- The note-space relocation is kept as today's behaviour (not a table flag) until S14.

**Golden impact:** Expected: none for existing fixtures (gate). No fixture has `\$` inside math, CRLF, or a code span across a join. Any diff must be a listed bug.
New fixtures: CRLF, trailing blank, `$` overrun, `^[a \] b]`, `#f[code `a]b` here]`, `[range $[0,1)$](u)`, ``a`b``, `*range [0, 1) only*`, `_see [sic_ ok`.

**OPS bump (as designed):** False

**Fixes:** `parser-frontend/inline-recognizer-cascade`, `parser-frontend/missed:0`, `parser-frontend/missed:1`, `parser-frontend/missed:4`, `parser-frontend/contiguous-escapes-blocks`, `markup-language/inline-scanner-overrun`

### S4 BlockAutomaton and bounded statements  → plan P1-07

- Container protocol (Prefix/Column/Explicit); spans are extended on every attributed line.
- Verbatim blocks: fences end at container exit, with dedent relative to the content column. Per-line offsets are passed only for fences inside prefix or column containers; elsewhere today's scalar is kept.
- Regions: by-name resync, orphan closer → Error leaf, forced close → region-unclosed.
- Line-remainder re-entry; leaf-owned comment lines.
- columnOf with a tab stop of 4 (App B constant); list identity on (marker class, column); per-rule paragraph interruption.
- Statements: `#{`/`#let` are balanced to container exit (EOF at the root, as today), with recovery at the first blank line only when the balanced scan fails. Multi-line statements inside containers become JsText pieces. A failure becomes an Error node, never pasted JS.

**Golden impact:** Container spans change the skeleton/ast/js(__at)/tree/semantic goldens and the .ops recordings of doc/structure, notes/cjk-glue, region/figure and code/tsm-hl.

Everything else is byte-identical:
- inline/fence-edge's indented top-level fence keeps the scalar offset;
- no fixture has tabs, a comment line directly after a paragraph line, an `N.` interruption with N≠1, or a fence inside an item.

New fixtures: `#{` with blank lines at the root, in an item and in a quote (scratchpad/t/blankblock.tsm compiles today and must still compile).

**OPS bump (as designed):** False

**Fixes:** `markup-language/missed:1`, `markup-language/missed:2`, `markup-language/missed:4`, `markup-language/missed:5`, `parser-frontend/missed:2`, `parser-frontend/missed:5`, `parser-frontend/region-container-special-case`, `markup-language/region-error-recovery`, `parser-frontend/fence-double-dedent-in-containers`, `parser-frontend/trailing-text-after-block-closers-dropped`, `markup-language/same-line-trailing-text-dropped`, `parser-frontend/unterminated-let-swallows-document`, `parser-frontend/span-fidelity`

### S5 Line ownership, tentative carries, content bodies  → plan P1-08

- Phase 1 skims each leaf line into the AtomTape.
- Own classes: Leaf for inline islands, inline JS and inline-form bodies; Container for comments and block-form bodies. Pair and link frames own no lines.
- Tentative commit/revert with RevertedWindows and boundary snapshots.
- `^[`, `#f[`, `@id[` and `#let x = [` bodies are Content bodies (inline and block form) parsed by parseContent in Blocks mode with sole-paragraph unwrap.
- The block-form closer line hands its remainder to the owning row first.

**Golden impact:** Expected: none for existing fixtures (gate).
- Single-line bodies unwrap to the same inline kids.
- No fixture has a single-line body starting with a block marker (`#f[- `, `[= `, `[> `, `[1. `), as checked by grep over test/fixtures, examples and zball-io.

New fixtures:
- App B rule 4;
- a mid-line comment hiding block markers;
- cjkstar (the list survives);
- swallow (the heading survives);
- a multi-line math island inside an item;
- `#f[⏎…⏎]` with blank lines;
- `#for`-style single-line `[- x]`.

**OPS bump (as designed):** False

**Fixes:** `markup-language/inline-delimiter-scanners`, `parser-frontend/cross-line-raw-scans`, `parser-frontend/content-args-inline-only`, `parser-frontend/inline-comment-leaks-block-structure`

### S6 Front-end exports and tooling  → plan P1-09

- syntaxTokens/outline/parseJson taking FrontEndOptions, with api/ wrappers and tsrc stages.
- Engine-internal 'tsm' token provider (amends code-design.md §2).
- syntax.gen.json constants, consumed by the tree-sitter grammar.js, the TextMate build and escapeTsm.
- One highlights.scm.
- VS Code: a WASM instance in the extension host, UTF-16 conversion, and engine tokens/outline/folding/completion with a tree-sitter cold-start fallback.
- Conformance tests (b) and (e).
- Docs corrected: PackCC, the SourceMap builder, the provenance location, editor-design §5 and l.95.

**Golden impact:** New tokens/outline/astjson goldens. code/tsm-hl highlight goldens change because tsm-in-tsm tokens now come from the engine.

**OPS bump (as designed):** False

**Fixes:** `markup-language/surface-grammar-drift`, `parser-frontend/multiple-tsm-grammars`, `real-world-evidence/lexical-syntax-copies`, `parser-frontend/editor-region-builder-list`, `parser-frontend/docs-drift`

### S7 One argument, header, label and ref grammar  → plan P2-06

- ArgList with named-only form = opts object, for splices, region headers and fence info.
- Positional or mixed headers → Error with a fix-it. Markdown-style fence info (tag + info). Trailing-junk diagnostic.
- Structural content-arg insertion.
- One LabelChar class; the SpliceHead class keeps `$`.
- Bare refs use IdJoin.
- The ` <id>` suffix on region/fence openers and on every math island, emitted as `({label: …, H})`.
- 'label-orphan', 'label-conflict' and 'label-like-text' diagnostics.
- `@[a, b]` parsed into an IdList. The structured ref lowering and `@id[..]` switch on with T3's ref constructor.

**Golden impact:** None for existing fixtures:
- every opener is `k: v`, so `({H})` is byte-identical;
- `#f(k: v)` was a SyntaxError before, so no fixture uses it;
- refs in fixtures use only `-` joins (`@fig-pipe`), which are unchanged;
- no fixture label contains whitespace.
The lints are diagnostic-only.

**OPS bump (as designed):** False

**Fixes:** `markup-language/arg-grammar-unification`, `parser-frontend/region-fence-private-dispatch`, `parser-frontend/missed:3`, `parser-frontend/label-and-id-lexing-scattered`, `markup-language/universal-labels`, `real-world-evidence/missed:2`, `math/math-island-oneoff-syntax`

### S8 Statements anywhere, keyword forms, content literals, Error lowering (prerequisites: T2's hygienic namespace, per-block units and seq-of-blocks normalization)  → plan P2-12

- Error → __s.error.
- The one scope rule; ANF units for top-level blocks that contain statements or keyword forms; hoisted declarations from StmtSide.binds; 'let-redeclared'.
- Inline and line-start keyword forms with ElseChain.
- `#let x = [..]` content literals.
- Reserved-word heads → Error.
- `#use` hoisted, with auto-registration; `$.use` for bindings.

**Golden impact:** Nothing beyond T2's namespace step, which changes line 1 of every .js.txt golden once. Blocks without statements keep their nested expression byte for byte. New fixtures cover nested #let in items, quotes, regions and user bodies, the same-scope rule, inline and multi-line if/else, `#for` lists, redeclaration and error blocks.

**OPS bump (as designed):** False

**Fixes:** `parser-frontend/keyword-forms-closed-set-missing`, `parser-frontend/code-statements-top-level-only`, `parser-frontend/no-error-nodes`, `markup-language/nested-code-statements-dropped`, `markup-language/spec-features-unimplemented`

### S9 Region provenance without loss (lands atomically with T2's prov.rows and public table/figure constructors)  → plan P2-11

- Delete splitCells, Row/Cell, the codegen Row branch and regionJoin.
- Region children lower through the ordinary path.
- Call-body paragraphs carry ParaProv (Sep/Join cuts with the nested flag, and line columns) as JS-only shadow metadata.
- Region handlers receive the interior span.

**Golden impact:** All stages change for the 10 region fixtures: region/{figure,table,table-tiny}, figure/{block,float,stack,pull-diag}, style/patch, pages/paged-doc and code/tsm-hl.
- Spans become real instead of @[0,0).
- The spurious ' ' and ' | ' rewrites disappear, so non-table text is emitted exactly as at top level.
- Display math inside regions is promoted and labelled.
- Semantic change: `*a | b*` is one cell with strong('a | b') (scratchpad/t/tabemph.tsm), plus a 'row-spans-markup' diagnostic when a frame crosses a row.
- .ops files are re-recorded with no version bump.

**OPS bump (as designed):** False

**Fixes:** `markup-language/region-pipe-segmentation`, `parser-frontend/region-pipe-segmentation-in-parser`, `math/missed:0`, `markup-language/missed:0`, `math/math-span-lexer-triplication`

### S10 Prose guards, autolink, escapes, hard break, interim join predicate  → plan P3-33

- PrevIdent guard on a '#' bare value head and on '@'; Intraword guard on '*' and '_'.
- The url Verbatim row.
- Escapable = ASCII punctuation; `\` + EOL → linebreak slot.
- Extract today's cjkish predicate unchanged into support/, shared by the interim join (applied to cooked Text neighbours, with the raw-byte rule for non-Text neighbours) and emit.cc:396.
- Coalesce adjacent Text nodes.

**Golden impact:** Affected fixtures:
- doc/url-break, all stages: italics removed, link added;
- inline/emph: `un*closed` is one text node;
- figure/pull-diag: `NEED_IMAGES` is one text node;
- cjk fixtures where a Pair delimiter sits at a line join (verify; expected few or none).
style/patch is unchanged: `……⏎照` uses the same predicate, and its '、'+#style join uses the raw-byte fallback. No fixture has a backslash before a letter or at end of line in prose.

**OPS bump (as designed):** False

**Fixes:** `parser-frontend/sigil-context-rules`, `markup-language/ambiguity-hazards`

### S11 Re-entrant parse API and single fragment lowering (with T2's CallTree interpreter and sidecars in the codeblock constructor)  → plan P2-13

- tsr_parse_fragment with out-of-band holes and copy-out JSON.
- m``, m.parse and ctx.m.parse through the generic CallTree interpreter.
- Sidecars split in the codeblock constructor.
- Delete fragment.cc's Conv and api/doc.h extractSidecars.
- Conformance test (d).

**Golden impact:** Code fixtures with sidecars: notes, comments and display math in sidecars are now honoured. Their tree/blocks/html goldens change and the .ops files are re-recorded, because sidecar groups now arrive through ops.

**OPS bump (as designed):** False

**Fixes:** `parser-frontend/fragment-parallel-lowering`, `real-world-evidence/markup-reentry-missing`

### S12 Printer, escapeTsm, converter kit, translate-tsm, front-matter option  → plan P3-35

- tsm-print.mjs (print on AST JSON; printShadow once T2's schema exists).
- A shared converter kit with a real HTML entity decoder.
- Converters migrated.
- translate-tsm: span-based masking, AST-equality validation, in-place patching.
- FrontEndOptions.frontMatter through T9 config, replacing the preview.js blanking.
- Corpus fidelity checks and conformance test (c).

**Golden impact:** None to engine goldens. examples/real-world outputs are regenerated and reviewed; they are not goldens.

**OPS bump (as designed):** False

**Fixes:** `real-world-evidence/no-tsm-printer`, `real-world-evidence/converter-fidelity-unchecked`, `real-world-evidence/converter-code-duplication`, `parser-frontend/front-matter-editor-only`, `markup-language/doc-drift`

### S13 SoftBreak into the model (with T5 TextRules and T2's codec versioning)  → plan P2-10

- Add a `softbreak` kind next to `hardbreak` in ops.def.
- The AST emits a SoftBreak node at every join; Join cuts become redundant with it.
- T5 resolves it at instantiation or flattening.
- Delete inline.cc:48-59. emit.cc:396 stays: it is App C emission, and T5 may move its class into TextRules data.

**Golden impact:** All .ops files are re-recorded (OPS_VERSION bump). .ast/.js goldens change for every multi-line paragraph. tree/blocks/html stay identical when T5 reproduces today's predicate, except for joins next to markup and English curly quotes, which are bug fixes listed by T5.

**OPS bump (as designed):** True

**Fixes:** `parser-frontend/parser-owned-cjk-line-join`, `markup-language/cjk-softbreak-classifier`

### S14 Note attachment out of the parser (with T5's attach-left property)  → plan P4-07

- Delete the space relocation (inline.cc:384-397) and spaceBeforeItem's note path.
- The AST keeps `word ` + note + `. end` losslessly.
- T5 trims the preceding blank at emit for any kind with attach-left: note, #note, sidenote.

**Golden impact:** notes/* change at the .ast/.js/tree stages: the blank moves back before the marker. blocks/html change where today renders ' . end', which is a bug fix (scratchpad/t/note.tsm).

**OPS bump (as designed):** False

**Fixes:** `markup-language/footnote-sugar-oneoff`

### S15 (optional, owner decision) Config-declared inline rows  → plan 不采纳（D-L06：不开放用户自定义行内语法）

FrontEndOptions.syntax.inline rows restricted to the Pair and Content body modes. They are validated against existing openers and lowered to late-bound slots. Every export receives them.

**Golden impact:** None (opt-in). New fixtures.

**OPS bump (as designed):** False

## Not generalized (kept special)

- **The keyword-form set stays closed: let, if/else, for, while, use.** — v2 §3 l.89 documents the closed set: loop variables must bind into the content block, and JS's statement set is fixed anyway. What is general is the shared lowering, the one scope rule and reserved-head containment, not open keywords.
- **No user-defined line-level (block) syntax.** — Regions and fences are the open block forms, and their meaning is late-bound. Document-declared block rules would hit the registration paradox (v2 §4.1 l.124) and break the interruption guarantees. Config rows (S15) are inline-only and cannot own lines.
- **Strict-pair emphasis (v2 §5 l.157); Pair and link frames never own lines.** — Strict pairing is the CJK-first decision; only an ASCII intraword guard is added. Letting inline pairing decide block structure is exactly what v2 §4 l.97 rules out.
- **The ` <id>` suffix is not accepted on paragraphs or list items.** — It is ambiguous with prose (`Vec <T>`). Such text gets a 'label-like-text' lint, and labels go through `label:` options. Headings keep their documented suffix (v2 §11.1 l.239), which can be escaped with `\<`.
- **Content literals `[..]` are not legal inside argument lists or headers; `#!theorem(title: [X])` is plain JS.** — App A l.333 restricts content literals to splice-controlled positions, and inside `(..)` the brackets are JS arrays. Content values in options come from m`…` (S11) or from content bodies.
- **A positional or mixed region header is an error rather than a second encoding.** — One encoding keeps `#!name(H)` ≡ `#name(H)[…]` exact and every corpus header byte-identical. The only casualty is shorthand-only `#!fig(src)`, which has no corpus use and gets a fix-it.
- **Fragments (m``, m.parse) do not evaluate splices.** — A tag function cannot reach the document's lexical scope, and evaluating generated JS synchronously would need 'unsafe-eval'; the executor uses async Blob-URL import (executor.mjs:290-308). Values enter through holes. This keeps today's fragment.cc:81-88 behaviour.
- **The math interior grammar stays a separate island language.** — T8 owns it. The front end delimits the island, decodes only `\$`, and records display and label. A 'holes' body parameter is reserved in case T8 wants values inside math.
- **Fence bodies stay raw; there is no parse-time custom grammar.** — This is the registration paradox (v2 §4.1 l.124-128). Markup inside fences comes only through ctx.m.parse at runtime.
- **Bare splice heads stay ASCII, a `#`+identifier call is always a splice, and `$` always opens math (`$5 and $10` stays a formula).** — These are documented lexical choices (v2 §3 rule 2, §5). They are fixed by escaping. A 'digits after `$`' heuristic would add the kind of special case this design removes. Only the bare value head gets the PrevIdent guard.
- **Single-use body-mode parameters: math's pad=Display and esc={$}, code's pad=Strip, the CallChain ';' terminator, and ElseChain for keyword heads.** — They are documented parameters of the closed body modes, kept as data so tooling and the printer can see them, not free flags. The earlier rule 'a flag needs two rows' is replaced by this one.
- **Tree-sitter and TextMate remain hand-written approximations.** — Regex grammars cannot express strict pairing, jslex or tentative carries. They read their constants from syntax.gen.json and are pinned by conformance test (b) with an allow-list. Authoritative structure comes from the engine.
- **No Markdown table alignment row, and no Djot-style `{#id .class}` attributes.** — v2 §4.1 l.145 makes alignment an opener argument. Generic attribute braces are a prose ambiguity, and options plus `label:` cover the need.
- **Front matter is not a language construct.** — It is a static-site-generator convention, so it is a host option applied by every host and every export.
- **Splice stays a distinct AST kind instead of a Call with a name string.** — Its callee is arbitrary JS text, and name-string dispatch would bring back stringly-typed sugar.
- **Label sugar for list items.** — This is a recorded gap. The function form `#item({label: "x"})[..]` is the path under T2's convention, and new line syntax for it is not justified.

## Risks

- Tentative carries are the most intricate part:
  - Each unclosed opener costs one bounded re-lex, O(n·u) in the worst case.
  - Mitigations: a per-carry bound, a hard cap on reverts per leaf with a diagnostic, libFuzzer, and a timing test.
- Coordination with T2 is a hard dependency:
  - S8, S9 and S11 need T2's namespace, units, prov.rows/table constructors, seq-of-blocks normalization and the CallTree interpreter.
  - S13 and S14 need T5.
  - If they slip, S1–S7 and S10 still deliver containment, lexer unification, the argument grammar and the exports.
- Behaviour changes for existing authors, each documented as a spec amendment with a diagnostic where possible:
  - comment lines no longer split paragraphs;
  - `N.` with N≠1 no longer interrupts;
  - intraword `_`/`*` and a bare `word#x` are literal;
  - backslash before a non-punctuation character stays;
  - `\` at end of line is a hard break;
  - `<my eq>` is no longer a label;
  - `#let xs = [1,2]` is content;
  - a single-line body starting with `- `/`1. ` is a list (Typst-compatible; `1\.` escapes);
  - `*a | b*` in a table is one bold cell;
  - shorthand-only region headers error.
- Comments own lines up to their closer within the container (v2 §4.2: 'lexically dumb', 'can comment out any markup'). A stray mid-line `%--` followed later in the same container by an unrelated `--%` hides the structure in between, just like C `/*`. Without a closer it reverts.
- Hoisting `#let` declarations removes JS's temporal dead zone. A use before definition yields the `__s.unset` sentinel and a 'used-before-let' diagnostic, instead of today's module-wide ReferenceError. Inside arbitrary JS expressions the sentinel propagates as an object.
- The extension host now loads a second WASM instance, which costs memory and startup in VS Code. The tree-sitter fallback covers cold start.
- `@id[supplement]` adjacency can capture prose such as `@x[1]` or `@fig[注]`. It is gated behind T3's ref constructor and announced; `@x;[` or `\[` escapes it.
- Golden churn concentrates in S9 (10 region fixtures), S13 (OPS bump plus every multi-line paragraph's .ast/.js) and S14 (notes). Reviewers must check it as bug fixes, so keeping the steps separate is essential.
- Generic emission of provenance relies on 'call body' being decidable at lowering time, which holds for region interiors, content bodies and fragments. Constructors that receive content any other way (values built in deep JS) see no cuts; this is documented.

## Open questions (decided in PLAN.md §3)

- Should `@id[supplement]` adjacency (Typst) be granted in CJK prose, or should supplements and locators require `#ref(..)`?
- Same-scope `#let` redeclaration: a contained error (proposed; it keeps honest JS `let` semantics), or Typst-style shadowing lowered as reassignment (friendlier, but closures would see the new value)?
- Should the CJK join keep the 'none' result for em-dash/ellipsis pairs once T5 owns it? This is T5's decision; T1's interim keeps today's behaviour.
- Should the heading span include its ` <id>` label? That is more correct for anchors but churns every labelled heading; the proposal keeps a separate label span.
- Should `*a | b*` in a table produce one bold cell (proposed, following v2 §4.1's tree-level segmentation) or GFM-style two literal cells? The latter would need a parse-time barrier that applies to every region, because the parser cannot know which regions are tables.
- Should S15's config-declared inline rows be adopted at all, and should Content-shaped rows be allowed in addition to Pair?
- Should m.parse accept an explicit `{scope}` object so that bare value heads (`#x`, `#a.b`) in fragments resolve by property lookup, without eval?

## Changelog (critique responses)

- [C1 major] Stmt rows with a blank-line bound break valid `#{` blocks. ACCEPTED. Statements are now Own::Container: balanced to container exit, which is EOF at the root, as today at linepass.cc:376. Recovery at the first blank line applies only when the balanced scan fails. Verified: scratchpad/t/blankblock.tsm compiles today. Fixtures were added at the root, in an item and in a quote (S4).
- [C1 major] A root-level Container carry plus free closer lookahead lets a stray `]` swallow headings. ACCEPTED; content bodies restructured into two forms:
  - Inline form: bounded by the leaf.
  - Block form: `[` ends the line, and only a `]`-led line at a column no greater than the opener's indent closes it.
  Partly rejected: the alternative bound 'at the next unindented interrupting starter', because column-0 headings inside `#for (…) [` bodies are the primary use (v2 l.104). The splice-row and S5 carries are now consistent, and the I7 wording is corrected. swallow.tsm keeps its heading.
- [C1 major] Pair/Frame carries let inline pairing suppress block starters. ACCEPTED. Line ownership is now a property of the body mode (enum Own). Pair, LinkText, IdList and Ident never own lines (v2 §4 l.97), and open entries flush as literal at a structural boundary. cjkstar.tsm keeps para+list.
- [C1 major] Closer lookahead creates backward dependencies, so incremental re-lex is unsound. ACCEPTED. There is no lookahead pass; instead, tentative commit/revert. The snapshot contract to T9 is LexState at top-level boundaries (empty by construction) plus RevertedWindows; an edit invalidates back to the earliest window containing it (I8).
- [C1 major] `[` frames on the single delimiter stack block strict-pair closers. ACCEPTED. Content bodies left the inline stack: they use one island-aware bracket counter and a recursive parseContent. A bare `[` is a weak LinkText entry that a Pair closer skips (CommonMark 'look for opener'). bracket.tsm output is preserved, and both cases are S3 fixtures.
- [C1 major] Sep/SoftBreak are not barriers, so cells and rows merge silently; the continuation rule is missing. PARTIALLY ACCEPTED. I took the critic's second option, lossless per-line provenance, rather than barriers. v2 §4.1 l.120/l.145 defines segmentation at tree level ('top-level segmentation of its inline runs', 'embedding-proof'), and the parser cannot know which regions are tables (registration paradox), so a barrier would apply to every region paragraph.
  - Cuts carry a `nested` flag, and T2's prov.rows reports 'row-spans-markup' instead of merging silently.
  - `*a | b*` is listed as an S9 semantic change, and it is an open question.
  - Line and block columns are recorded so that T2 implements v2 §4.1's indented continuation.
- [C1 major] The S10 interim CJK classifier regresses style/patch and English quotes, and S13 deletes emit.cc:396. ACCEPTED. The interim predicate is exactly today's cjkish (inline.cc:50), extracted to support/ and shared with emit, with non-Text neighbours falling back to the raw-byte rule. Curly quotes are left to T5. Verified: emit.cc:396 is App C defined-width emission, so S13 no longer deletes it. style/patch was analysed and is unchanged.
- [C1 major] Scoping is inconsistent, and document-scope ANF conflicts with per-block units. ACCEPTED. One scope rule: statement scopes are the document and keyword bodies, and content never opens a scope, built-in or user (v2 §2 l.71, §3 l.89). Declarations are hoisted (`let n = __s.unset`) and statements become assignments inside T2's guarded units. Redeclaration policy: 'let-redeclared' is a contained error (open question).
- [C1 major] Keyword forms have no inline row, and the line-remainder rule breaks `] else [`. ACCEPTED. A kwform INLINE CallChain row with ElseChain was added. The ANF lowering needs no IIFE. The block-form closer line offers its remainder to the owning row before paragraph re-entry. Fixtures cover inline and multi-line if/else.
- [C1 major] The dual header encoding is ambiguous and changes the meaning of shorthand, string/computed keys and spread. ACCEPTED, restructured as one ArgList rule shared by splices, regions and fences:
  - A named-only list is the opts object `({…})`, verbatim, so every JS PropertyDefinition form works and all 795 corpus headers stay byte-identical.
  - Otherwise the list is verbatim JS.
  Shorthand-only headers become a contained Error with a fix-it; corpus use is zero, and shorthand.tsm is a constructed probe. Content literals are not legal in argument values (App A l.333), so the examples now use m``.
- [C1 major] The AstNode grows to 376 B and the skim is a second lex, against the I9 claim. ACCEPTED. The node is 32 bytes or less, with side records. The AtomTape means each byte is lexed once for atoms. An AST-bytes metric was added to the bench gate.
- [C1 major] Several golden-neutral claims were false. ACCEPTED:
  - PROMOTE is dropped and the codegen peephole stays as an interim L1 rule, so S2 is byte-identical including math/display, eqref and stretch .ast.
  - No SoftBreak AST node exists before S13; joins merge as today and are recorded as cuts.
  - Fence offsets stay scalar except for bodies inside prefix or column containers. inline/fence-edge, an indented top-level fence, is unaffected (checked).
  - style/patch was analysed for S10.
- [C1 major] GuardAlnum on '#' removes intraword splices. ACCEPTED. The guard applies only to a bare value head with no `(`/`[` continuation, so `H#sub[2]O` stays a splice. A grep over the fixtures found no `[A-Za-z0-9]#[A-Za-z]` in prose.
- [C1 minor] Auto mode contradicts v2 §4 l.104. ACCEPTED. Content bodies are always Blocks, with sole-paragraph unwrap. Checked: no fixture or corpus body starts with a block marker on a single line.
- [C1 minor] The in-band U+FFFC hole encoding is ambiguous. ACCEPTED. Holes are out-of-band ranges. A hole inside an island uses its stringified text. Buffer ownership is copy-out, valid until the next tsr_* call.
- [C1 minor] `@[` is treated both as a frame and as an IdList. ACCEPTED. `@[` is an IdList body where only `]`, `,` and `\` are significant. The `@id[..]` supplement is a separate Content body.
- [C1 minor] SyntaxTable inconsistencies: flags, IdStart without `$`, guard names. ACCEPTED. The flags are replaced by closed body modes with documented parameters and two guards (PrevIdent, Intraword), each used by two rows. The SpliceHead class includes `$` (App A l.330, jslex.h:90-92). Region names keep that class, as linepass.cc:304-306 already does, and the earlier IdJoin claim was wrong.
- [C1 minor] The 'fragments get the full language' claim and sidecar splices. ACCEPTED. Splices in fragments stay literal with info, today's fragment.cc:81-88 behaviour, and the claim now reads 'full markup; values through holes'.
- [C1 minor] Factual and contract details. ACCEPTED:
  - the extension host needs its own WASM and UTF-16 conversion (preview.js:1, server.js:1-4, tokens.js:1-6);
  - -Werror=switch does not forbid default arms, so S1 uses -Werror=switch-enum scoped to the front-end translation units (-Wall already gives -Wswitch, CMakeLists.txt:27);
  - fuzzing targets the C++ entries, and the JS interpreter is property-tested;
  - the tab stop is an App B constant, not a host option.
- [C1 overlaps] All accepted:
  - T2: hoisted-declaration units and one canonical header record.
  - T2 prov.rows: the barrier guarantee is replaced by the nested-cut flag and diagnostic.
  - T5: join policy and the em-dash/ellipsis class stay as today until T5.
  - T9: snapshot contract, and the tab stop removed from settings.
  - T3: the suffix is sugar for `label:` with 'label-conflict', and outline lists literal labels.
  - T8: the math escape set is exactly {$} decoded, with `\\` and other pairs passed verbatim, so today's src bytes are unchanged.
- [C2 major] The carry sits on inline rows, and `^[` and `#note[` differ. ACCEPTED (same restructure as C1). Ownership follows the body mode and form, not the row. `^[` is a Content body with exactly `#note[`'s bounds and parse mode.
- [C2 major] The `#{`/`#let` blank-line bound is wrong. ACCEPTED (same fix as C1).
- [C2 major] `$` was dropped from splice heads. ACCEPTED. A separate SpliceHead class `[A-Za-z_$][A-Za-z0-9_$]*` was added. Verified that `#$.ref("x")` compiles today; a fixture was added.
- [C2 major] The argument-grammar examples contradict the JS-verbatim rule; the encoding is dual; there is no header→ctor mapping; region names. ACCEPTED:
  - With the named-only rule, `#callout(kind: "warn")[..]` is valid and the header→ctor mapping is identical: the same ArgList gives the opts object for ctor(opts, ...content).
  - `[..]` values are not legal in arguments, so the examples were fixed to use m``.
  - Region names already use the splice-head class (linepass.cc:304-306).
- [C2 major] Sep text nodes change emission; there is no provenance for `#table[..]` or m.parse; Pair×Sep merges cells. ACCEPTED:
  - Provenance is zero-width (cuts on the paragraph; Text is not split), so emission is identical in and out of regions.
  - It is emitted for every call body: regions, content bodies and m.parse.
  - Pair×Sep: PARTIALLY ACCEPTED. A nested-cut diagnostic replaces the recommended table-context barrier, because 'table context' is unknowable at parse time.
- [C2 major] Fragments keep a second lowering, and evaluation was not considered. PARTIALLY ACCEPTED. There is now a single lowering: T2 lowers AST→CallTree once, printed for documents and interpreted generically for fragments. The binary AST buffer, lower-ast.mjs and the per-sugar walker are deleted.
  REJECTED: evaluating generated JS for fragments.
  - m is a function, not a direct eval, so document bindings are unreachable anyway.
  - The executor loads code only by async Blob-URL import (executor.mjs:290-308), while m`` must return synchronously.
  - `new Function` would add an 'unsafe-eval' CSP requirement.
  The splice limit is documented, and an explicit `{scope}` option is an open question.
- [C2 major] Auto mode, and list grouping stays at parse time. ACCEPTED. Content bodies are always Blocks. Grouping of adjacent same-marker-class lists moves to T2's L3 normalization. The marker class (Bullet/Auto/Explicit) is carried through the AST and the lowering.
- [C2 major] Two scope rules, incompatible with per-block containment. ACCEPTED (joint with C1). The statement semantics are T1's decision record; the containment mechanics are T2's.
- [C2 minor] AttachLeft is a typographic decision in the parser. ACCEPTED. It is no longer a table flag. Today's relocation stays only until T5's attach-left property lands (new step S14), which applies to ^[ ], #note and sidenote alike.
- [C2 minor] PROMOTE is a placement decision in L0, and the S2 golden claim was false. ACCEPTED. PROMOTE is dropped, and the peephole stays as an interim L1 rule until T2's normalization.
- [C2 minor] syntax.def mixes layers; single-use flags; per-row carry; `@[` as a frame; generated regexes. Mostly ACCEPTED:
  - no LOWER templates; slot names only;
  - ownership per body mode and form;
  - the guards merged into two;
  - `@[` removed from the frames.
  PARTIALLY ACCEPTED: tooling generation. Only a constants file, syntax.gen.json, is generated. The structural tree-sitter and TextMate rules stay hand-written and conformance-tested.
- [C2 minor] Paragraph and item labels stay silently literal. ACCEPTED. A 'label-like-text' lint (suppressed by `\<`) is part of the corpus checks. The item-label path is recorded as a gap with the function form.
- [C2 minor] Keyword forms exist only as block rows. ACCEPTED (the kwform INLINE row).
- [C2 minor] `#use` binds nothing. ACCEPTED. `#use` keeps the documented import plus `fences` auto-registration (architecture §4.1), and bindings are ordinary JS through `#let h = await $.use(spec)`. No new syntax.
- [C2 minor] translate-tsm and printer inputs. ACCEPTED:
  - translate-tsm masks by engine token spans within whole paragraphs, validates by AST equality and patches in place (verified the line masking and byte-exact validation at translate-tsm.mjs:1-7,46-70);
  - print takes AST JSON losslessly;
  - printShadow depends on T2's per-kind schema and a sugar-inverse map;
  - the marker class is carried.
- [C2 minor] FrontEndOptions plumbing, the tab stop, the extension host and the NEED_TOKENS detour. ACCEPTED:
  - every export takes FrontEndOptions;
  - the tab stop is a language constant;
  - the extension host gets its own WASM;
  - the engine tokenizes 'tsm' internally, amending code-design.md §2 with a stated exception.
- [C2 minor] AstNode layout. ACCEPTED (same as C1).
- [C2 minor] Hard line break. ACCEPTED. `\` + EOL is the linebreak slot, reusing ops kind hardbreak (ops.def:38), and lands in S10 together with the Escapable change. Verified that today it prints a literal backslash and that no fixture uses it.
- [C2 minor] Doc-example re-entry pollutes labels and counters. ACCEPTED as a T3 dependency (fragment scope), documented as a limitation until then.
- [C2 minor] GuardAlnum on '#'. ACCEPTED (same fix as C1).
- [C2 minor] Subsumption and migration bookkeeping. ACCEPTED:
  - every id maps to exactly one step or one owning theme;
  - ctor-names-are-reserved-words is T2-owned and is only a prerequisite of S8;
  - reference-forms-closed is owned by T3, with T1's surface described;
  - spec-features-unimplemented is owned by S8;
  - keyword bodies move to S8;
  - -Werror is scoped to the front-end translation units.
- [C2 overlaps] Accepted:
  - T2/T1 ownership seam as proposed: T1 owns the AST, parse entries and statement semantics; T2 owns lowering, namespace and units.
  - T2 owns list and display normalization.
  - T5 owns attach-left and softbreak.
  - T3 owns the fragment scope and semantic label diagnostics.
  - Content-valued options: T1 rules `[..]` illegal in arguments, so values come from m`` or bodies; node-valued attributes are T2's.
  - T8 may parameterize the math body mode with holes.
  - T9 owns both ABIs, every export carries options, and the engine serves 'tsm' tokens.
