<!-- GENERATED from engine/schema/schema.json by tools/gen-schema.mjs — do not edit. -->
# Ops vocabulary (generated)

The kind table of document-model §2.1, generated from `engine/schema/schema.json`. Ops version 6, min compat 6.

| id | kind | level | body | attributes (writer order: domain) |
|---|---|---|---|---|
| 0 | `doc` | block | blocks | — |
| 1 | `para` | block | inline | — |
| 2 | `heading` | block | inline | `level`: int:1:6; `label`: label |
| 3 | `list` | block | items | `ordered`: bool; `start`: int:-1073741824:1073741824 |
| 4 | `item` | block | blocks | — |
| 5 | `quote` | block | blocks | — |
| 6 | `codeblock` | block | code | `lang`: token; `wrap`: bool; `lineNo`: int:0:1048576; `hl`: rangeset; `sidecar`: str |
| 7 | `rule` | block | none | — |
| 8 | `group` | adaptive | position | `role`: ident; `label`: label; `name`: str |
| 9 | `table` | block | rows | `cols`: int:1:64; `align`: token; `label`: label |
| 10 | `trow` | block | cells | — |
| 11 | `tcell` | block | inline | — |
| 12 | `term` | adaptive | inline | `name`: str |
| 13 | `collect` | block | data | `what`: enum:toc\|glossary\|notes\|bibliography; `form`: enum:all |
| 14 | `mathblock` | block | none | `src`: str; `label`: label |
| 15 | `error` | adaptive | none | `message`: str; `code`: ident |
| 16 | `comment` | trivia | text | — |
| 17 | `text` | inline | none | — |
| 18 | `styled` | transparent | position | `bits`: flags:EM=2,BOLD=3,UNDER=16,OVER=17,STRIKE=18; `font`: font; `lang`: lang; `color`: color; `sizePx`: num:1:2000 |
| 19 | `link` | inline | inline | `url`: url |
| 20 | `code` | inline | text | — |
| 21 | `ref` | inline | none | `target`: str |
| 22 | `mathinline` | inline | none | `src`: str |
| 23 | `raw` | block | none | `html`: html; `w`: num:0:100000; `h`: num:0:100000 |
| 24 | `hardbreak` | inline | none | — |
| 25 | `seq` | transparent | position | — |
| 26 | `image` | block | none | `src`: url; `alt`: str; `w`: num:0:100000; `h`: num:0:100000; `scale`: num:0:100; `side`: enum:left\|right |
| 27 | `note` | inline | blocks | — |

| op | id |
|---|---|
| MAKE_TEXT | 1 |
| MAKE_NODE | 2 |
| EMIT | 3 |
| STYLE_PUSH | 4 |
| STYLE_POP_TO | 5 |
| SPAN | 6 |
