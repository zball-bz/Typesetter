---
name: tsm
description: Syntax of .tsm, the Typesetter markup language — blocks, inline markup, labels and references, math, code fences, regions, tables, figures, notes and generated lists, and the # scripting layer. Use when reading, writing or editing .tsm files.
---

<!-- {{generated-note}} -->

# .tsm syntax

A `.tsm` file is UTF-8 text. CRLF reads as LF; a tab advances to the next
multiple of 4 columns. Syntax version {{syntax-version}}. Tables of every
constructor, style key, math function and math symbol: [reference.md](reference.md).

## Paragraphs and line breaks

- Blank lines separate blocks.
- The lines of one paragraph join with a soft break: a space, or nothing
  between two characters that join without one (two CJK characters), also
  across markup — `这是*强调*` at a line end followed by `中文` reads 这是强调中文.
- A run of blanks reads as one space.
- `\` at the end of a line is a hard line break; `#linebreak` is the same.

```tsm
First line of a paragraph
continues here.\
After a hard break.

A second paragraph.
```

## Front matter

A `---` line at the very start of the file through the next `---` (or `...`)
line is metadata, skipped, only when the host sets `source.frontMatter`
(static site generators, the VS Code extension). Without it those lines are
markup: a rule and a paragraph.

## Headings and rules

`=` to `======` and a space start a heading of level 1–6. A label
` <id>` may end the line. `---` alone on a line is a rule.

```tsm
= Title <top>
== Section <sec-a>
====== Level six

---
```

## Lists

- `- ` is a bullet item, `+ ` an ordered item, `N. ` an ordered item
  starting at N.
- An item's content column is right after its marker. Continuation lines,
  blank lines and nested blocks indented to that column belong to the item.
- `-`, `+` and `N.` lists never merge: a change of marker class starts a new
  list.
- An item interrupts a paragraph only when it has content, and an `N.` item
  only when N is 1 (`1984. Then …` on a paragraph's second line continues the
  paragraph).

```tsm
- bullet
- another bullet,
  continued on an indented line

  a second paragraph of the same item
  - nested bullet
+ ordered
+ ordered, second
3. ordered, starting at 3
4. next
```

## Description lists

`/ term: description` lines, one after another, form a description list. The
term runs to the first `:` followed by a blank or the line end (outside code,
math and escapes; `\:` keeps a colon in a term). The description continues on
lines indented past `/ `.

```tsm
/ Measure: the width of a line.
/ Leading: the distance between baselines,
  continued on an indented line.
```

## Quotes

`> ` prefixes a quote line; `> > ` nests. A blank line ends the quote.

```tsm
> A quoted paragraph
> over two lines.
> > A nested quote.
```

## Comments

- `%-- … --%` inside a line is a comment; comments nest. One left open within
  its block is literal text (with an error).
- A line starting with `%--` outside a paragraph starts a block comment that
  runs to `--%`. Inside a paragraph such a line is an inline comment and does
  not split the paragraph.

```tsm
Text %-- not shown --% continues.

%--
A block comment.
--%
```

## Inline markup

| syntax | meaning |
|---|---|
| `*text*` | strong |
| `_text_` | emphasis (on CJK text: emphasis dots) |
| `` `code` `` | code span |
| `[text](url)` | link: a relative reference, `http(s):` or `mailto:` |
| `https://…` | a bare URL: a link to itself |
| `$…$` | inline formula |
| `^[…]` | footnote |
| `@id`, `@[id]`, `@[a, b]`, `@id[…]` | references (below) |
| `#…` | a splice: a value, a call, a statement (below) |

- **Pairs** `*…*` and `_…_` are strict: the opener needs a non-blank after
  it, the closer a non-blank before it.
- **Inside words**: `*` and `_` between two ASCII letters or digits are text
  (`snake_case`, `2*3*4`). Neighbours that are not ASCII do not guard:
  `中*强调*文` is strong. Emphasis inside a word is `#em[…]`:
  `d#em[upper]case`.
- `@` and a bare `#name` right after an ASCII letter, digit, `_` or `$` are
  text (`user@host.org`, `C#`); a call stays a call (`H#strong[2]O`).
- **Code spans**: a run of N backticks closes at the next run of exactly N
  (``` ``a`b`` ``` is the code a`b); one leading and one trailing space are
  stripped when both are present. An unclosed run is literal.
