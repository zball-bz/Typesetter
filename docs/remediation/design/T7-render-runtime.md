# T7-render-runtime

> **Audit design input (generated, English).** Produced by the 2026-10-05 audit (architect → two adversarial critics → revision). Where it conflicts with `docs/remediation/PLAN.md` (decision register §3) or with `INTEGRATION.md` (conflict resolutions), PLAN.md wins, then INTEGRATION.md. Step ids are mapped to plan steps in the Migration section. Line numbers refer to commit ecc3a89.

## Thesis

Paint annotates layout; it never performs a second layout. T6 owns one record for each materialized line and box, kept in DOM order. That record holds geometry, justification, separator, anchor, track, marks and realised gaps. T7's paint pass adds only what layout cannot know: runs formed at runId boundaries, typed inline payloads, presentation hooks and copy specs. Every typeset backend (screen and paged) is then a stateless walk of that DisplayList through one HtmlWriter and AnchorNamer. The semantic backend walks the resolved tree through the same writer. A T3 class declares a category, a `like:` base and slots. One data-complete PresentationMap turns that meaning into what each backend shows: elements, attributes, slot elements, ARIA, frames, copy and preview policy. The map is declared through one channel, the T4 settings key render.classes.<cls>. Built-in kinds are ordinary entries in it, so user classes stand on equal footing with figure and footnote. At the render→shell boundary the engine sends a per-block result that carries a generation stamp and only the blocks the shell does not already hold. The shell keeps one session per container, one commit path and a core copy contract. Every other feature is a Behavior that reads only declared data and calls only typed engine operations. Content features, classes and behaviors stay open. The paint primitive set, the copy/DOM contract and the render protocol stay closed and versioned.

## Diagnosis

Three undeclared boundaries around the renderer explain almost every ad-hoc item.

(1) layout→render has no IR. The renderer receives LineBox, a union tagged by the magic `special` (engine/src/layout/layout.h:20, where 5 is undocumented), plus the TopBlock/FlowUnit/ContentNode graph, so it has to finish layout itself:
- it re-derives the CJK-stretch predicate (typeset_html.cc:531-535, :558-562 vs layout.cc:514-521) and run boundaries (:584-590);
- it reads the display-math advance and eqno offset from Config (:226-249) and model args (:242-245);
- it keeps mutable anchor state (lastAnchored :179, :612, :752);
- pagination repeats the ladder inside the serializer (:642-765).
The verified bugs follow from this: dropped and duplicate paged ids, ref runs swallowing prose (cite/basic.html.txt), and duplicate style attributes (:416-421).

(2) Meaning never reaches presentation as data:
- roles are strings tested in each layer (semantic_html.cc:286, emit.cc:781, resolve.cc:166);
- the typeset DOM gets only style bits;
- copy semantics are an accident of which node kind produced the text (BF_REF→data-syn, typeset_html.cc:454) and which of four line producers set `join` (layout.cc:66-78, :587; none for cells and sidecars).
Users therefore get no element, theme hook, copy rule or preview for their own constructs.

(3) render→shell is an implicit ABI made of bytes, names and timing:
- chunkParas regex-matches attribute order (shell.mjs:264);
- popups match `#tsr-fn-` plus `.tsr-sup` and walk sibling lines (:146, :175, :133-143);
- the `tsr-` id namespace is shared by the resolver (resolve.cc:113, :263, :292), both serializers and the shell's own nodes. Two documents on one page collide (zball-io/_site/p/hello-typesetter/index.html has id=tsr-intro twice), and print() deletes a heading labelled print-root (shell.mjs:436);
- the worker dispatches messages concurrently while paginate mutates the live doc (worker.mjs:228-246, :269-280).

The rest is missing shared primitives (two escapers, two diverging style mappers) and missing settings and resource channels (T4/T9).

Layering correction:
- L6 owns line and box records and pagination, indexed by layout ids.
- L7 paint annotates those records and never re-decides them.
- L7 has two entry points that share one presentation kit: the resolved tree (semantic backend, before measurement, runs in Node) and the DisplayList (typeset and paged).

## Abstractions

### DisplayList (paint annotation over T6 layout records)

**owner_layer**

L7 Paint at the L6→L7 boundary. Paint code lives in engine/src/paint/{displaylist.h,paint.cc,dump.cc}. It reads T6's LaidOutBlock (engine/src/layout/laid_out.h); T6 owns that record, its shape is agreed here, and S3 introduces it as an adapter over LineBox. Backends are engine/src/render/typeset_html.cc and render/paged_html.cc.

**purpose**

One render IR for every typeset backend, with no second copy of layout decisions. T6's record holds every geometric and structural decision. Paint adds only run formation, typed payloads, hooks and copy specs. Backends read only LaidOut* and DL* fields, never Config, FlowUnit, LinebreakBlock or ContentNode, so they are stateless walks. Content features stay open; paint primitives are closed.

**definition**

```
// engine/src/layout/laid_out.h — OWNED BY T6
struct Track { enum class K : u8 { Main, Cell, Sidecar, Caption, Furniture } k; u16 index; };
enum class Just : u8 { Justified, Ragged, Centered, Fixed };            // derived from LineEnds
enum class Sep  : u8 { Newline, Space, None, Tab, Row, Para, Custom };  // copy separator AFTER the line
struct Placement { enum class Edge : u8 { Start, End } edge; Su x, y; }; // relative to the line origin; End = right-anchored, needs no width
struct GapReal { u32 item; double px; enum class Carrier : u8 { WordSpacing, LetterSpacing, MarginL, MarginR, Spacer } c; };
struct LineMark { StrRef name; };                                       // hl, diff+, focus … from content line attributes
struct LaidOutLine { Su x, y, w, h, baseline /*CSS inline formula incl. strut*/; Just just; Track track; Sep sep; StrRef sepText;
  AnchorId anchor;   // first ANCHOR-ELIGIBLE line (text, code, display math, image) of a registry-winning anchored unit
  ClassPath cls; SlotId slot; std::vector<LineMark> marks; std::vector<GapReal> gaps; double wordSpacingPx;
  std::optional<Placement> gutter /*marker or line number: edge End*/; LineStyle ls /*code-row centring line-height, font features*/; ItemRange items; Span src; };
struct LaidOutBox { enum class K : u8 { Rule, Image, Raw, Glyphs, Frame } k; Su x, y, w, h; Insets border /*BoxModel*/; u8 openEdges /*frame fragment at a page cut*/;
  AnchorId anchor; ClassPath cls; Span src; };
struct LaidOutBlock { u32 pid; Span src; Su y, h, gapAfter; std::vector<std::variant<LaidOutLine, LaidOutBox>> nodes; /* materialization order == DOM order */ };

// engine/src/paint/displaylist.h — T7
using SynKind = StrRef;   // open set. Built-ins: hyphen marker ref prefix math eqno cont indent boundary image error backlink.
                          // User kinds arrive through T2's $.synthetic attribute.
enum class CopyMode : u8 { Text, Omit, Replace };
struct CopySpec { CopyMode mode; StrRef text; u32 group /*a split formula copies once*/; };
struct RunLink  { StrRef href; AnchorId target; ClassId targetCls; };
struct GlyphLeaf { Su x, baseline; double px, lineHeightPx; StrRef text; u8 fontRole; };   // flattened by T8
struct RuleLeaf  { Su x, y, w, h; };
struct Glyphs   { std::vector<GlyphLeaf> g; std::vector<RuleLeaf> r; StrRef src; };
struct ImageRef { StrRef src, alt; };
struct RawHtml  { StrRef html; };
using Payload = std::variant<Glyphs, ImageRef, RawHtml>;
struct DLRun { StrRef text; StyleRef style /*T4 ingest-validated values + class tokens*/; RunLink link; AnchorId anchor; StrRef title;
  SynKind syn; CopySpec copy; std::optional<Placement> at; Span src /*exact sub-span or empty (T5)*/;
  double letterSpacingPx, marginLPx, marginRPx, fixedWidthPx /*copied from GapReal*/; u8 squeeze /*contract class L|R*/; };
struct DLSpacer { double widthPx; SynKind syn; };
struct DLInlineBox { Payload p; Su w, asc, desc; std::optional<Placement> at; StyleRef style; RunLink link; SynKind syn; CopySpec copy; Span src; };
using DLItem = std::variant<DLRun, DLSpacer, DLInlineBox>;
struct DLLine { const LaidOutLine* geo; Hooks hooks; std::vector<DLItem> items; };
struct DLBox  { const LaidOutBox* geo; std::optional<Payload> p /*none for Rule and Frame*/; StyleRef style; RunLink link; Hooks hooks; SynKind syn; CopySpec copy; };
struct DLBlock { const LaidOutBlock* geo; BlockKey key /*128-bit value hash*/; Hooks hooks;
  std::vector<DLBox> frames /*Frame boxes only: paint-only backgrounds, emitted first*/;
  std::vector<std::variant<DLLine, DLBox>> nodes /*exactly geo->nodes order*/; };

DLBlock paintBlock(const LaidOutBlock&, const PaintEnv& /*PresentationMap, CopyPolicy*/);
void writeTypesetBlock(const DLBlock&, HtmlWriter&);                          // screen backend
void writePagedSheet(const DisplayList&, const PageResult::Page& /*T6: bands over layout ids + frame fragments*/, HtmlWriter&);
std::string dumpDisplayList(const DisplayList&);                              // tsrc --stage=dl (debug only, no goldens)

Rules:
(1) A backend reads only LaidOut* and DL* fields.
(2) One line node becomes one absolutely positioned nowrap element. Every spacing value is a field, realised by T6 (GapReal) and copied by paint.
(3) Runs break exactly where T5's runId changes (style, link, syn kind, anchor, script), so data-syn, id and data-s describe whole runs.
(4) Anchors, separators and placements are data. Serializers hold no state.
(5) DOM order equals layout materialization order. Only Frame boxes are hoisted.
(6) Display math is one line holding a placed Glyphs inline box plus the eqno run. Interim: Placement edge=End. Once T6 places a measured tag, it gets an explicit x/y.
(7) DLBlocks are never cached, because they point into the per-Doc arena. The cache holds serialized block HTML (see RenderResult).
```

**surface**

No document syntax.

Tooling: `tsrc --stage=dl` (a debug dump with no goldens).

Extension contract for neighbouring themes: a new content feature (T5 InlineObject, T6 BlockLayouter, T8 construct) lowers into DLRun, DLSpacer, DLInlineBox{Glyphs|ImageRef|RawHtml} or a LaidOutBox kind. It never adds a backend branch. Page furniture that T6 lays out arrives as ordinary blocks on a Furniture track.

**replaces**

- engine/src/layout/layout.h:7-32 LineBox.special plus per-feature fields (codeLine, cbLo/cbHi, codeCont, contCols, snapLatinPx/snapCjkPx, codeHl, marker, markerStyle, 3-state join)
- engine/src/render/typeset_html.cc:177-602 renderLineBox ladder (special==3 :183, ==1 :198, ==4 :208, ==5 :254, ==2 :298)
- engine/src/render/typeset_html.cc:242-245 model reach-back for the display-math src arg
- engine/src/render/typeset_html.cc:226-230, :246-249, :233-238, :332-343, :627 Config reads in the renderer
- engine/src/render/typeset_html.cc:531-535, :558-562 duplicate CJK-stretch predicate (third copy layout.cc:514-521), replaced by T6 GapReal
- engine/src/render/typeset_html.cc:546-552, :584-590 run-coalescing loops that ignore BF_REF
- engine/src/render/typeset_html.cc:179, :211-216, :265-270, :310-318, :612, :752 lastAnchored anchor state
- engine/src/render/typeset_html.cc:642-765 band cutting inside renderPages (moved by T6 to layout/paginate.cc over layout ids)
- engine/src/render/typeset_html.cc:220, :304, :324-325 data-ragged/data-cell audit hints decided per feature (→ Just, Track)
- engine/src/render/typeset_html.cc:105-108, :474-478 mathfont constants and MathBox walk in the serializer (→ T8 flattened leaves)
- runtime/src/main/shell.mjs:30-31 marker placement by CSS right:100% (→ Placement edge=End; the default serializes byte-identically)

### HtmlWriter + AnchorNamer

**owner_layer**

L7 Paint, a serializer kit shared by the typeset, paged, semantic and fragment backends: engine/src/render/html_writer.h.

**purpose**

