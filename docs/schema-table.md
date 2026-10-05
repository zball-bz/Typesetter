<!-- GENERATED from engine/schema/schema.json by tools/gen-schema.mjs — do not edit. -->
# Ops vocabulary (generated)

The kind table of document-model §2.1, generated from `engine/schema/schema.json`. Ops version 7, min compat 6.

| id | kind | level | body | inline | attributes (writer order: domain) | constructor |
|---|---|---|---|---|---|---|
| 0 | `doc` | block | blocks | unsupported | — | — |
| 1 | `para` | block | inline | container | — | `para(…)` |
| 2 | `heading` | block | inline | unsupported | `level`: int:1:6; `label`: label | `heading(level, label, …)` |
| 3 | `list` | block | items | unsupported | `ordered`: bool; `start`: int:-1073741824:1073741824 | `list(ordered, start, …)` |
| 4 | `item` | block | blocks | unsupported | — | `item(…)` |
| 5 | `quote` | block | blocks | unsupported | — | `quote(…)` |
| 6 | `codeblock` | block | code | unsupported | `lang`: token; `wrap`: bool; `lineNo`: int:0:1048576; `hl`: rangeset; `sidecar`: str | `codeblock(lang, lines, {options}, …)` |
| 7 | `rule` | block | none | unsupported | — | `rule()` (nullary) |
| 8 | `group` | adaptive | position | container | `role`: ident; `label`: label; `name`: str | `group({options}, …)` |
| 9 | `table` | block | rows | unsupported | `cols`: int:1:64; `align`: token; `label`: label | `table(body, {options}, …)` |
| 10 | `trow` | block | cells | unsupported | — | `row(…)` |
| 11 | `tcell` | block | inline | unsupported | — | `cell(…)` |
| 12 | `term` | adaptive | inline | unsupported | `name`: str | `term(name, …)` |
| 13 | `collect` | block | data | unsupported | `what`: enum:toc\|glossary\|notes\|bibliography; `form`: enum:all | `collect({options}, …)` |
| 14 | `mathblock` | block | none | unsupported | `src`: str; `label`: label | `mathblock(src, label, …)` |
| 15 | `error` | adaptive | none | error | `message`: str; `code`: ident | `error(code, message, …)` (sealed) |
| 16 | `comment` | trivia | text | skip | — | `comment(text, …)` |
| 17 | `text` | inline | none | text | — | `text(text, …)` (sealed) |
| 18 | `styled` | transparent | position | container | `bits`: flags:EM=2,BOLD=3,UNDER=16,OVER=17,STRIKE=18; `font`: font; `lang`: lang; `color`: color; `sizePx`: num:1:2000 | `styled({options}, …)` |
| 19 | `link` | inline | inline | container | `url`: url | `link(url, …)` |
| 20 | `code` | inline | text | code | — | `code(text, …)` |
| 21 | `ref` | inline | none | container | `target`: str | `ref(target, …)` |
| 22 | `mathinline` | inline | none | object | `src`: str | `mathinline(src, …)` |
| 23 | `raw` | block | none | object | `html`: html; `w`: num:0:100000; `h`: num:0:100000 | `raw(html, {options}, …)` |
| 24 | `hardbreak` | inline | none | break | — | — |
| 25 | `seq` | transparent | position | container | — | `seq(…)` (sealed) |
| 26 | `image` | block | none | object | `src`: url; `alt`: str; `w`: num:0:100000; `h`: num:0:100000; `scale`: num:0:100; `side`: enum:left\|right | `image(src, {options}, …)` |
| 27 | `note` | inline | blocks | unsupported | — | `note(…)` |

Derived constructors (`stdlib.ctors`): `strong({options}, …)`, `em({options}, …)`, `style({options}, …)`, `figure(body, {options}, …)`, `toc({options}, …)` (nullary), `glossary({options}, …)` (nullary), `notes({options}, …)` (nullary), `bibliography(src, {options}, …)`, `node(kind, {options}, …)` (sealed). Std functions: `val`, `m`, `plain`.

| op | id |
|---|---|
| MAKE_TEXT | 1 |
| MAKE_NODE | 2 |
| EMIT | 3 |
| STYLE_PUSH | 4 |
| STYLE_POP_TO | 5 |
| SPAN | 6 |
| DIAG | 7 |