- **Bare URLs** (`http://`, `https://`) end at a blank, a non-ASCII character
  or one of `` < > " ` | \ $ ``; trailing `. , : ; ! ? ' * _ ~` and an
  unbalanced `)` or `]` stay outside. Inside a URL `_` and `*` are part of it.
  `https\://…` keeps a URL as plain text.
- **Escapes**: `\` before ASCII punctuation is that character (`\*`, `\#`,
  `\$`, `\[`, `\\`). Before the end of a line it is a hard break. Before
  anything else it is a backslash (`C:\temp`).
- **Brackets** inside code spans, formulas, comments and splice arguments do
  not close link text, a content argument or a footnote:
  `[range $[0,1)$](https://example.com)` is one link.
- **Across lines**: a construct left open at a line end (a formula, a code
  span, a call's parentheses or content, an inline comment) continues on the
  following lines of its paragraph, where block markers are text. One still
  open at a blank line is literal text from its opener.

```tsm
*Strong*, _emphasis_, `code`, a [link](https://example.com), a bare
https://example.com/a_b, $e^(i pi) + 1 = 0$, a note^[The footnote text.],
snake_case, C:\temp and \*stars\*.
```

## Labels

A label is ` <id>`: `id` is any characters but whitespace and `<>[]@,;\`
(`<sec:intro>`, `<标签>`). It ends a heading line, a display formula, a fence
opener or a region opener; constructors take `label: "id"`. Of two equal
labels the first keeps its anchor. Ids shaped like generated anchors
(`h-2.1`, `fn-3`, `bib-x`) are reserved.

## References and citations

- `@id` — a bare id is `[A-Za-z_][A-Za-z0-9_]*` parts joined by `-`, `.` or
  `:` (`@sec:intro`; in `@fig-1.` the period is prose). Any other label:
  `@[标签]`.
- `@[a, b]` refers to several; three or more consecutive citation numbers
  compress (`[1–3]`).
- `@id[…]` adds a supplement: for a label it replaces the word
  (`@fig-a[Fig.]` reads "Fig. 1"); for a citation it is the locator
  (`@kp81[p. 5]` reads "[1, p. 5]" when kp81 is the first source cited).
- A reference reads as its target's number with the word of the document's
  language: §1, Figure 1 / 图 1, Table 1 / 表 1, Eq. (1) / 式 (1). A target
  without a number shows its label text.
- The same `@key` cites a bibliography entry (`#bibliography`, below).
- `#ref("id", {form, supplement})`: `form` is `number`, `title`,
  `supplement`, `full` or a form the element declares.
- `#link({target: "id"})[text]` links to a label with its own text.

```tsm
= Introduction <intro>

See @intro, #ref("intro", {form: "title"}), @intro[Section] and
#link({target: "intro"})[the start].
```

## Math

`$…$` is an inline formula. A formula padded by blanks, `$ … $`, standing as
its own paragraph is a display formula; a display formula inside a paragraph
splits the paragraph around it. A label after a display formula numbers it.

```tsm
Inline $a^2 + b^2 = c^2$ in text.

$ int_(-oo)^oo e^(-x^2) d x = sqrt(pi) $ <gauss>

See @gauss.
```

- **Scripts**: `x_1`, `x^2`, `x_(i j)^(n+1)`; `x^ab` is x to the power ab.
  Primes: `f'`, `f''`; `f'^2` puts the prime and the 2 in one superscript.
- **Grouping**: parentheses around a script, fraction or call operand vanish;
  `{…}` and `[…]` show (`x^{a}` shows the braces).
- **Fractions**: `a/b`, `(a+b)/2`; `/` binds tighter than relations.
  `frac(a, b)` too.
- **Calls** bind only on a `(` directly after the name: `sqrt(x)` is a root,
  `sqrt x` the name "sqrt".
- **Names and text**: a known word is its symbol (`alpha`, `oo`, `sum`, `RR`);
  an unknown word is an upright operator name; `"text"` is upright text.
- **Negation**: `!` touching a relation negates it (`!=`, `!in`, `!exists`,
  `!models`); `n!` is a factorial.
- **Delimiters** `( ) [ ] { } | ‖ ⟨ ⟩ ⌊ ⌋` stretch around tall content; a bar
  alone in a group is its middle (`{x | x > 0}`, `P(A | B)`). `big(()`,
  `Big`, `bigg`, `Bigg` force a size.
- **Big operators** `sum`, `prod`, `int`, `oint`, `lim`: limits above and
  below in display, beside inline; `limits(…)` and `scripts(…)` choose.
- **Rows**: `;` separates rows and `&` cells in `mat`, `cases`, `aligned`
  (`,` also separates matrix cells). `&` is always an alignment point; an
  ampersand is `"&"`.
- **Equation blocks**: display formula lines one after the other (no blank
  line between) form one block aligned at `&`; each labelled row has its own
  number. Inside one display formula `\` at a line end starts a new row; the
  formula keeps one number.
- `\$` is a dollar sign. `#name` and `#(expr)` are holes: a number is set as
  math, a string as text, a math value as its formula (`$x^#n$`).
- Spaces: `thin`, `med`, `thick`, `quad`, `wide`. Alphabets: `bb(R)`,
  `cal(A)`, `frak(g)`, `bold(v)`, `italic(h)`, `sans(x)`, `mono(x)`;
  `AA`…`ZZ` are blackboard letters. Typed Unicode symbols (`≤`, `∑`, `→`)
  behave as their names.

```tsm
$mat(1, 2; 3, 4)$, $abs(x) = cases(x & "if" x >= 0; -x & "otherwise")$,
$overbrace(a + b, n)$, $attach(x, t: a, b: i)$, $bb(R)$, $!exists n!$.

$ (a+b)^2 &= a^2 + 2 a b + b^2 $ <sq-1>
$ (a-b)^2 &= a^2 - 2 a b + b^2 $ <sq-2>

$ f(x) &= (x+1)^2 \
&= x^2 + 2 x + 1 $
```

## Code blocks

A fence opener is ```` ```tag(args) info words <label> ````. The fence closes
at a line of at least as many backticks; a longer run shows backtick fences
inside. Arguments are named only. Languages: {{languages}}.

| argument | meaning |
|---|---|
| `lineNo: N` | line numbers starting at N (`true`: 1) |
| `hl: "2,5-7"` | highlighted lines |
| `wrap: false` | no wrapping (wrapped lines continue indented on a character grid) |
| `sidecar: "///"` | text after the marker on each line is a margin note in markup |
| `snapKerning`, `contIndent`, `sidecarFrac`, `features` | grid kerning, continuation indent (columns), note column width, font features |
| `overlays: ["noweb"]` | literate overlays (`<<name>>` references, `<<name>>=` headers) |

````tsm
```js(lineNo: 1, hl: "2") A title <code-1>
const memo = new Map();
function fib(n) { return n <= 1 ? n : fib(n - 1) + fib(n - 2); }
```

```js(sidecar: "///")
const memo = new Map(); /// a *cache* with $O(1)$ lookups
```
````

## Regions

`#!name(options) <label>` on its own line opens a region; the line `#name!`
closes the innermost open region of that name. Options are named only:
`style: {…}` styles the region, `label:` labels it, every other option is the
region's data. A name with no handler of its own makes a block group of role
`name`, which rules and `$.element` style and number. Built-in regions:
`figure`, `table`, `refsection`.

```tsm
#!aside(title: "Note", style: {box: {padding: "0.4em 0.8em"}}) <aside-1>
Paragraphs, lists and code blocks.
#aside!
```

## Tables

`#!table(…)` takes one row per line; an unescaped `|` at the top level of the
line cuts cells (`|` inside a formula, a code span or a call stays; `\|` is a
bar).

| option | meaning |
|---|---|
| `cols: 3` | number of columns; or a list `[{width: "auto", align: "l"}, {width: "1fr"}, {width: "6em", align: "r"}]` (`auto`, `min-content`, `max-content`, fr, a length or a percent) |
| `align: "lcr"` | per-column alignment |
| `rules: "booktabs"` | `booktabs`, `grid` or `none` |
| `header: 1` | header rows (repeated on each printed page) |
| `label: "id"` | label (numbered as a table) |

A cell may be `#cell({colspan, rowspan, align, valign})[…]` and may hold
blocks.

```tsm
#!table(cols: 3, align: "lcr", rules: "booktabs", header: 1, label: "tbl-units")
Quantity | Value | Unit
Speed | 3 | m/s
Span | #cell({colspan: 2})[two columns]
#table!
```

## Figures and images

`#!figure(…)` holds an image (`src`) or any blocks; its paragraphs are the
caption, numbered (Figure 1 / 图 1). A figure whose body is one table numbers
as a table unless it says `kind: "figure"`; a figure inside a figure is a
subfigure (1(a)).

| option | meaning |
|---|---|
| `src`, `alt` | the image and its text |
| `w`, `h` | intrinsic size in px (one alone keeps the aspect ratio) |
| `scale` | width as a fraction of the measure |
| `float: "left"` / `"right"` | float the figure; text wraps beside it |
| `label`, `kind` | label; `kind: "figure"` keeps a table figure a figure |

`#image("src", {w, h, alt})` sets an image in running text, on the
baseline. Any block floats with `style: {place: {float: "right", width:
"45%"}}`; `place: {float: "inline"}` sets blocks side by side; `"top"`,
`"bottom"`, `"page"` make page floats.

```tsm
#!figure(src: "/images/plot.png", alt: "A plot", scale: 0.5, label: "fig-plot")
The caption.
#figure!

As @fig-plot shows, an inline image #image("/images/dot.png", {w: 12, h: 12}) sits on the baseline.
```

## Notes, terms, generated lists

- `^[…]` is a footnote; its body may hold several paragraphs and lists.
- `#note({label: "id"})[…]` is a named note; `#ref("id", {form: "marker"})`
  places a further marker to it.
- `#term[name][definition]` defines a term; `#glossary` lists the terms.
- `#entry({role: "index", key: "k", sortKey: "k"})[text]` marks an index
  entry; `#index()` lists them.
- `#toc`, `#lof()`, `#lot()`, `#notes()` place a table of contents, the
  lists of figures and tables, and the notes (by default at the end).
- `#bibliography("refs.json")` lists the cited entries of a CSL-JSON file
  where it stands (`{cited: "cited-then-all"}` adds the rest); `@key` cites.
  `#!refsection … #refsection!` numbers its citations afresh.

```tsm
A claim^[A footnote.] with a #term[measure][The width of a line.] and a
cited source @knuth84 #entry({role: "index", key: "measure"})[measure].

#glossary

#index()

#bibliography("refs.json")
```

## Splices: the `#` layer

- `#name` places a value; `#a.b` a property; `#(expr)` a JavaScript
  expression; `#f(args)` a call. A bare nullary constructor is called
  (`#toc`). `;` ends a splice (`#x;y`).
- **Content arguments** are `[…]` directly after the call, one or more:
  `#f(a)[first][second]`. A `[` that ends its line opens a block-form body,
  closed by a line whose first non-blank character is `]` at or left of the
  opener's indentation; it holds blocks.
- **Named arguments** pass one options object: `#card(tone: "warm")[…]` is
  `card({tone: "warm"}, …)`. Named and positional arguments together are an
  error.
- **Values**: a node stays itself; strings and numbers are text; `null`,
  `undefined` and `false` render nothing; arrays flatten.
- `#let x = expr` (to the line end or `;`) binds a name for the rest of the
  document; repeating it reassigns. `#let x = [ … ]` is a content literal
  (markup, placed by `#x`). `#{ … }` is a JavaScript statement block. Both
  work in any container; `await` is allowed.
- **Keyword forms**: `#if (c) [A] else if (d) [B] else [C]`,
  `#for (const x of xs) [#x, ]`, `#while (c) […]`; a body's edge blanks are
  kept.
- `#use("./lib.mjs")` runs a module's default export as `default($, std)`
  (register fences, regions, math, elements); its `fences`, `regions` and
  `providers` exports register by name.
- `` m`*markup* ${x}` `` builds content from markup; `m.parse(src)` parses a
  string.
- Names beginning `__` are reserved; JavaScript keywords other than the
  keyword forms are not splice heads.

```tsm
#let n = 3
#let greet = [Hello *world*]

#greet: #if (n > 2) [many] else [few]; #for (const x of [1, 2, 3]) [#x, ]
and #(n * 2).
```

### Constructors

Every kind has a constructor with one calling convention: leading arguments
bind to its parameters while they fit, then one options object, then the
content: `#heading(2)[Title]`, `#list(true)[#item[a] #item[b]]`,
`#codeblock("js", "x")`, `#image("/a.png", {w: 20})`. Every constructor also
takes `label`, `role`, `class`, `slot`, `syn`, `copy` (`text`, `omit`,
`replace:…`), `ext: {name: value}` and `style: {…}`. Aliases: {{aliases}}.
The full list with options: [reference.md](reference.md#constructors).

### Style

A style patch is an object of style keys ([reference.md](reference.md#style-keys)):
`{color: "#a8432a", weight: 600, size: "0.9em", par: {indent: "2em"},
box: {padding: "0.4em"}, text: {wrap: "nowrap"}}`. Sugar: `bold`, `italic`,
`underline`, `overline`, `strike`.

- `#style({…})[…]` styles its content; `{style: {…}}` on a constructor
  styles that node.
- `$.set(selector, patch)` (in `#{ … }`) styles every later node the
  selector matches; `#style.where(selector, patch)[…]` does so inside its
  content. A selector is a kind name (`"code"`, `"heading"`, `"para"`) or
  `{kind, role, class, lang, depth, …the node's own attributes}`
  (`{kind: "heading", level: 2}`, `{kind: "codeblock", lang: "cpp"}`).
- Order, last wins: engine defaults, host rules, the document's rules, a
  node's own style, host rules marked `force`.

```tsm
#{ $.set({kind: "heading", level: 2}, {color: "#2f5d8a"}) }

== A blue heading

#style.where("code", {color: "#a8432a"})[Red `code` here], and
#style({weight: 600, size: "0.9em"})[semibold, smaller].
```

### The `$` API (inside `#{ … }` and `#let`)

| call | does |
|---|---|
| `$.set(sel, patch)` | a rule (above) |
| `$.element(name, spec)` | declares an element class: counter, supplement, title, sites, numbering, flow, html, preview |
| `$.counter(name, {within, numbering, start, gap, levels})`, `$.counter.system(name, {symbols, mode})` | counters and numbering systems |
| `$.collector(name, {select, head, entry, like, scope})` | a generated list of classes |
| `$.region(name, (body, ctx) => content)` | a region handler (`body.blocks()`, `body.rows()`, `ctx.args`, `ctx.next(body)`); returns a constructor |
| `$.fence(tag, (body, ctx) => content)` | a code-fence handler (`ctx.info`, `ctx.raw(html, opts)`, `ctx.next(body)`) |
| `$.ctor(name, next => (call, ctx) => content)` | overrides a constructor (markup included: `$.ctor("strong", …)` changes `*x*`) |
| `$.math.symbol(name, {char, class})`, `$.math.op(name, {limits})`, `$.math.fn(name, params, body)` | math vocabulary from that point on |
| `$.doc({lang})` | the document's own language |
| `$.locale(tag, {terms: {figure: "Fig. "}})` | the document's words for a language |
| `$.load(src, {as: "text" \| "json" \| "bytes"})` | reads a resource relative to the document (a promise) |
| `$.labels.import("ch1.labels.json")` | labels of another document of a project |
| `$.bib.format = (entry) => content` | replaces the bibliography entry format |

`counterUpdate(name, {set, step, add, numbering, supplement})` placed as
`#…` changes a counter at that point. Numbering patterns: `1`, `a`, `A`,
`i`, `I`, `①`, `一`, `*` and `{system}`, with any prefix, separator and
suffix (`"A.1"`, `"(i)"`, `"§1"`).

```tsm
#{ $.element("theorem", {counter: {name: "thm", within: "heading", depth: 1}, supplement: {en: "Theorem "}, title: {arg: "title"}, sites: [{where: "prepend:first-para", template: [strong(slot("supplement"), slot("number")), " "]}]}) }

= Results

#!theorem(label: "thm-main", title: "Main")
The statement.
#theorem!

By @thm-main.
```

### Other inline pieces

`#fill` takes the rest of its line (`Left #fill right`). `#attach("prev")[…]`,
`#attach("next")[…]` and `#attach("both")[…]` attach content without a space
or break on that side. `#pagebreak()` starts a new printed page.
`#raw("<b>html</b>", {w, h, measure: "host"})` places trusted HTML.
`#para({label: "p1"})[…]` is a labelled paragraph.