The single place where elements, attributes, style declarations and ids are assembled. Duplicate attributes, escaping drift, unallowlisted elements and id-scheme drift become impossible by construction. The DOM id namespace becomes a per-document render concern, reserved for document labels only.

**definition**

```
// engine/src/render/html_writer.h
struct AnchorNamer {
  std::string prefix = 'tsr-';   // setting render.idPrefix: per document, identical for static export and hydration;
                                  // secondary instances (print root, second article) use their own
  bool suppressIds = false;       // fragments (previews)
  void id(std::string& out, AnchorId a) const;    // prefix + HTML-attribute escape(spelling): byte-identical to today (tsr-h-1.1 stays)
  void href(std::string& out, AnchorId a) const;  // '#' + id
};
// Only T3 registry winners reach the namer. Labels containing ASCII whitespace are rejected or mapped at registration (T3).
// The shell owns NO ids in this namespace.
class HtmlWriter {
 public:
  HtmlWriter(std::string& out, const AnchorNamer&, DiagSink&);
  HtmlWriter& open(ElementName);                  // allowlisted enum, never a free string
  HtmlWriter& cls(ClassToken);                    // CSS-identifier validated, de-duplicated
  HtmlWriter& attr(AttrName, std::string_view);   // allowlisted name, escaped value. Repeated key: style merges; others are first-wins
                                                  // plus a diagnostic (assert only in debug and native-test builds)
  HtmlWriter& px(CssProp, double);                // fmtPx, merged into ONE style attribute
  HtmlWriter& style(const ResolvedStyle&);        // T4 typed values, validated at ingest (the measurer reads the same values)
  HtmlWriter& anchor(AnchorId);
  HtmlWriter& text(std::string_view);             // escapes & < > " '
  HtmlWriter& trustedRaw(std::string_view);       // raw.html: the ONE unescaped path (document-model §9)
  void end();
};
// No cross-element state. A projection that elides an element hoists that element's anchors explicitly (list projection → <li>).

Golden-runner invariants (S1), checked on every *.html, *.paged and *.semantic output:
- every internal href=#x has exactly one id=x;
- no element repeats an attribute;
- ids are unique;
- only allowlisted elements appear outside trustedRaw.
```

**surface**

Setting render.idPrefix, registered in T4's settings registry, which also declares its precedence. JS: handle.idPrefix and RenderResult.head.idPrefix.

**replaces**

- engine/src/render/typeset_html.cc:9-20 and engine/src/render/semantic_html.cc:7-18 byte-identical escapers
- engine/src/render/typeset_html.cc:44-90 styleInto/runStyleAttr vs semantic_html.cc:63-111 divergent style mapping (the typed CSS half is T4's)
- engine/src/render/typeset_html.cc:416-421 second style attribute on snap-kerned code runs
- engine/src/render/typeset_html.cc:483-484 hyphen run opened outside its link plus a string-inserted duplicate data-syn
- engine/src/render/typeset_html.cc:213, :267, :314, :450 and semantic_html.cc:46, :137 per-site 'tsr-' id writing
- engine/src/resolve/resolve.cc:113, :263, :292 resolver-built '#tsr-…' URLs (refs keep only a target AnchorId)
- engine/src/render/semantic_html.cc:205-211 tight-item shortcut that drops the para's id
- runtime/src/main/shell.mjs:436-469 'tsr-print-root'/'tsr-print-style' ids inside the document label namespace

### PresentationMap (class → per-backend presentation, declared through one channel)

**owner_layer**

L7 Paint: engine/src/render/presentation.{h,def}.

The only declaration channel is T4's settings key render.classes.<cls>. T7 owns its schema; the settings parser validates it as a JSON value, and the schema is versioned and fuzzed with that parser. `$.element(…, { render })` is sugar that lowers to that settings write.

The other neighbours each own one thing:
- T3 declares only category, `like:` and slots;
- T6 owns frame geometry (BoxModel insets and border widths);
- T4 owns class tokens, paint values and per-key precedence.

**purpose**

Map a class to how each backend presents it. The class is either a T3 element class or a built-in kind; both live in the same table. Each entry covers:
- semantic HTML: element, attributes from args, slot elements, wrappers, ARIA, structural projection;
- typeset hooks: data-role, frame classes;
- ref preview and copy policy;
- the separator after a unit, and how generated text is copied.

Built-ins are rows in presentation.def, not C++ branches, so user classes get the same expressive power and can inherit from any class with `like:`.

**definition**

```
// engine/src/render/presentation.h
struct AttrRule    { AttrName html /*allowlist: title lang dir cite datetime open start reversed value abbr colspan rowspan data-*; never on*, style, src, href*/;
                     std::variant<ArgKey, StrRef> from; };
struct ElementSpec { ElementName el;   // allowlist: flow, sectioning and phrasing elements only.
                                       // Never raw-text or RCDATA (script style textarea title xmp noscript …), embedded or interactive elements.
                     StrRef elFromArg /*e.g. heading 'h{level}', clamped to 1..6*/; std::vector<ElementName> wrappers /*codeblock pre>code*/;
                     std::vector<AttrRule> attrs; ClassList classes; };
struct SemanticShape { ElementSpec self; std::map<SlotId, ElementSpec> slots /*caption title body tag term def summary …*/;
                       AriaRole aria /*validated against WAI-ARIA 1.2*/; bool inlineLevel; ProjectionId projection /*none|list|table|codeblock|math*/; };
struct TypesetHooks { bool dataRole /*data-role ATTRIBUTE on .tsr-para and lines; never a class*/; ClassList frameClasses /*only on the T6 frame box*/; };
struct RefPolicy    { CopyMode copy; u8 preview /*none|inline|block*/; };
struct ElementPresentation { ClassId like; SemanticShape semantic; TypesetHooks typeset; RefPolicy ref;
  CopyMode generatedCopy /*generated text (prefix) inside this class*/; Sep sepAfter /*Para|Newline after a unit of this class*/;
  std::map<SlotId, bool> omitInFragment /*e.g. the footnote backlink slot*/; MathProjection math /*boxes|source|mathml per profile page|feed*/; };
class PresentationMap {
 public:
  static PresentationMap defaults();   // presentation.def: para p; heading h{level}; list ul|ol[start] (projection list); item li; quote blockquote;
                                       // codeblock pre>code.language-x (projection codeblock, sidecar slot); table (projection table);
                                       // term dl>dt+dd (slots term/def); collect nav|section; figure figure + figcaption (slot caption); footnote; …
  void apply(const SettingsNode& renderClasses, DiagSink&);   // an invalid element, role or attribute raises a diagnostic and falls back to div[data-role]
  const ElementPresentation& of(ClassId) const;               // explicit > like-chain > generic div[data-role] + tsr-role-<name>
};

Projections are the only code, and any class may select one:
- list: emits li, and hoists an elided tight paragraph's anchors onto the li;
- table: rows and cells;
- codeblock: pre>code, with the sidecar slot rendered inline using the fence's DECLARED marker;
- math: boxes, source or mathml.

Rules:
- Class tokens are validated as CSS identifiers.
- In the typeset DOM, role classes never sit on ancestors of measured runs; only data-role attributes and frame boxes carry them.
- Metric styling of a class goes through the style stack or T4 rules, so measurement sees it.
```

**surface**

In a document:

#{ $.element('theorem', { like: 'aside', counter: 'thm', supplement: 'Theorem', slots: ['title', 'body'],
     render: { semantic: { element: 'section', aria: 'note', slots: { title: { element: 'header' } } },
               typeset: { frame: true }, ref: { preview: 'block', copy: 'text' }, generatedCopy: 'omit' } }) }

As host or document settings:

{ render: { classes: {
    spoiler: { semantic: { element: 'details', slots: { title: { element: 'summary' } } } },
    kbd:     { semantic: { element: 'kbd', inline: true } },
    figure:  { semantic: { slots: { caption: { element: 'figcaption' } } } } },
  math: { page: 'boxes', feed: 'source' } } }

**replaces**

- engine/src/render/semantic_html.cc:284-306 role=='figure' branch with one never-closed figcaption
- engine/src/render/semantic_html.cc:307-317 generic div[data-role] as the only shape for any other role
- engine/src/render/semantic_html.cc:195-337 per-Kind code for heading level, list ol/ul/start, pre>code, img attrs (moved to presentation.def plus four projections)
- engine/src/render/semantic_html.cc:241-251 sidecar group skipped as 'display-layer only'
- engine/src/render/semantic_html.cc:155-160, :260-266 math source fallback and missing equation tag
- engine/src/render/semantic_html.cc:281, :331-333 inline max-width and text-align styles
- engine/src/emit/emit.cc:816-819 group flattening that leaves no role or frame for the typeset DOM
- engine/src/render/typeset_html.cc:236, :276 one-off feature classes (tsr-eqno, tsr-img) as the only hooks
- runtime/src/main/shell.mjs:175 footnote recognition by .tsr-sup plus href prefix

### ContentText projection + CopyPolicy

**owner_layer**

L7 Paint: engine/src/paint/content_text.h holds the native projection and the policy. runtime/src/main/copy.mjs is the DOM mirror of the same rules and part of the shell core.

**purpose**

One declared answer to 'what is the content text of this rendered thing'. It drives clipboard copy, the optional screen-reader text layer, preview plain text and a native golden dump. Policy is decided per synthetic kind and per class, not by the code path or line producer that made the text.

**definition**

```
// engine/src/paint/content_text.h
struct CopyPolicy {   // setting copy.policy: { <synKind>: 'text'|'omit'|'replace' } (T4 registry),
                      // overlaid per class by PresentationMap ref.copy and generatedCopy
  Map<SynKind, CopyMode> byKind;   // defaults reproduce today:
                                   //   hyphen marker cont indent boundary eqno image error ref backlink = Omit
                                   //   math = Replace(source); prefix = Text
};
std::string contentText(const DisplayList&, DLPos from, DLPos to);   // tsrc --stage=text, a11y layer, preview text

DOM contract (successor of document-model §9.3/§9.4):
- Encoding:
  - Text → plain run, no data-syn;
  - Omit → data-syn=<kind>;
  - Replace → data-syn=<kind> data-copy=<text> [data-copy-group]. Math keeps data-src as an alias, so its bytes do not change.
- Ownership: the core copy handler owns the clipboard iff the range intersects at least one .tsr-line of a commit root, even if the result is empty. Otherwise native copy applies. In mixed ranges, non-line parts (overlay, host content, a11y layer) contribute their native text in document order.
- Lines: for each intersecting line, in order:
  - a line with NO items (a blank code row) contributes '' plus its separator;
  - a line whose items are all omitted contributes nothing, not even its separator;
  - otherwise it contributes the selected text of Text runs, each data-copy once per group, then the separator.
- Separator = data-join: space | none | tab | row | para | custom (+ data-copy-sep); absent = newline. A change of block identity implies para; block identity is .tsr-para on screen and .tsr-band[data-b] in paged output.
- Sep comes from T6's line record (break item, cell or row end, unit end with the class's sepAfter), never from alignment.
```

**surface**

Settings: copy.policy (one map), a11y.textLayer (bool), a11y.mathLabel (bool, default on).

Per-class overrides: render.classes.<cls>.ref.copy and render.classes.<cls>.generatedCopy.

JS: handle.contentText(range).

Tooling: tsrc --stage=text, with goldens for a curated copy set: tables, sidecars, code, cite, notes, math, figure captions.

**replaces**

- runtime/src/main/copy.mjs:15-19 math-only data-src special case
- runtime/src/main/copy.mjs:19 'skip any data-syn' rule (policy by node kind)
- runtime/src/main/copy.mjs:29 empty in-range lines dropped before their separator
- runtime/src/main/copy.mjs:30-36 paragraph detection via .closest('.tsr-para') only (no unit boundaries inside a block, none in paged output)
- runtime/src/main/copy.mjs:52 native-copy fallback for synthetic-only selections
- engine/src/layout/layout.h:17 3-state join and its producers layout.cc:66-78, :544-550, :569, :587 (join derived from isLast||ragged)
- engine/src/render/typeset_html.cc:305-308 code-row next-line peek
- engine/src/render/typeset_html.cc:454 BF_REF→data-syn=ref as the only ref copy rule
- engine/src/render/typeset_html.cc:276, :287 dead data-syn='image' on elements that copy never visits
- runtime/src/main/shell.mjs:144-150 popup text via cloneNode().textContent

### RenderResult protocol + commit (live and static export)

**owner_layer**

