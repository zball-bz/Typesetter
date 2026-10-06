<!-- GENERATED from engine/schema/schema.json by tools/gen-schema.mjs — do not edit. -->
# Ops vocabulary (generated)

The kind table of document-model §2.1, generated from `engine/schema/schema.json`. Ops version 10, min compat 6.

| id | kind | level | body | inline | attributes (writer order: domain) | constructor |
|---|---|---|---|---|---|---|
| 0 | `doc` | block | blocks | unsupported | — | — |
| 1 | `para` | block | inline | container | `label`: label; `role`: ident; `slot`: ident; `syn`: ident; `copy`: copy; `class`: classlist; `ext`: ext | `para({options}, …)` |
| 2 | `heading` | block | inline | unsupported | `level`: int:1:6; `label`: label; `role`: ident; `slot`: ident; `syn`: ident; `copy`: copy; `class`: classlist; `ext`: ext | `heading(level, label, {options}, …)` |
| 3 | `list` | block | items | unsupported | `ordered`: bool; `start`: int:-1073741824:1073741824; `label`: label; `role`: ident; `slot`: ident; `syn`: ident; `copy`: copy; `class`: classlist; `ext`: ext | `list(ordered, start, {options}, …)` |
| 4 | `item` | block | blocks | unsupported | `label`: label; `role`: ident; `slot`: ident; `syn`: ident; `copy`: copy; `class`: classlist; `ext`: ext | `item({options}, …)` |
| 5 | `quote` | block | blocks | unsupported | `label`: label; `role`: ident; `slot`: ident; `syn`: ident; `copy`: copy; `class`: classlist; `ext`: ext | `quote({options}, …)` |
| 6 | `codeblock` | block | code | unsupported | `lang`: token; `wrap`: bool; `lineNo`: int:0:1048576; `hl`: rangeset; `sidecar`: str; `label`: label; `role`: ident; `slot`: ident; `syn`: ident; `copy`: copy; `class`: classlist; `ext`: ext | `codeblock(lang, lines, {options}, …)` |
| 7 | `rule` | block | none | unsupported | `label`: label; `role`: ident; `slot`: ident; `syn`: ident; `copy`: copy; `class`: classlist; `ext`: ext | `rule({options}, …)` (nullary) |
| 8 | `group` | adaptive | position | container | `role`: ident; `label`: label; `name`: str; `slot`: ident; `syn`: ident; `copy`: copy; `class`: classlist; `ext`: ext | `group({options}, …)` |
| 9 | `table` | block | rows | unsupported | `cols`: int:1:64; `align`: token; `label`: label; `role`: ident; `slot`: ident; `syn`: ident; `copy`: copy; `class`: classlist; `ext`: ext | `table(body, {options}, …)` |
| 10 | `trow` | block | cells | unsupported | `label`: label; `role`: ident; `slot`: ident; `syn`: ident; `copy`: copy; `class`: classlist; `ext`: ext | `row({options}, …)` |
| 11 | `tcell` | block | inline | unsupported | `label`: label; `role`: ident; `slot`: ident; `syn`: ident; `copy`: copy; `class`: classlist; `ext`: ext | `cell({options}, …)` |
| 12 | `term` | adaptive | inline | unsupported | `name`: str; `label`: label; `role`: ident; `slot`: ident; `syn`: ident; `copy`: copy; `class`: classlist; `ext`: ext | `term(name, {options}, …)` |
| 13 | `collect` | block | data | unsupported | `what`: ident; `form`: enum:all; `cited`: enum:cited\|cited-then-all; `label`: label; `role`: ident; `slot`: ident; `syn`: ident; `copy`: copy; `class`: classlist; `ext`: ext | `collect({options}, …)` |
| 14 | `mathblock` | block | none | unsupported | `src`: str; `label`: label; `name`: str; `role`: ident; `slot`: ident; `syn`: ident; `copy`: copy; `class`: classlist; `ext`: ext | `mathblock(src, label, {options}, …)` |
| 15 | `error` | adaptive | none | error | `message`: str; `code`: ident; `label`: label; `role`: ident; `slot`: ident; `syn`: ident; `copy`: copy; `class`: classlist; `ext`: ext | `error(code, message, {options}, …)` (sealed) |
| 16 | `comment` | trivia | text | skip | `label`: label; `role`: ident; `slot`: ident; `syn`: ident; `copy`: copy; `class`: classlist; `ext`: ext | `comment(text, {options}, …)` |
| 17 | `text` | inline | none | text | — | `text(text, …)` (sealed) |
| 18 | `styled` | transparent | position | container | `bits`: flags:EM=2,BOLD=3,UNDER=16,OVER=17,STRIKE=18; `font`: font; `lang`: lang; `color`: color; `sizePx`: num:1:2000; `label`: label; `role`: ident; `slot`: ident; `syn`: ident; `copy`: copy; `class`: classlist; `ext`: ext | `styled({options}, …)` |
| 19 | `link` | inline | inline | container | `url`: url; `label`: label; `role`: ident; `slot`: ident; `syn`: ident; `copy`: copy; `class`: classlist; `ext`: ext | `link(url, {options}, …)` |
| 20 | `code` | inline | text | code | `label`: label; `role`: ident; `slot`: ident; `syn`: ident; `copy`: copy; `class`: classlist; `ext`: ext | `code(text, {options}, …)` |
| 21 | `ref` | inline | none | container | `target`: str; `url`: url; `form`: ident; `supplement`: str; `label`: label; `role`: ident; `slot`: ident; `syn`: ident; `copy`: copy; `class`: classlist; `ext`: ext | `ref(target, {options}, …)` |
| 22 | `mathinline` | inline | none | object | `src`: str; `label`: label; `role`: ident; `slot`: ident; `syn`: ident; `copy`: copy; `class`: classlist; `ext`: ext | `mathinline(src, {options}, …)` |
| 23 | `raw` | block | none | object | `html`: html; `w`: num:0:100000; `h`: num:0:100000; `label`: label; `role`: ident; `slot`: ident; `syn`: ident; `copy`: copy; `class`: classlist; `ext`: ext | `raw(html, {options}, …)` |
| 24 | `hardbreak` | inline | none | break | `label`: label; `role`: ident; `slot`: ident; `syn`: ident; `copy`: copy; `class`: classlist; `ext`: ext | — |
| 25 | `seq` | transparent | position | container | `label`: label; `role`: ident; `slot`: ident; `syn`: ident; `copy`: copy; `class`: classlist; `ext`: ext | `seq({options}, …)` (sealed) |
| 26 | `image` | block | none | object | `src`: url; `alt`: str; `w`: num:0:100000; `h`: num:0:100000; `scale`: num:0:100; `side`: enum:left\|right; `label`: label; `role`: ident; `slot`: ident; `syn`: ident; `copy`: copy; `class`: classlist; `ext`: ext | `image(src, {options}, …)` |
| 27 | `note` | inline | blocks | unsupported | `label`: label; `role`: ident; `slot`: ident; `syn`: ident; `copy`: copy; `class`: classlist; `ext`: ext | `note({options}, …)` |
| 28 | `field` | inline | none | skip | `name`: str; `of`: str; `label`: label; `role`: ident; `slot`: ident; `syn`: ident; `copy`: copy; `class`: classlist; `ext`: ext | `field(name, {options}, …)` |
| 29 | `event` | trivia | none | skip | `counter`: ident; `set`: intlist; `step`: int:1:16; `add`: int:-1073741824:1073741824; `numbering`: str; `supplement`: str; `label`: label; `role`: ident; `slot`: ident; `syn`: ident; `copy`: copy; `class`: classlist; `ext`: ext | — |
| 30 | `entry` | trivia | inline | skip | `key`: str; `label`: label; `role`: ident; `slot`: ident; `syn`: ident; `copy`: copy; `class`: classlist; `ext`: ext | `entry({options}, …)` |
| 31 | `slot` | inline | none | skip | `name`: str; `or`: str; `label`: label; `role`: ident; `slot`: ident; `syn`: ident; `copy`: copy; `class`: classlist; `ext`: ext | `slot(name, {options}, …)` |
| 32 | `when` | transparent | position | container | `of`: str; `label`: label; `role`: ident; `slot`: ident; `syn`: ident; `copy`: copy; `class`: classlist; `ext`: ext | `when(of, {options}, …)` |
| 33 | `each` | transparent | position | container | `of`: str; `sep`: str; `label`: label; `role`: ident; `slot`: ident; `syn`: ident; `copy`: copy; `class`: classlist; `ext`: ext | `each(of, {options}, …)` |

Derived constructors (`stdlib.ctors`): `strong({options}, …)`, `em({options}, …)`, `style({options}, …)`, `figure(body, {options}, …)`, `toc({options}, …)` (nullary), `glossary({options}, …)` (nullary), `notes({options}, …)` (nullary), `bibliography(src, {options}, …)`, `counterUpdate(counter, {options}, …)`, `node(kind, {options}, …)` (sealed). Std functions: `val`, `m`, `plain`.

| op | id |
|---|---|
| MAKE_TEXT | 1 |
| MAKE_NODE | 2 |
| EMIT | 3 |
| STYLE_PUSH | 4 |
| STYLE_POP_TO | 5 |
| SPAN | 6 |
| DIAG | 7 |
| AT | 8 |
| RAWMAP | 9 |
| DECL | 10 |