L7→host boundary:
- engine/src/api: tsr_render_result and tsr_render_fragment, in T9's pipeline-shaped C ABI;
- runtime/src/main/commit.mjs;
- Node side: runtime/src/node/render.mjs and tools/export-static.mjs.
T9 owns worker serialization, the ABI handshake, typed operations and the product keys.

**purpose**

Replace the exact-bytes HTML string and its naming conventions with one generation-stamped, delta-framed per-block result. The same structure feeds three things: the live shell's single commit path (upgrade, relayout, update), the anchor and offset services, and the static-export bundle.

**definition**

```
// engine: tsr_render_result(doc, kind, heldKeys) → frame
//         tsr_render_fragment(doc, anchor, generation) → semantic HTML of the post-resolve subtree (ids suppressed, omitInFragment slots dropped)
type RenderResult = {
  head: { generation: number /*monotone per session*/, idPrefix: string, lang: string, heightPx: number,
          // measure contract, from the Config and vmets the engine measured with
          container: { style: { fontFamily, fontSize, lineHeight /*body content-height factor*/ },
                       vars: { '--tsr-cjk-font', '--tsr-lh-cjk', '--tsr-lh-code' }, attrs: { lang } } },
  blocks: { pid: number, key: string /*128-bit value hash; excludes pid, s0, gapAfter*/, src: [number, number], hPx: number, gapAfterPx: number,
            state: 'semantic' | 'estimated' | 'exact', off: number, len: number /*len 0 = the shell already holds this key*/ }[],
  html: string,   // ONE buffer holding only the blocks the shell lacks; transferable, no JSON escaping
  anchors: { id, label, cls, pid, preview }[],   // from the AnchorIds the writer emitted, in paint order (deterministic); cls from the T3 registry
  diags: Diag[], timings?: object };
// One ABI handshake at module load (T9) covers the ops, render and settings versions. RenderResult carries no version of its own.

// runtime/src/main/commit.mjs: the ONLY DOM mutation path
commit(session, next: RenderResult) → { ranges: { oldPids, newPids, rects?(): { old, new } }[], kept: number }
//  - holds element REFERENCES for kept blocks and checks they are still connected and in order; otherwise it replaces that range;
//  - matches an order-preserving prefix/suffix on key (duplicate keys allowed) and replaces only the middle;
//  - writes data-pid, data-s0 and margin-bottom positionally on kept and inserted blocks;
//  - per-block state lets semantic, estimated and exact blocks coexist (v2 §9 pending states);
//  - reads rects lazily, only when an onUpgrade listener exists, then acks the generation to the worker.
handle.offsetAt(node, offset) → byte | null
handle.elementsAt(byte) → Element[]

// Process-wide cache (engine/worker): block key → serialized block HTML + anchor rows.
// key = hash(T6 block layout hash, item text bytes, resolved style VALUES, anchor spellings, T8 MathBox structure hash,
//            presentation/settings generation, idPrefix), supplied as a T9 product key.
// A hit skips paint and serialization of that block.

// Worker rules (with T9):
//  - messages for one doc are serialized;
//  - render and fragment exports refuse unless the doc is DONE;
//  - the last committed generation's doc stays alive until the next commit ack, so stamped fragment, anchor and offset
//    requests are answered from it or get { stale: true };
//  - paginate runs on a separate layout product at page width (T6) and never re-measures the live doc in place.

// Node / static export
renderTsm(source, opts) → { result /*semantic*/, html, settings /*resolved JSON*/, resources /*T9 ResourceManifest*/, css: { contract, behaviors }, ok }
exportStatic(bundle, { template = defaultTemplate /*(parts) → html*/, hydrate = true, embedResources = true })   // hydration passes bundle.settings back verbatim
```

**surface**

JS:
- handle.update, relayout and typeset resolve with { ranges } on every path;
- handle.offsetAt and handle.elementsAt;
- renderTsm returns a bundle;
- exportStatic(bundle, { template }).

The legacy tsr_render and handle.html concatenate the same block bodies with pid, s0 and margin-bottom inlined, which keeps goldens byte-identical.

**replaces**

- runtime/src/main/shell.mjs:251-278 chunkParas (exact prefixes, attribute-order regex :264, literal tail :273)
- runtime/src/main/shell.mjs:279-309 patchIn and :312-328 swapIn (container.innerHTML)
- runtime/src/main/shell.mjs:398-405 empty upgrade records on the patch path
- runtime/src/main/shell.mjs:332-333, :346-356 container contract re-specified with drifting defaults (Georgia vs config.h:23 Crimson Text, zh-CN)
- runtime/src/worker/worker.mjs:139-151 single html string result; :228-246 paginate mutating the live doc; :269-280 concurrent dispatch (with T9)
- engine/src/render/typeset_html.cc:621-628 positional data-pid/data-s0/margin-bottom inside the paragraph bytes
- editors/vscode-tsm/media/preview.html:96-127 offset decoding that assumes monotone data-s0
- tools/export-static.mjs:36-76 fixed template, <html lang=zh-CN>, fonts, duplicated hydration options
- runtime/src/node/render.mjs:20-52 html-only return shape with no resources or settings

### Shell core + Behavior registry

**owner_layer**

Runtime host (main thread).
- runtime/src/main/shell.mjs is the core: sessions, transport, measure contract, commit and core copy.
- runtime/src/main/behaviors/{ref-preview,print,audit}.mjs are the behaviors.
- Main-thread resource providers plug into T9's protocol as capabilities.

**purpose**

Make the 'thin' shell actually thin and multi-document. The core keeps one session per container and owns the copy contract, which v2 §8 makes required. Every other DOM feature is a Behavior that consumes only declared data (RenderResult, the DOM contract, settings) and calls only typed engine operations. It never depends on resolver label spellings, style classes or layout shape. Hosts can drop, replace or add behaviors on equal footing with built-ins.

**definition**

```
createEngine({ copy = coreCopy /*replaceable, never absent inside commit roots*/, behaviors = defaultBehaviors(),
               capabilities /*T9 main-thread providers, e.g. imageDims*/ })
engine.typeset(source, container, opts) → session   // Map container → session { doc, generation, commit state, behaviors };
                                                    // one worker and one WASM instance serve many documents
type Behavior = { name: string, css?: string, install(ctx: BehaviorCtx): () => void };
type BehaviorCtx = {
  container: Element, root(): Element, overlay: Element /*inside the container, outside the commit root*/,
  settings: ResolvedRenderSettings /*render, copy, a11y; page.* read-only (T6)*/,
  onCommit(cb: (ranges) => void): () => void,
  anchors: { byId(id), byLabel(label) }, refAt(el): { id, label, cls, preview } | null,
  ops: { fragment(anchor), paginate(pageSpec), contentText(range), offsetAt(node, off), elementsAt(byte) }
  // only operations declared in T9's typed protocol; every call is stamped with the session's committed generation
};
defaultBehaviors = () => [refPreview(), print()];   // devAudit() is opt-in

refPreview() honours anchors[].preview (PresentationMap) by default. refPreview({ classes }) is a host override and wins; this precedence is documented.

Core lifecycle:
- CSS is injected once per behavior;
- install and handlers run in try/catch, so a failing behavior is disabled;
- behaviors are installed per session;
- shell-owned nodes are held by reference or marked with data-tsr-* attributes, never ids.

print():
- reads PageSpec (T6-owned page.* keys) and derives @page and the sheet size;
- calls ops.paginate on a separate layout product with a derived idPrefix;
- keeps the Gecko fractional-px clamp.

devAudit():
- includes the existing audits;
- adds metric-drift (computed metric properties vs the run's expected style);
- adds a baseline check (≤1px);
- takes its thresholds from a constants module shared with the tests.
```

**surface**

createEngine({ behaviors: [...defaultBehaviors(), refPreview({ classes: ['citation', 'term'] })] }), or a custom { name, css, install(ctx) }. Behaviors reach the overlay, anchors, fragments and content text only through ctx.

**replaces**

- runtime/src/main/shell.mjs:123-197 installNotePopups (selectors :146, :175; one-.tsr-para sibling walk :133-143; append to .tsr-doc :162-165, the patch root)
- runtime/src/main/shell.mjs:199-209 ensureCss injecting every feature's CSS (TSR_CSS :16-85, link colour literal :39)
- runtime/src/main/shell.mjs:218-229 and runtime/src/worker/worker.mjs:66-76 bespoke 'image-dims?' RPC
- runtime/src/main/shell.mjs:340-343, :375-378 one liveDocId and one uninstallCopy per engine (zball-io/eleventy.config.js:83-84 creates one engine per language as a workaround)
- runtime/src/main/shell.mjs:353 popup-only --tsr-pop-font set by core typeset()
- runtime/src/main/shell.mjs:421-478 paginate/print with 666×995 defaults and A4 constants (:423, :448-452)
- runtime/src/main/audit.mjs:40, :52, :91, :103 literal thresholds and predicates keyed on data-join/data-cell

## Subsumption (finding → mechanism)

- **subsumed** by *DisplayList (paint annotation over T6 layout records)*: `render-runtime/linebox-special-dispatch`, `render-runtime/render-layout-decisions`, `render-runtime/audit-hint-attributes`, `render-runtime/marker-gutter`, `render-runtime/paged-gutter-clipping`, `render-runtime/trailing-float-and-gap-drift`, `render-runtime/error-render-divergence`
  How each item becomes record data:
  - The special codes and the if-ladder become LaidOutBox kinds plus typed payloads (S3).
  - Config reads and the duplicated CJK predicate become T6 GapReal values that paint copies (interim S3: one shared predicate).
  - Audit hints become Just and Track. They are serialized as data-ragged on every non-justified line carrying a break-type join, and data-track replaces the three-meaning data-cell (S8).
  - Markers and code line numbers become Placement{edge=End}: right-anchored, no width needed, so the default serializes byte-identically (S10).
  - T6 supplies the gutter extent (code: digits×ch from the grid atom; lists: inside the item indent), so sheets clip including it.
  - DLBlock.y is the single vertical authority (su-rounded gaps), and DisplayList height includes a trailing float (layout.cc:609).
  - Error nodes become syn=error runs with class tsr-err and a title (document-model §9.1).

  Split with T6 on marker-gutter: T6 computes the gutter extent; T7 places and paints. Fixing steps: S3, S8, S10.
- **subsumed** by *DisplayList (paint annotation over T6 layout records)*: `render-runtime/missed:1`, `break-layout-pages/missed:0`
  Anchors live on T6's line and box records: the first anchor-eligible line of a registry-winning unit (text, code, display math, image), exactly today's screen rule. Both backends print them statelessly. A unit continued on a later sheet does not repeat its id, because the id belongs to one line node. lastAnchored is deleted.

  Split with T6: T6 marks eligibility; T7 owns the stateless backends. Fixed in S3, which adds a new paged fixture reproducing both failures.
- **subsumed** by *ContentText projection + CopyPolicy*: `render-runtime/copy-syn-policy`, `render-runtime/copy-line-separators`, `render-runtime/missed:5`, `break-layout-pages/missed:2`, `render-runtime/copy-drops-blank-code-lines`
  Policy is keyed by syn kind and class, never by the producing code path. Ownership is 'range intersects a .tsr-line', which fixes the synthetic-only native fallback (missed:5) without breaking overlay copy.

  Empty lines contribute their separator; all-omitted lines contribute nothing. Sep is a property of the break or unit end, never of alignment (layout.cc:569). Tables get tab/row, sidecars custom with the declared marker, and intra-block unit ends para/newline per class sepAfter. Paged output gains .tsr-band block identity.

  Split with T6 on break-layout-pages/missed:2: T6 produces Sep (materializeLines); T7 serializes it and owns copy. Defaults keep today's e2e expectations (test/e2e/typeset.spec.mjs:137-150). Fixed in S8.
- **subsumed** by *Shell core + Behavior registry*: `render-runtime/shell-note-popups`, `render-runtime/shell-feature-inventory`, `render-runtime/popup-breaks-patch`, `resolver/missed:2`, `real-world-evidence/notes-popups-dom-scraping`
  Footnote popups become the generic refPreview():
  - keyed on RenderResult.anchors (class plus the preview policy), not the 'fn-' prefix or .tsr-sup;
  - content comes from ops.fragment(anchor), stamped with a generation: post-resolve semantic HTML with refs resolved, ids suppressed and the backlink slot omitted;
  - shown in an overlay outside the commit root, so patching continues while a popup is open.

  Copy is core. Print and audit are behaviors. image-dims becomes a T9 capability.

  Splits:
  - with T3: it registers every anchored node (including bib entries and terms) with its class and tags the backlink slot;
  - with T9: typed operations and capability providers.

  Fixed in S5.
- **subsumed** by *RenderResult protocol + commit (live and static export)*: `render-runtime/shell-chunk-byte-coupling`, `render-runtime/swap-whole-container`, `render-runtime/anchor-decode-duplication`, `render-runtime/static-export-template`, `render-runtime/missed:4`
  Structured, delta-framed blocks:
  - keys exclude pid, s0 and gap;
  - the 128-bit key plus element references guard against stale reuse;
  - one commit path returns per-range records on upgrade, relayout and update, with rects read lazily (v2 §9);
  - offsetAt and elementsAt replace preview.html decoding. Code rows gain data-s in S8; run precision depends on T5 sub-spans.

  The Node bundle drives a user template, hydration from the resolved settings JSON, and resource copy or embedding from T9's manifest. Embedding bib entries avoids the refs.json copy-path failure (executor.mjs:246-250).

  Split with T9: T9 owns worker serialization, the ABI handshake, product keys and the resource manifest; T7 owns framing, commit and the bundle shape. Fixed in S4 and S14.
- **subsumed** by *HtmlWriter + AnchorNamer*: `render-runtime/anchor-namespace`, `resolver/anchor-namespace`, `render-runtime/duplicate-serializer-primitives`, `render-runtime/snap-kerning-duplicate-style`, `api-measure-code/snap-kerning-duplicate-style-attr`, `render-runtime/hyphen-in-link-or-ref`, `render-runtime/semantic-footnote-ids-dangle`
  One writer assembles every attribute, escapes text, and writes ids and hrefs through AnchorNamer (prefix plus HTML-attribute escaping, byte-identical). The hyphen run inherits its run's link. The resolver stores target AnchorIds, not '#tsr-' URLs. idPrefix is a per-document setting, and the shell's own nodes leave the id namespace.

  Splits:
  - anchor namespace with T3: T3 owns the label half (reserved h-/fn-/fnref-/bib-/eq- prefixes; whitespace rejected; winners only; a losing duplicate gets its auto anchor or, for kinds without one, no DOM id plus a diagnostic);
  - duplicate-serializer-primitives with T4: escaping, ids and attribute assembly are T7's; typed style→CSS is T4's;
  - semantic-footnote-ids-dangle with T3: the list projection hoists the elided paragraph's anchors onto the <li> (T7); T3 does not move labels (resolver/semantic-tight-item-anchor defers here).

  Fixed in S2, S7 and S9.
- **subsumed** by *PresentationMap (class → per-backend presentation, declared through one channel)*: `render-runtime/semantic-role-switch`, `render-runtime/typeset-role-blind`, `render-runtime/sidecar-dropped-in-semantic`, `api-measure-code/missed:2`, `real-world-evidence/missed:5`, `render-runtime/typeset-a11y`
  Built-in kinds and user classes are rows of one table, with slots, attributes from arguments, wrappers, ARIA and four selectable projections. How each item is handled:
  - figure: maps its caption slot to figcaption. Until T3 slots land, each caption paragraph is closed inside the figcaption and non-caption children go outside.
  - codeblock: projects its sidecar slot inline with the declared marker.
  - Typeset DOM: hooks are data-role attributes plus T6 frame boxes carrying classes, never classes on ancestors of runs (I2).
  - a11y: semantic elements and ARIA come from the same entry; the opt-in sr-only text layer uses ContentText.

  Splits:
  - with T3: category, like and slots; real-world-evidence/missed:5 needs T3 caption slots;
  - with T6: frame geometry;
  - with T2: the sidecar becomes a first-class slot instead of the 'sidecar-lines' role (api-measure-code/missed:2).

  Fixed in S12 (a11y layer in S13).
- **subsumed** by *PresentationMap (class → per-backend presentation, declared through one channel)*: `real-world-evidence/missed:4`, `math/fallback-and-a11y`
  The math projection defaults to 'boxes' on the page profile. The semantic, fragment and static outputs emit the same Glyphs leaves as the typeset path, through the shared writer, with role=math and aria-label from the source. This honours v2 §9 ('Math is exact from the start … formulas never render natively'). In pre-measure contexts (first paint, Node), text-font leaves use T8's estimate-tolerant metrics; the upgrade replaces the block.

  The feed profile defaults to 'source', because feed readers drop positioning CSS. MathML is an opt-in feed format only (v2 §13, document-model §9.4).

  Split with T8: it exports flattened leaves and estimated text-leaf metrics. Fixed in S13; the 7 math-bearing semantic goldens are re-recorded.
- **owned-by-other-theme** by *T6-layout-pagination*: `render-runtime/paged-keep-rules-by-kind`, `break-layout-pages/paginator-in-serializer`, `render-runtime/sidecar-hyphen-missing`
  T6 owns all of the following:
  - the page builder over the VList (keeps, atomic groups, widows, inserts, marks);
  - PageSpec (page.* keys);
  - PageResult indexed by LAYOUT line and box ids, with per-band frame fragments (openEdges);
  - the single materializeLines that sets endsWithHyphen, span and Sep for every stream (layout.cc:344-357 sets none of them today).

  T7 consumes PageResult in a stateless paged backend that wraps each band in .tsr-band[data-b] carrying block hooks. The interim relocation of the band cutter (typeset_html.cc:642-765 → layout/paginate.cc, verbatim, over LayoutResult) is T6 code moved in S3. Page-number refs must not use a resolve↔paginate fixpoint (v2 §11.1).
- **owned-by-other-theme** by *T4-style-settings*: `render-runtime/css-contract-monolith`, `render-runtime/no-class-channel`, `render-runtime/token-theme-sniffing`, `render-runtime/config-plumbing`, `render-runtime/missed:2`
  T4 provides:
  - typed style values validated ONCE at ingest and read by both the measurement-request generator and the writer (missed:2). Backslash CSS escapes in families are valid syntax. There is no writer-only sanitizer, because it would split measurement from paint;
  - dynClasses, with tokens as classes;
  - a contract.css generated from the measurement table, including the metric locks on .tsr-line and the per-font line-height factor rules;
  - the settings registry with per-key precedence, plus the JSON ABI (with T9).

  T7's share:
  - HtmlWriter writes only typed declarations and validated class tokens;
  - metric declarations stay inline;
  - behavior CSS ships with behaviors;
  - the shell pins the container from head.container;
  - T7 registers the render.*, copy.* and a11y.* keys and the render.classes schema.
- **bug-fix-only** by *DisplayList run formation (interim run key, S6)*: `render-runtime/missed:0`, `emitter/missed:0`
  S6 adds the BF_REF/synthetic flag to both coalescing predicates (typeset_html.cc:550-551, :585-589), so ref runs never absorb prose or brackets. This re-records the cite goldens that currently lock the bug in. T5's runId (which keys on syn kind) later replaces the predicate without a golden change. T5 lists emitter/missed:0 as well; the fix lands here once.
- **owned-by-other-theme** by *T5-text-shaping*: `render-runtime/missed:3`
  Exact per-item source sub-spans are T5's. T7 writes data-s from DLRun.src and emits none when the span is empty (not 1:1).
- **owned-by-other-theme** by *T3-semantics*: `resolver/missed:1`
  T3 makes the equation number tree content in slot 'tag', instead of an ArgK::name string that only emit reads (resolve.cc:193, emit.cc:756). T7 maps the tag slot through PresentationMap: <span class=tsr-eqno> in semantic HTML, and the placed eqno run in the DisplayList.
- **bug-fix-only** by *baseline authority (S11): T4 contract rules + T6 LaidOutLine.baseline*: `render-runtime/host-line-height-leak`
  Every inline box gets an explicit line-height equal to its measured content height (ascent+descent). That is how mathLeaves already pins glyph baselines (typeset_html.cc:93-117). It removes host leakage, hhea lineGap and fallback-font metrics from the baseline.

  Mechanism:
  - unitless per-font factors (vmet/size) travel in head.container as the body line-height plus --tsr-lh-cjk and --tsr-lh-code;
  - T4's generated contract applies them to .tsr-doc, .tsr-cjk and .tsr-code;
  - only runs with a user font family carry an inline value;
  - code rows keep their centring line-height (class tsr-row);
  - T6 computes LaidOutLine.baseline with the CSS inline formula, strut included;
  - the devAudit baseline check enforces ≤1px.
- **bug-fix-only** by *doc updates per step + contract tests*: `render-runtime/normative-doc-drift`
  Docs are updated in the step that changes the behaviour:
  - document-model §9.1 and §9.4 in S3;
  - architecture §4.2 and §4.3 in S4 and S5;
  - document-model §9.3 and pages-design §2/§5 in S8, which also closes the drift item;
  - document-model §9.2 in S12.
  The S1 runner invariants, plus the copy simulator checked against the --stage=text dump, keep docs and code aligned.

## User extension examples

### A theorem environment that is themeable, has a real HTML element, and previews on hover

**Today**

#!theorem(label: 'thm-ua') … #theorem! becomes group{role:'theorem'} (executor.mjs:130). Across the outputs:
- Semantic output is <div data-role=theorem> (semantic_html.cc:307-317).
- The typeset DOM is anonymous lines (emit.cc:816-819 flattens the group): no hook, no rectangle.
- @thm-ua renders '??', because only figure groups register labels (resolve.cc:165-170).
- Hover previews exist only for footnotes (shell.mjs:175).

**After**

#{ $.element('theorem', { like: 'aside', counter: 'thm', supplement: 'Theorem', render: { semantic: { element: 'section', aria: 'note' }, typeset: { frame: true }, ref: { preview: 'block', copy: 'text' } } }) }

T3 supplies numbering and refs. The render record lowers to render.classes.theorem.

Semantic output:
<section class=tsr-role-theorem data-role=theorem role=note id=tsr-thm-ua>

Typeset output:
- lines carry data-role=theorem;
- T6's BoxModel yields a frame, which paint emits as <div class='tsr-box tsr-role-theorem' style='top;left;width;height;border-width'> behind the lines, split per band when printed.

Theming and behaviour:
- .tsr-role-theorem { background: var(--thm-bg) } hits the section and the frame on both pages;
- an italic body is a style rule (T4), so measurement sees it, not page CSS;
- refPreview shows the theorem on hover of @thm-ua with no host configuration;
- copying the ref yields 'Theorem 2.1'.

Dependencies: T3 element classes, T6 frames, T4 settings. A label containing ':' also needs T1's universal label lexing; today `@thm:ua` lexes as ref 'thm' followed by ':ua'.

### A user class with its own semantic shape (details/summary, keyboard keys)

**Today**

Every role becomes <div data-role=…> (semantic_html.cc:307-317). Only figure has a branch, and it never closes its figcaption (:284-306). document-model §9.2's own term→dl>dt+dd and collect→nav|section mappings are unimplemented, because shapes are C++ branches per Kind.

**After**

Settings: { render: { classes: { spoiler: { semantic: { element: 'details', slots: { title: { element: 'summary' } } } }, kbd: { semantic: { element: 'kbd', inline: true } } } } }.

The spoiler renders <details class=tsr-role-spoiler><summary>…</summary>…</details>, and kbd renders inline <kbd>. Built-in term and collect are rows in the same presentation.def, so the §9.2 mappings ship as data.

An element:'style' or 'script' entry is rejected with a diagnostic and falls back to div[data-role].

### Citation and glossary previews (a new shell behaviour)

**Today**

This requires editing createEngine. installNotePopups is hard-wired (shell.mjs:126-197, :377) and recognises only a.tsr-sup[href^='#tsr-fn-']. It scrapes sibling .tsr-line text (:137-150), so hyphen glyphs, inter-CJK spaces and Euler glyph text leak into the popup.

**After**

Default: once T3 registers bib entries and terms with their classes, render.classes.citation.ref.preview = 'block' is enough. The default refPreview() honours anchors[].preview.

Host override: createEngine({ behaviors: [...defaultBehaviors(), refPreview({ classes: ['citation', 'term'] })] }).

Custom: { name: 'cite-card', css, install(ctx) { … const r = ctx.refAt(e.target); const { html, stale } = await ctx.ops.fragment(r.id); if (!stale) ctx.overlay.append(card(html)) … } }.

The fragment is post-resolve semantic HTML, with math as boxes and no duplicate ids. It is answered from the committed generation, so a hover during an edit never shows the wrong entry. Patching continues while the card is open.

### Two engine documents on one page (bilingual post, side-by-side preview, print root)

**Today**

Every instance writes the same tsr- ids. zball-io/_site/p/hello-typesetter/index.html contains id=tsr-intro twice, so @intro in the English article targets the hidden Chinese heading. One engine handles one live document (shell.mjs:340-343), so the blog creates one engine, worker and WASM instance per language (eleventy.config.js:83-84). print() looks up ids that a user label can produce: a heading <print-root> renders id=tsr-print-root and is deleted (shell.mjs:436).

**After**

const eng = createEngine(); await eng.typeset(zh, zhEl, { settings: { render: { idPrefix: 'tsr-zh-' } } }); await eng.typeset(en, enEl, { settings: { render: { idPrefix: 'tsr-en-' } } }).

This gives two sessions on one worker. AnchorNamer prints ids and hrefs from labels, and print uses a derived prefix. The shell's own nodes are held by reference, so no label can collide with them. Static export passes the same settings, which keeps deep links stable.

### Copy policy for a user construct that fabricates text

**Today**

A user #!exercise handler that prepends 'Exercise 3:' is copied as content, and so is the resolver's 图 1： prefix (resolve.cc:171-180). Meanwhile a prose ref in '见 @s 一节' loses '§1' (BF_REF → data-syn=ref, typeset_html.cc:454; copy.mjs:19). Policy is an accident of node kind.

**After**

The handler wraps its label as $.synthetic(label, { kind: 'exercise-label', copy: 'omit' }). That is T2's typed content attribute, an OPS_VERSION bump counted by T2. Alternatively, a T3 template generates the label as syn=prefix.

The author can also set render.classes.exercise.generatedCopy = 'omit', or set copy.policy = { ref: 'text' } for prose refs. The DOM carries each decision as data-syn or data-copy to the core copy handler.

### A site template for static export

**Today**

tools/export-static.mjs:55-76 hard-codes the page, <html lang=zh-CN>, fonts, and a hydration script that duplicates shell options (:42-53). refs.json is never copied, so hydration replaces a good static bibliography with an error (executor.mjs:246-250). Math prints as $…$ source. The blog regex-parses front matter for lang (zball-io/eleventy.config.js:29-32) and re-implements engine CSS.

**After**

const bundle = await renderTsm(src, { baseDir }); await exportStatic(bundle, { template: (p) => myLayout(p) }).

The bundle supplies:
- lang and title from document settings;
- the resolved settings JSON, which hydration passes back verbatim;
- the T9 resource manifest, copied or embedded;
- contract and behavior CSS;
- math as positioned boxes with aria-label.

A feed template uses profile 'feed' (math as source).

### A new inline object, e.g. an inline SVG icon from a user fence handler

**Today**

Math is the only inline box (LinebreakBlock::math, typeset_html.cc:474-479). Raw is block-only, with a declared height (special 3, :183-196). A user cannot place a sized inline object.

**After**

T5's InlineObject protocol gives the icon width, ascent and descent. Paint lowers it to DLInlineBox{ RawHtml }, carrying the run's StyleRef and link.

- The typeset backend writes <span class=tsr-raw-inline style='width;height;vertical-align'> around the trusted HTML.
- The semantic backend writes the HTML inline.
- ContentText uses its declared copy text (alt).

No backend branch is added.

### Printing on US Letter with numbered code listings and a framed theorem across a page cut

**Today**

@page size A4 and 666×995 are literals (shell.mjs:423, :448-452). Code line numbers are placed by CSS right:100% (shell.mjs:30) and clipped by .tsr-sheet overflow:hidden (typeset_html.cc:749). Paginate re-measures the live document in place (worker.mjs:228-246).

**After**

Set #set({ page: { size: 'letter', marginPx: 72 } }) or the equivalent host setting (PageSpec, owned by T6).

- The print behavior derives @page and the sheet size, and calls ops.paginate, which runs on a separate layout product.
- Line numbers stay right-anchored. Sheets clip at the measure extended by T6's gutter extent.
- The theorem frame arrives as two band fragments with open edges, painted as slices.
- The live document is never touched.

## Invariants

- **I1 su fixed-point determinism; byte-exact goldens**
  Determinism of the outputs themselves:
  - LaidOut*/DL geometry is su, and printed px values are doubles (no float narrowing).
  - Paint is pure, with no hash-map iteration in output order.
  - anchors[] are emitted in paint order, and the label registry is document-ordered (T3).
  - HtmlWriter keeps fmtPx.

  Byte-identical steps (html, paged and semantic goldens): S1, S2, S3, S4, S5, S9 (html/semantic), the neutral port in S12, and S14.

  Golden churn is confined to named steps, each with a scripted 'only these attributes changed' check:
  - S6: cite/basic and cite/unknown-diag html.
  - S7: notes/basic, notes/cjk-glue and notes/explicit semantic.
  - S8: html, layout and paged for about 20 fixtures with cells, code rows, multi-unit blocks or captions (listed in S8).
  - S9: *.tree.txt of ref fixtures.
  - S10: 33 html goldens with margin-bottom 19.2px→19.203px (1229su, verified: 94 occurrences, 15 single-block goldens have none), plus figure/float min-height, error fixtures and the paged golden clip.
  - S11: code fixtures (tsr-row) and style fixtures with user families.
  - S12: semantic figure, region, code/sidecar and style.
  - S13: 7 math-bearing semantic goldens.

  New curated *.text.txt goldens are additions. --stage=dl is debug-only.
- **I2 measurement–render robustness contract (v2 §7): nowrap lines, ε over-estimate, explicit spacing, no runtime repair**
  Line geometry:
  - Each line node is exactly one nowrap, absolutely positioned element.
  - Spacing comes only from T6-realised fields (GapReal → word-spacing, letter-spacing, margins, spacers).
  - Baselines are explicit: per-font content-height line-heights (S11), with T6 computing the baseline by the CSS formula.

  Style:
  - Style values are validated once at ingest and read by both measurement and paint (T4), so no value can be measured in one font and painted in another.
  - Role classes never sit on ancestors of measured runs. Theming hooks are data-role attributes and frame boxes.
  - T4's generated contract locks inherited metric properties on .tsr-line with !important.
  - devAudit flags computed metric drift.

  Paint-only content:
  - Frames are paint-only boxes with explicit inline geometry, painted behind lines.
  - Behaviors render only into the overlay.
  - The element allowlist keeps raw-text elements (style, script) out of the typeset DOM.

  commit swaps whole blocks atomically, and the audit stays read-only.
- **I3 ops contract and OPS_VERSION discipline**
  T7 adds no opcode, kind or arg key.
  - Presentation travels as a JSON value in T4's settings channel. T7's schema is validated, versioned and fuzzed by the settings parser, so presentation changes never touch OPS_VERSION.
  - Tagging user-handler text as synthetic needs T2's typed `syn`/`copy` content attributes. That is an OPS_VERSION bump owned and counted by T2; T7 only consumes it.
  - Internal refs stop carrying ArgK::url after resolution. This is engine-internal and visible only in tree dumps.
  - JS/WASM skew of the render protocol is caught by T9's single ABI handshake at module load, alongside OPS_VERSION and the settings version.
- **I4 execution declares, resolver decides**
  Presentation is declared data (settings), and paint and backends never feed back into execution or resolution. Fragments render the post-resolve tree inside the engine. Scripts cannot observe ids, the DisplayList or behaviors.
- **I5 dual-target rule**
  Engine-side and browser-free (engine/src/paint, engine/src/render): DisplayList, paint, PresentationMap, HtmlWriter, ContentText and fragments. Native goldens cover html, paged, semantic and curated text. DOM-only parts live in runtime/src/main: commit, sessions, core copy and behaviors. The JS copy rules are checked in e2e against the native --stage=text projection.
- **I6 emission-time style binding with the DAG/schedule encoding**
  Untouched. Paint reads per-leaf resolved styles from instantiation. PresentationMap adds element shapes and hooks only; it never re-styles or touches the style stack. Metric styling of a class goes through T4 rules on the style stack.
- **I7 block-granular containment of errors**
  - Error nodes become syn=error runs or lines inside their own block (class tsr-err with a title, copy omitted by default), as document-model §9.1 requires.
  - HtmlWriter is first-wins with a diagnostic in release; it never aborts the WASM render.
  - An invalid presentation entry raises a diagnostic and falls back to div[data-role].
  - A throwing behavior is disabled.
  - A commit whose kept references fail verification replaces that range only.
- **I8 resumable pull-loop state machine; atomic per-paragraph swaps**
  - Render and fragment exports run only when the doc is DONE, and per-doc messages are serialized (T9). Paginate uses a separate layout product, so no request sees a half-measured doc.
  - Blocks are the swap unit and carry per-block state (semantic, estimated, exact), as v2 §9's pending states require.
  - commit returns per-range { oldPids, newPids, rects } on every path, with offsetAt/elementsAt as the source↔line map.
  - Generations make async queries consistent with the DOM they were asked about.
- **I9 performance (editor fast path ~6 ms compile+execute+ingest at 87K)**
  No claim rests on caching DLBlocks; they die with the Doc.
  - Paint is O(lines + runs) and runs after the hot path.
  - A process-wide cache keyed by a value hash (a T9 product key) maps blocks to serialized HTML, so unchanged blocks skip paint and serialization.
  - The transport is a delta: one transferable HTML buffer holding only blocks the shell lacks, plus a small table. That means no JSON escaping and no per-edit re-chunking.
  - Rects are read lazily, and behaviors install once per session.

  Gate: bench-edit at 7.8K, 35K and 87K before and after S4. The budget for render, transport and commit is ≤ today's tsr_render, postMessage and patchIn. editor-design §4 lists a ~7 ms render string at 87K.

## Interfaces

- **T6-layout-pagination** (consumes)
  T6 owns one record per materialized line and box (LaidOutBlock/LaidOutLine/LaidOutBox), in DOM order. Each record carries:
  - geometry in su, including the baseline (CSS inline formula with strut);
  - Just (from LineEnds) and Track{kind, index};
  - Sep and sepText (from the break item, cell or row end, or unit end with the class's sepAfter);
  - the anchor on the first anchor-eligible line of a registry winner;
  - class path and slot, LineMarks, and GapReal (realised width and carrier per gap);
  - gutter Placement plus the gutter extent;
  - frames with BoxModel insets and border widths.

  T6 also provides:
  - tag placement for display math (measured, with a collision policy); T7's edge=End is the interim encoding;
  - PageResult over layout ids, with per-band frame fragments (openEdges), inserts, marks and furniture blocks;
  - PageSpec (page.*);
  - a separate page-width layout product for paginate;
  - a per-block layout hash for the HTML cache key.
- **T6-layout-pagination** (provides)
  - The paint contract for each record kind.
  - The anchor-eligibility rule (text, code, display math, image; rules and raw only after S9b).
  - The copy Sep vocabulary and its DOM encoding.
  - Frame painting with open edges as decoration-break slices.
  - The rule that pagination properties never enter the DisplayList.
  - The S3 adapter (layout/laid_out.cc) that seeds T6's record from today's LineBox, which T6 then owns and replaces internally.
- **T5-text-shaping** (consumes)
  - runId per item, keyed by style, link, syn kind, anchor and script. It is the only run boundary; S6 is the interim.
  - Exact per-item source sub-spans, empty when the item is not a 1:1 slice.
  - Gap classes, which T5 defines and T6 realises (T7 only copies the values).
  - InlineObject kinds that lower to DLInlineBox payloads (Glyphs, ImageRef, RawHtml), with style and link.
- **T3-semantics** (consumes)
  Classes:
  - element classes with category, `like:` base and slots on children (caption, title, body, tag, term, def, backlink);
  - no presentation fields in the declaration op.

  Label registry (persistent, document-ordered):
  - registry winners only;
  - reserved auto-anchor prefixes (h-/fn-/fnref-/bib-/eq-);
  - whitespace rejected at registration;
  - a losing duplicate gets its auto anchor, or no DOM id plus a diagnostic.

  Registration and generated text:
  - every anchored node, including bib entries and terms, is registered with its class and pid;
  - refs carry the target AnchorId and target class, not a URL;
  - generated text is tagged syn=prefix or ref with its class;
  - the equation number is content in slot 'tag'.
- **T3-semantics** (provides)
  - renderSemanticFragment(node) and tsr_render_fragment(anchor, generation), so any labelled node can be previewed.
  - The list projection's anchor hoisting: T3 never moves labels, which resolves resolver/semantic-tight-item-anchor.
  - The PresentationMap shape, which a `like:` chain inherits across built-in and user classes.
- **T4-style-settings** (consumes)
  Style values:
  - ResolvedStyle with typed values validated ONCE at ingest; measurement requests and HtmlWriter read the same value;
  - class tokens (dynClasses).

  Generated contract.css:
  - metric locks on .tsr-line (!important);
  - per-font line-height factor rules (.tsr-doc, .tsr-cjk, .tsr-code);
  - squeeze classes;
  - .tsr-box paint-only rules.

  Settings:
  - the registry with per-key precedence;
  - T7 registers render.idPrefix, render.classes (schema), render.math, copy.policy, a11y.textLayer and a11y.mathLabel;
  - theme tokens for paint values (link colour, frame colours).
- **T2-constructor-ir** (consumes)
  - Typed content attributes `syn` (an open kind name) and `copy` (text|omit|replace(s)) on text and styled content, exposed as $.synthetic(content, { kind, copy }). This is an OPS_VERSION bump owned by T2.
  - The sidecar as a first-class slot of codeblock, replacing the 'sidecar-lines' role string.
  - `$.element(…, { render })` sugar lowering to a document-scope settings write.
- **T8-math** (consumes)
  - Flattened, typed leaves (GlyphLeaf{x, baseline, px, lineHeightPx, text, fontRole}, RuleLeaf), so no backend includes mathfont.h.
  - Estimate-tolerant text-leaf metrics for pre-measure contexts (semantic first paint, Node export).
  - The formula source for CopySpec and aria-label.
  - A MathBox structure hash for the cache key.
  - Optionally, a MathTree walk for the opt-in MathML feed projection.
- **T9-host-protocol** (consumes)
  Worker and protocol:
  - one ABI handshake at module load (ops, render and settings versions);
  - per-doc message serialization and DONE-state guards;
  - generation ids, with the committed generation's doc retained until the next commit ack;
  - a typed operation registry (fragment, paginate, contentText, offsetAt, elementsAt) backing BehaviorCtx.ops;
  - main-thread capability providers (imageDims, later needMainMeasure).

  Data:
  - the settings JSON ABI, used identically by shell, worker, renderTsm and hydration;
  - the ResourceManifest for static export;
  - value-level product keys used for the process-wide block-HTML cache.
- **T9-host-protocol** (provides)
  - The RenderResult frame: the block table (pid, key, src, hPx, gapAfterPx, state, off, len), one HTML buffer, the anchors table and head.container.
  - The tsr_render_result(doc, kind, heldKeys) and tsr_render_fragment(doc, anchor, generation) exports.
  - The commit ack that lets the worker release the previous generation's doc.

## Migration

### S1 Contract tests  → plan P0-01

The golden runner checks every output:
- every internal href=#x has exactly one id=x;
- no element repeats an attribute;
- ids are unique;
- only allowlisted elements appear outside raw;
- every non-synthetic text line has data-s/e.

Known failures are recorded as xfail: notes/*.semantic (dangling footnote ids), code rows and captions (no spans), and a new duplicate-label mathblock fixture.

**Golden impact:** None. These are new checks plus one new fixture; the xfail list shrinks as later steps land.

**OPS bump (as designed):** False

### S2 HtmlWriter + AnchorNamer (prefix 'tsr-')  → plan P0-10

- Port both serializers onto the shared writer: one style attribute per element, buffered allowlisted attributes, one escaper, ids through AnchorNamer.
- id() = prefix + HTML-attribute escaping, byte-identical to today.
- Repeated attributes are first-wins plus a diagnostic in release, and assert in debug and native tests.
- The hyphen run opens inside its run's link.
- No style sanitizer in the writer: value validation is T4's ingest step.

**Golden impact:** None expected on 48 html, 48 semantic and 1 paged golden. No fixture exercises snap-kerning, a hyphen inside a link, or ';' in a style value; any diff is a defect. New fixtures for snap-kerning and hyphen-in-link are added.

**OPS bump (as designed):** False

**Fixes:** `render-runtime/snap-kerning-duplicate-style`, `api-measure-code/snap-kerning-duplicate-style-attr`, `render-runtime/hyphen-in-link-or-ref`, `render-runtime/duplicate-serializer-primitives`

### S3 Layout record seam + DisplayList + stateless anchors (joint with T6)  → plan P1-18

- layout/laid_out.cc builds LaidOutBlock from today's LineBox and FlowUnit: nodes in LineBox order, Just, Track, anchor eligibility exactly as today, and GapReal from the one shared CJK predicate. This is T6-owned code from then on.
- paint/ builds DLBlocks with typed payloads. The typeset backend becomes a DL walker, and Config and model reads disappear.
- T6's verbatim relocation moves the band cutter to layout/paginate.cc over LayoutResult, indexed by layout ids. The paged backend consumes PageResult statelessly; lastAnchored is deleted.
- Add the debug stage `tsrc --stage=dl`.
- Update document-model §9.1 and §9.4.

**Golden impact:** *.html.txt and pages/paged-doc.paged.txt stay byte-identical: rules stay interleaved with cells, and the id stays on the first cell line. *.layout.txt is unchanged, because dumpLayout stays until T6 replaces it. A new paged fixture (heading + labelled display formulas at 240px) records ids that today's code drops.

**OPS bump (as designed):** False

**Fixes:** `render-runtime/linebox-special-dispatch`, `render-runtime/render-layout-decisions`, `render-runtime/missed:1`, `break-layout-pages/missed:0`

### S4 RenderResult + commit (requires T9 per-doc serialization and ABI handshake)  → plan P3-05

- tsr_render_result emits the block table plus one HTML buffer for unheld keys. Keys are 128-bit value hashes; pid, s0 and gap are positional; blocks carry per-block state; anchors table; head.container; generation.
- A process-wide block-HTML cache sits on T9 product keys.
- commit() replaces chunkParas, patchIn and swapIn. It keeps element references and returns per-range records with lazy rects.
- Add handle.offsetAt and handle.elementsAt; the VS Code preview switches to them.
- tsr_render and handle.html remain as concatenations.
- Update architecture §4.2 and §4.3.

**Golden impact:** None. e2e adds paragraph insert/delete patching, upgrade records on update, and a stale-key guard. bench-edit at 7.8K/35K/87K is a merge gate.

**OPS bump (as designed):** False

**Fixes:** `render-runtime/shell-chunk-byte-coupling`, `render-runtime/missed:4`, `render-runtime/swap-whole-container`, `render-runtime/anchor-decode-duplication`

### S5 Shell core, sessions and Behavior registry  → plan P3-06

- Core: Map container→session, core copy, overlay outside the commit root, shell nodes held by reference.
- Behaviors: refPreview (anchors table + generation-stamped fragments), print (T6 PageSpec, ops.paginate on a separate layout product, derived idPrefix) and devAudit (shared constants).
- image-dims becomes a T9 capability.
- Split CSS into contract and behavior CSS.
- Update architecture §4.2.

**Golden impact:** None in engine goldens. e2e adds popup tests: content, patching while a popup is open, CJK and hyphen text, hover during an in-flight update. It also adds a two-documents-one-engine test and a <print-root> label test.

**OPS bump (as designed):** False

**Fixes:** `render-runtime/shell-note-popups`, `real-world-evidence/notes-popups-dom-scraping`, `resolver/missed:2`, `render-runtime/shell-feature-inventory`, `render-runtime/popup-breaks-patch`

### S6 Interim run key  → plan P0-10

Paint's run boundary includes the synthetic/ref flag (BF_REF), so ref runs no longer absorb adjacent prose or brackets. T5's runId replaces the predicate when it lands.

**Golden impact:** Re-record cite/basic.html.txt and cite/unknown-diag.html.txt (4+1 locked-in instances), plus any fixture with '??' next to prose.

**OPS bump (as designed):** False

**Fixes:** `render-runtime/missed:0`, `emitter/missed:0`

### S7 List projection hoists anchors  → plan P0-10

The semantic list projection moves an inlined tight-item paragraph's anchors onto its <li> explicitly. The writer holds no pending state.

**Golden impact:** notes/basic, notes/cjk-glue and notes/explicit .semantic.txt gain <li id=tsr-fn-n>. The anchor-closure xfail is removed.

**OPS bump (as designed):** False

**Fixes:** `render-runtime/semantic-footnote-ids-dangle`

### S8 Separators, copy contract, audit hints (layout side owned by T6)  → plan P3-07

T6 side:
- one Sep producer for every stream (materializeLines, or its interim helper in layout.cc), including unit ends via the class's sepAfter.

T7 serialization:
- data-join gains tab, row, para and custom (+ data-copy-sep from the declared sidecar marker);
- paged output wraps bands in .tsr-band[data-b];
- data-track='cell|sidecar|caption' replaces data-cell;
- data-ragged appears on every non-justified line that carries a break-type join;
- code rows gain data-s.

Audit (audit.mjs), changed in the same step:
- right-edge applies to join ∈ {space, none} and not ragged;
- the stacking check groups by data-track.

Copy (copy.mjs):
- ownership iff the range intersects a line;
- empty vs all-omitted lines are distinguished;
- data-copy and data-copy-group;
- no native fallback when lines intersect.

Also: curated --stage=text goldens, and updates to document-model §9.3 and pages-design §2/§5.

**Golden impact:** Re-record html (+ layout where join is printed):
- cells and captions: region/table, region/table-tiny, figure/float, figure/stack;
- code rows: code/wrap, code/tsm-hl, code/hang, code/runs, code/json-hl, code/sidecar, region/fence-handler, inline/fence-edge, pages/paged-doc.html;
- multi-unit blocks or captions: cite/basic, cite/unknown-diag, doc/refs, doc/structure, figure/block, figure/pull-diag, notes/basic, notes/cjk-glue, notes/explicit, region/figure (region/figure.layout.txt:4-5 changes from join=last to space);
- pages/paged-doc.paged.txt (band wrappers).

The runner generates the exact list. e2e runs the audit over region/table and code/sidecar, and copy tests cover tables, sidecars, blank code lines, aside paragraphs and paged output.

**OPS bump (as designed):** False

**Fixes:** `render-runtime/copy-line-separators`, `render-runtime/copy-drops-blank-code-lines`, `break-layout-pages/missed:2`, `render-runtime/missed:5`, `render-runtime/copy-syn-policy`, `render-runtime/audit-hint-attributes`, `render-runtime/normative-doc-drift`

### S9 Anchor authority + idPrefix (with T3 registry)  → plan P3-04

- Internal refs store a target AnchorId, not '#tsr-' URLs, and AnchorNamer owns href and id.
- Anchors come only from registry winners (duplicates get no id plus a diagnostic).
- render.idPrefix is a setting shared by renderTsm and typeset().
- T3 validates reserved prefixes and whitespace.
- S9b (called out separately): anchors on labelled raw blocks and rules, which today are dropped.

**Golden impact:** *.tree.txt of fixtures with refs change (url arg replaced by target). html and semantic goldens are unchanged with the default prefix. The S1 duplicate-label fixture flips from xfail to pass. S9b adds a labelled-raw fixture.

**OPS bump (as designed):** False

**Fixes:** `render-runtime/anchor-namespace`, `resolver/anchor-namespace`

### S10 Geometry authority  → plan P3-16

- margin-bottom comes from block y-deltas (su-rounded), written positionally.
- .tsr-doc gets min-height from DisplayList height.
- Markers and line numbers become Placement{edge=End}; the default spelling is unchanged.
- Sheets clip at the measure extended by T6's gutter extent.
- Error nodes become tsr-err runs and lines with a title.

**Golden impact:** Re-record:
- 33 html goldens: margin-bottom 19.2px→19.203px (1229su). The expected substitution is generated from the runner's config.
- figure/float: min-height.
- error fixtures: tsr-err.
- pages/paged-doc.paged.txt: sheet clip.

A scripted check confirms only these attributes changed.

**OPS bump (as designed):** False

**Fixes:** `render-runtime/trailing-float-and-gap-drift`, `render-runtime/marker-gutter`, `render-runtime/paged-gutter-clipping`, `render-runtime/error-render-divergence`

### S11 Baseline authority  → plan P3-19

- head.container carries per-font content-height factors.
- T4's contract applies them (.tsr-doc, .tsr-cjk, .tsr-code), and runs with user families get an inline value.
- Code rows get class tsr-row and keep their centring line-height.
- T6's baseline uses the CSS formula.
- devAudit adds a baseline check (≤1px).

**Golden impact:** Re-record code fixtures (tsr-row) and style fixtures with user font families. Other html bodies are unchanged; the contract CSS changes. e2e adds the baseline audit on every fixture.

**OPS bump (as designed):** False

**Fixes:** `render-runtime/host-line-height-leak`

### S12 PresentationMap  → plan P3-23

Neutral port first: presentation.def plus four projections reproduce today's semantic bytes, and render.classes is read from T4 settings.

Then deliberate fixes:
- figure caption slots (T3 slots; interim per-paragraph figcaption);
- the sidecar slot projected inline with the declared marker;
- §9.2 term and collect mappings;
- semantic style divergences (sizeMul, sup with bold), via T4's typed writer;
- element, attribute and ARIA allowlists;
- data-role hooks;
- frames from T6 BoxModel, only for classes that declare typeset.frame.

Update document-model §9.2.

**Golden impact:** The port is neutral. The fixes re-record semantic goldens for region/figure, figure/*, code/sidecar, style/* and term/collect fixtures. html goldens change only for fixtures with declared frames (none today) and data-role lines in group fixtures (open question on the default).

**OPS bump (as designed):** False

**Fixes:** `render-runtime/semantic-role-switch`, `render-runtime/typeset-role-blind`, `render-runtime/sidecar-dropped-in-semantic`, `api-measure-code/missed:2`, `real-world-evidence/missed:5`

### S13 Semantic math boxes, a11y  → plan P3-27

- The semantic and fragment math projection defaults to 'boxes' on the page profile, through T8's flattened leaves (estimated text leaves before measurement), with role=math and aria-label from the source.
- The feed profile defaults to source.
- MathML is an opt-in feed format.
- a11y.textLayer (opt-in): aria-hidden lines plus an sr-only semantic element filled from ContentText.

**Golden impact:** Re-record 7 semantic goldens: math/break, math/eqref, math/display, math/stretch, math/inline, math/parse-diag and notes/basic. html gains aria-label on math spans only if a11y.mathLabel defaults on. That is a separate, called-out re-record of math-bearing html goldens, applied with this step.

**OPS bump (as designed):** False

**Fixes:** `real-world-evidence/missed:4`, `math/fallback-and-a11y`, `render-runtime/typeset-a11y`

### S14 Export bundle  → plan P3-36

- renderTsm returns RenderResult, resolved settings, the T9 ResourceManifest and CSS.
- exportStatic gains a template function, hydration from the settings JSON, resource copy or embed, <html lang> from the document language, and page/feed profiles.
- The blog drops its front-matter regex, CSS duplication and per-language engines.

**Golden impact:** None in engine goldens. The Node export smoke test is updated.

**OPS bump (as designed):** False

**Fixes:** `render-runtime/static-export-template`

## Not generalized (kept special)

- **The paint primitive set is closed: Run, Spacer, InlineBox{Glyphs, ImageRef, RawHtml}, and boxes Rule, Image, Raw, Glyphs, Frame.** — Every backend must implement every primitive. Content kinds stay open upstream because they lower into primitives, and RawHtml is the escape hatch. Adding a primitive is an ABI-handshake event, not an extension point.
- **No canvas, SVG or PDF backend.** — The browser shapes inside the line (v2 §8), so the DisplayList carries text runs, not glyph positions. A non-DOM backend would need its own shaper and would fall outside the v2 §7 contract. The DisplayList does not foreclose one.
- **The semantic backend stays a tree walk, not a DisplayList consumer.** — It must run before measurement: first paint, and the Node export with no canvas (pages-design §3, v2 §9). It shares the writer, the namer, PresentationMap, the copy policy and the math leaves.
- **List markers are never measured. Markers and line numbers are right-anchored (Placement edge=End).** — Right anchoring needs no width and keeps '9.'/'10.' aligned, which is today's behaviour. An explicit left edge would need marker strings in the measurement requests, with measurement-buffer and blocks golden churn for no visual gain. The gutter extent needed for clipping comes from T6: digits×ch for code, the item indent for lists.
- **Metric-bearing style declarations stay inline per run; there are no per-StyleId classes.** — StyleIds renumber per document, which would break per-block key stability across edits (editor-design §3). Only metric-neutral user, role and token classes become class tokens, and only on non-ancestors of runs.
- **CJK punctuation squeeze stays a contract class (tsr-sqL/R) instead of explicit margins.** — Explicit margins would churn every CJK golden for no behavioural gain. T4 generates the class from the metric table.
- **The block boundary stays structural for copy (.tsr-para on screen, .tsr-band in paged output) instead of data-join='para' on every block-final line.** — The wrapper is already required by commit and carries block identity. Writing para on every final line would re-record all 48 html goldens for no new information. Unit boundaries inside a block are explicit.
- **DisplayLists are not cached across documents.** — They point into the per-Doc arena, interner and StyleTable, which are freed on every update (worker.mjs:166, :216-218). The process-wide cache holds serialized block HTML keyed by value hashes instead.
- **Page furniture (running heads, page numbers) is deferred to T6's page builder.** — Once T6 lays furniture out as ordinary blocks on a Furniture track from mark templates, paint and the paged backend need nothing new. Designing templates now would precede any owner request (pages-design §5).
- **The Gecko fractional-A4 margin clamp stays in the print behavior.** — It is a browser workaround (shell.mjs:440-450), not a model concern. PageSpec supplies the inputs.
- **Previews show semantic (browser-flowed) HTML with math boxes, not engine-typeset lines.** — Typesetting at popup width needs a second layout and measurement round-trip, which transient UI does not justify. The fragment comes from the post-resolve tree, so numbers and refs are exact.
- **Raw HTML stays the single trusted, unescaped path.** — Document-model §9. HtmlWriter.trustedRaw is the only entry point. Presentation entries can name only allowlisted elements, so they cannot open a second raw-text channel.
- **Code rows keep line-height centring (LineStyle, class tsr-row).** — It is a deliberate code-layout choice (typeset_html.cc:344-348). T6 computes the resulting baseline with the same CSS formula, so it stays explicit.
- **MathML is only an opt-in feed projection, never the default page or typeset path.** — v2 §13 and document-model §9.4 reject MathML on quality, and v2 §9 requires exact math from the start. Boxes are the default, and the feed defaults to source because feed readers drop positioning CSS.

## Risks

- S3 rewrites a 767-line serializer behind a new layout-record seam and must stay byte-identical across 48 html goldens and the paged golden. Mitigation: keep the old serializer behind a flag, diff both over the fixtures and the 199-document corpus, and delete the old one only after that.
- Extending data-join, adding data-track/data-copy and wrapping paged bands changes a normative DOM contract (document-model §9.3, pages-design §2). Third-party consumers could misbehave; none is known, and the VS Code preview reads neither attribute.
- Retaining the committed generation's doc until the next commit ack doubles peak engine memory during edits on large documents. Bound it: the worker releases the doc after a timeout and answers { stale: true }.
- The 'boxes' semantic math uses estimated text-leaf metrics before measurement, so formulas with operator names can shift slightly at upgrade, and permanently on no-JS static pages. Mitigation: T8 estimates with ε over-estimation, and devAudit reports the shift.
- Metric locks with !important in contract CSS can surprise hosts that style typeset text directly. This is intended (I2), but it needs a documented theming guide: paint-only CSS on frames, data-role selectors, metric changes through T4 rules.
- The element and attribute allowlists may be too strict for some user shapes. They are extensible only by an engine release, deliberately, because each addition is a security review.
- BehaviorCtx and the typed ops become public API. Keep them small; changes go through T9's handshake.
- S8 and S10 re-record many goldens. Run each alone, with a scripted attribute-diff check, so review fatigue does not hide unrelated diffs.
- Changing the default copy policy for prose refs (open question) changes user-visible clipboard output and the e2e expectations.

## Open questions (decided in PLAN.md §3)

- Default copy policy for refs: keep 'omit' (today's e2e, test/e2e/typeset.spec.mjs:137-150), or use 'text' for prose refs and 'omit' only for footnote markers? The latter also makes typeset copy match the semantic phase's native copy.
- Should built-in groups (figure, quote, notes) get data-role on their lines by default (html churn in group fixtures), or only declared classes?
- Should built-in figure and quote declare typeset frames by default once T6's BoxModel lands, or stay frameless unless a theme opts in?
- Sidecar copy contract: declared marker plus the note text (proposed), or omit? verbatim-design §5 leaves it OPEN.
- Should a11y.mathLabel (aria-label on typeset math spans) default on? It is cheap and sound, but re-records math-bearing html goldens.
- Should paged output stay HTML sheets for the browser's print engine (pages-design §2), or should PageResult also feed a future PDF writer? This decides whether the DisplayList ever needs glyph positions.
- Should the semantic serializer mark footnote markers and generated prefixes with data-syn/data-copy, so the core copy handler also governs the semantic phase?

## Changelog (critique responses)

- C1-1 (blocker, DLBlock box hoisting + anchor rule): ACCEPTED. Verified in test/golden/region/table.html.txt:6-19: the order is rule, then cells (the first cell carries id=tsr-tbl), then a rule; the rule branch typeset_html.cc:198-206 writes no id. DLBlock.nodes is now ONE ordered variant sequence in T6 materialization order, and only Frame boxes are hoisted. Anchor eligibility is exactly today's rule (first text, code, display-math or image line; never rule or raw). Anchors on raw blocks and rules are a separate called-out step (S9b). The S3 golden statement now explains why it stays byte-identical.
- C1-2 (S8 breaks the e2e audit): ACCEPTED. Verified at audit.mjs:52 (join present and not ragged = justified) and audit.mjs:91 (data-cell skip). The audit change lands in S8 together with the separators, not earlier: emitting data-just in S3 would break S3's byte identity. The right-edge predicate becomes join ∈ {space, none} && !ragged. data-ragged is emitted on every non-justified line that carries a break-type join, data-track replaces data-cell, and the stacking check groups by track. S8's golden impact lists the audit runs over region/table and code/sidecar.
- C1-3 / C2-1 (per-block DisplayList cache unsound across edits): ACCEPTED. Verified: worker.mjs:166 creates a new doc per edit, and :216-218 frees the previous one; DLBlocks hold StrRef, StyleRef and pointers into that arena. The design now caches serialized block HTML plus anchor rows in a process-wide cache. Its key is a value hash (T6 layout hash, text bytes, style values, anchor spellings, MathBox hash, settings generation, idPrefix) supplied as a T9 product key. pid, s0 and margin-bottom moved out of block bytes and are written positionally. The I9 claim is restated as a bench-edit gate rather than an asserted saving.
- C1-4 (fragment, generation and worker races): ACCEPTED. Verified: worker.mjs:269-280 dispatches without awaiting, and paginate (:228-246) sets the page width, awaits, then restores. Every result and request now carries a generation. The worker keeps the committed generation's doc until the next commit ack and otherwise answers { stale: true }. Per-doc serialization and DONE guards are required from T9. ctx.request is replaced by typed ops, and paginate runs on a separate T6 layout product.
- C1-5 (explicit-left markers need an unmeasured width): ACCEPTED. Verified: shell.mjs:30-31 uses right:100%, and layout.cc:298-304 stores only the marker StrRef. Markers and line numbers use Placement{edge=End}, right-anchored and serialized byte-identically. Clipping uses T6's gutter extent (digits×ch for code, the item indent for lists). Measuring markers is listed under not_generalized with its cost.
- C1-6 (writer-only sanitizer splits measurement from paint): ACCEPTED. Verified: canvas_measure.mjs:24-31 builds ctx.font from the same family string and updates fontKey regardless of whether the assignment succeeded. The writer sanitizer is removed. Values are validated once at ingest (T4), and both the measurement requests and HtmlWriter read the validated value. Backslash CSS escapes are valid family syntax. render-runtime/missed:2 is now T4's alone and is no longer claimed by S2.
- C1-7 (element and ARIA names as a second raw channel): ACCEPTED. ElementName and AttrName are allowlisted enums: no raw-text or RCDATA elements, no embedded or interactive elements, and no on*, style, src or href attributes. ARIA roles are validated against WAI-ARIA 1.2. Anything invalid raises a diagnostic and falls back to div[data-role]. The a11y text layer uses only allowlisted elements.
- C1-8 / C2-12 (role classes on ancestors of measured runs; frame insets untyped): ACCEPTED. Verified: the contract (shell.mjs:23-29) sets no font-style or weight on lines or runs. In the typeset DOM, role hooks are now data-role attributes plus classes on T6 frame boxes only. T4's generated contract locks inherited metric properties on .tsr-line with !important. Frames carry explicit inline geometry including border widths from T6's BoxModel. devAudit gains a metric-drift check. The theorem example now routes an italic body through T4 style rules.
- C1-9 (copy owns the clipboard while the overlay sits in the container): ACCEPTED. Verified: copy.mjs:47-56 installs the handler on the container. The core copy handler now owns the clipboard iff the range intersects at least one .tsr-line of a commit root. Non-line parts of a mixed range (overlay, host content, a11y layer) contribute their native text, so previews stay copyable.
- C1-10 / C2-20 (line-height:normal does not give top+ascent): ACCEPTED. Verified: mathLeaves pins line-height to the hhea height (typeset_html.cc:93-117), and vmet is the primary font's fontBoundingBox (canvas_measure.mjs:44-47). The new S11 'baseline authority' step pins explicit content-height line-heights. Per-font factors travel in head.container and are applied by T4 contract classes; only user-family runs carry an inline value. Code rows keep centring, T6 computes the baseline by the CSS formula, and an e2e baseline audit enforces ≤1px. Golden churn is confined to code and user-family fixtures.
- C1-11 / C2-1 (JSON RenderResult cost): ACCEPTED. The protocol now sends only blocks the shell lacks: a block table plus one transferable HTML buffer holding only unheld keys, with no JSON escaping. bench-edit at 7.8K, 35K and 87K is an S4 merge gate.
- C1-12 / C2-9 (I3 accounting for user synthetic text; render record on the ops boundary): ACCEPTED. Verified: executor.mjs:116-137 handlers return plain content, and the ops ArgK list has no origin key. SynKind is now an open interned name. User text is tagged through T2's `syn`/`copy` content attributes ($.synthetic), an OPS_VERSION bump owned and counted by T2. The `render:` record no longer rides T3's declaration op: it is a T4 settings JSON value whose schema the settings parser validates, versions and fuzzes. The 'no engine change' claims were removed.
- C1-13 / C2-11 (frames across page cuts; paged closure): ACCEPTED. T6 emits per-band frame fragments with openEdges, which T7 paints as slices. The paged backend wraps each band in .tsr-band[data-b], which carries block hooks and copy block identity. Running heads and page numbers move to not_generalized: once T6 lays them out as Furniture-track blocks, paint needs nothing new.
- C1-14 (display math needs a placed inline box): ACCEPTED. DLInlineBox has an optional Placement. Display math is specified as one line holding a placed Glyphs inline box plus the eqno run, which keeps it inside .tsr-line for copy and keeps the bytes.
- C1-15 (float px fields): ACCEPTED. Every printed px field is double (layout.h:13-14 is double).
- C1-16 (S10 numbers wrong): ACCEPTED. Verified: 94 occurrences of margin-bottom:19.2px in 33 of 48 html goldens; the layout gap is 2868-1639 = 1229su = 19.203125px. S10 now says 33 goldens, 19.2px→19.203px, with the substitution generated from the runner's config.
- C1-17 (anchors[] determinism; label table discarded): ACCEPTED. Verified: resolve.cc:50 is an unordered_map local to Resolver. anchors[] is built from the AnchorIds the writer emitted, in paint order. T3 must export a persistent, document-ordered registry.
- C1-18 (duplicate labels on kinds without auto anchors): ACCEPTED. Emit and paint take anchors only from registry winners. A loser without an auto anchor gets no DOM id plus a diagnostic. S1 adds a duplicate-label mathblock fixture (xfail until S9), and the T3 interface records the rule.
- C1-19 (shell ids collide with label ids): ACCEPTED. Verified with tsrc: '= Printing <print-root>' emits id=tsr-print-root, and shell.mjs:436 removes that element. Shell and behaviors hold nodes by reference or mark them with data-tsr-* attributes. The id namespace belongs to document labels alone.
- C1-20 / C2-17 (identEscape undefined; ns vs reserved prefixes; example wrong): ACCEPTED. id() is now prefix plus HTML-attribute escaping, byte-identical (tsr-h-1.1 stays). Whitespace is rejected at T3 registration. AnchorRef.ns was dropped in favour of reserved prefixes only, which are html-neutral. The theorem example uses label thm-ua and lists its T1, T3, T6 and T4 dependencies.
- C1-21 / C2-19 (attr assert aborts WASM; pendingAnchor hidden state): ACCEPTED. Release builds are first-wins plus a diagnostic; debug and native tests assert. pendingAnchor was removed: the list projection hoists anchors explicitly, and T3 does not move labels (split note on semantic-footnote-ids-dangle).
- C1-22 (blank vs all-synthetic lines in copy): ACCEPTED. A line with no items contributes its separator. A line whose items are all omitted contributes nothing. Both copy.mjs and contentText implement this, and the curated --stage=text goldens cover it.
- C1-23 (per-document kind; per-pid upgrade records): ACCEPTED. State is per block (semantic, estimated or exact), so mixed results are expressible. Records are per replaced range { oldPids, newPids, rects() }, read lazily only when a listener exists.
- C1-24 (hash64 collision; no structural guard): PARTIALLY ACCEPTED. Keys are 128-bit, and commit keeps element references and verifies them, falling back to replacing that range. The full-HTML compare is rejected: the delta protocol deliberately does not resend held blocks, and a 128-bit collision is negligible.
- C1-25 / C2-6 (semantic math must not stay $…$): ACCEPTED. Verified: v2 §9 says 'Math is exact from the start … formulas never render natively', and document-model §9.4 and v2 §13 reject MathML. The math projection defaults to 'boxes' on the page profile, using the same Glyphs leaves with role=math and aria-label. Text leaves use T8 estimates before measurement. The feed profile defaults to source, and MathML is opt-in for feeds only. S13 calls out the re-record of 7 semantic goldens.
- C1-26 (one live doc per engine): ACCEPTED. Verified at shell.mjs:340-343 and :375-378, and in the blog's per-language engines (eleventy.config.js:83-84). The core keeps a Map from container to session, so one worker serves many documents.
- C1-27 (migration bookkeeping a-d): ACCEPTED. (a) dumpLayout stays unchanged until T6 replaces it; the DisplayList no longer claims it. (b) Code-row spans are scheduled in S8 with their churn listed. (c) audit-hint-attributes is fixed in S8, not S3. (d) Track is {kind, u16 index}.
- C2-2 (paginating over DisplayList lines inverts the layers): ACCEPTED. The band cutter moves verbatim to layout/paginate.cc over LayoutResult, indexed by layout ids (T6 code). PageResult refers to L6 entities, pagination properties never enter the DisplayList, and PageSpec is T6's. T7 only reads it in print().
- C2-3 (two parallel line records): ACCEPTED. T6 owns LaidOutLine and LaidOutBox (geometry, Just, Track, Sep, anchor, class path, marks, GapReal, gutter). DLLine is a pointer to that record plus hooks and runs. S3 seeds the record as an adapter that T6 then owns, and S8's layout-side Sep production is assigned to T6.
- C2-4 (four declaration channels): ACCEPTED. The single channel is T4's settings key render.classes.<cls>, with $.element(…, { render }) as sugar for it. T3 declares only category, like and slots. T6 owns frame geometry, and a frame exists iff BoxModel yields one, so regionBox was removed. T4 owns class tokens, paint values and per-key precedence; T7 no longer restates precedence.
- C2-5 (SemanticShape too weak; built-ins privileged): ACCEPTED. The shape now has element or elFromArg, wrappers, an attribute list from args or constants, a slot map, an inline flag and a selectable projection. Every built-in kind, including §9.2's term and collect, is a row of presentation.def. Only four structural projections remain as code, and any class can select one. The closed Landmark enum was replaced by `like:` inheritance.
- C2-7 (paragraph separators inside a block and in paged output): PARTIALLY ACCEPTED. Verified with a probe: two #!aside paragraphs share one .tsr-para with no data-join, so they copy with a single newline. Unit ends inside a block now serialize data-join=para or newline from the class's sepAfter, and paged output gets block identity via .tsr-band[data-b]. The block boundary itself stays structural rather than writing para on every block-final line, which would churn all 48 goldens for no new information (not_generalized).
- C2-8 (S8 audit; overloaded u8 track): ACCEPTED. See C1-2. Track is {TrackKind, u16 index}, and audit.mjs plus the e2e expectations are in S8's change list.
- C2-10 (untyped payloads; mathfont in backends; no style on boxes): ACCEPTED. Payload is a std::variant of Glyphs (T8 flattened GlyphLeaf/RuleLeaf with fontRole), ImageRef and RawHtml. DLInlineBox and DLBox carry StyleRef and RunLink, so math, images and rules get colour, lang, classes and links. No backend includes mathfont.h.
- C2-13 (paint still evaluates the gap predicate): ACCEPTED. T5 defines gap classes, T6 realises each gap's width and carrier (GapReal), and paint only copies the values. S3's interim uses one shared predicate inside the layout adapter. The ownership open question is closed.
- C2-14 (eqno 'no tag measurement' conflicts with T6/T8 DisplayBox): ACCEPTED. edge=End is now only the interim encoding. Once T6 places a measured tag with a collision policy, Placement carries an explicit x and y.
- C2-15 (removable copy; double preview policy; untyped request): ACCEPTED. Copy is core: replaceable, never absent (v2 §8 'Required'). refPreview() honours anchors[].preview by default, and its classes option is a documented host override that wins. ctx.request is replaced by ctx.ops, which exposes only operations declared in T9's typed protocol.
- C2-16 (anchors table misses bib entries; backlink in fragments): ACCEPTED. Verified: resolve.cc:421 sets a 'bib-' label without registering it, and :263 builds the URL. The anchors table is built from emitted AnchorIds plus the T3 class, and T3 must register every anchored node including bib entries and terms. The backlink is a T3 slot whose presentation sets omitInFragment.
- C2-18 (sidecar keyed by a private role; hard-coded marker): ACCEPTED. The projection is keyed by the codeblock's sidecar slot (a T2 first-class slot). The separator and the semantic prefix come from the fence's declared marker (doc.h:94) via sepText.
- C2-21 (line marks are not class presentation): PARTIALLY ACCEPTED. LineMark is a list on T6's line record, sourced from content line attributes (hl, diff, focus …), and tsr-hlline was removed from PresentationMap's replaces. Paint serializes a mark as a line class and the row's explicit height, which keeps today's bytes. The proposed extra per-line Region box was rejected as redundant, because the line element already spans the row.
- C2-22 (economy): PARTIALLY ACCEPTED. The DL dump is a debug stage with no goldens. RENDER_CONTRACT_VERSION was replaced by T9's single ABI handshake at load. Text goldens cover a curated copy set rather than all 48 fixtures. Copy settings collapse into one copy.policy map plus per-class overrides; the general map is kept instead of only copy.ref, because user syn kinds need a policy entry.
- C2-23 (subsumption bookkeeping): ACCEPTED. Each input id now has exactly one row and one fixing step. render-runtime/missed:2 belongs to T4 and S2 no longer lists it. break-layout-pages/missed:1, which is not a T7 input, was removed from S2. Split notes now cover ids shared with T3, T4, T5, T6, T8 and T9.
- Missing-items lists (both critics: real-world-evidence/missed:4, math/fallback-and-a11y, marker-gutter, missed:2, audit-hint-attributes, host-line-height-leak, copy-line-separators, sidecar-dropped-in-semantic, api-measure-code/missed:2): ADDRESSED respectively by math boxes (S13), Placement edge=End plus T6 gutter extent (S10), T4 ingest validation, Just/Track in S8, baseline authority (S11), explicit unit separators and .tsr-band (S8), and the slot-keyed sidecar projection (S12).
- Overlaps (both critics): RESOLVED in the interfaces section.
  - T6 owns line records, pagination, PageSpec, frames and gap realisation.
  - T3 declares category, like and slots, and provides the persistent registry.
  - T4 owns validation, the settings channel and precedence.
  - T2 owns syn/copy attributes and the sidecar slot.
  - T8 owns flattened leaves and estimates.
  - T9 owns serialization, generations, typed ops, the handshake and product keys.
