<!-- GENERATED from engine/src/syntax/syntax.def by tools/gen-syntax.mjs — do not edit. -->
# Surface syntax (generated)

From `engine/src/syntax/syntax.def`, syntax version 4. Behaviour for each body mode, ownership class and block shape: `docs/syntax-design.md`.

## Inline delimiters

| id | open | close | body | params | guard | precedence | slot | capture |
|---|---|---|---|---|---|---|---|---|
| `code` | `` ` `` | `` ` `` | `Verbatim` | `pad=Strip` | `-` | `Island` | `code` | `string` |
| `math` | `$` | `$` | `Verbatim` | `esc=$ pad=Display` | `-` | `Island` | `math` | `embedded` |
| `comment` | `%--` | `--%` | `Verbatim` | `nest` | `-` | `Comment` | `comment` | `comment` |
| `splice` | `#HEAD` | `HEAD_CHAIN` | `CallChain` | `-` | `PrevIdent` | `Markup` | `splice` | `function` |
| `strong` | `*` | `*` | `Pair` | `-` | `Intraword` | `Markup` | `strong` | `strong` |
| `em` | `_` | `_` | `Pair` | `-` | `Intraword` | `Markup` | `em` | `emphasis` |
| `link` | `[` | `](URL)` | `LinkText` | `-` | `-` | `Markup` | `link` | `link` |
| `note` | `^[` | `]` | `Content` | `-` | `-` | `Markup` | `note` | `note` |
| `ref` | `@` | `BARE_ID` | `Ident` | `-` | `PrevIdent` | `Markup` | `ref` | `label` |
| `refs` | `@[` | `]` | `IdList` | `-` | `-` | `Markup` | `ref` | `label` |

## Blocks

| id | shape | starter | interrupts | ownership | slot |
|---|---|---|---|---|---|
| `quote` | `Prefix` | `>` | `Always` | `None` | `quote` |
| `item` | `Column` | `LIST_MARKER` | `ListRule` | `None` | `item` |
| `region` | `Explicit` | `#!NAME ARGS?` | `Always` | `Container` | `region` |
| `fence` | `Verbatim` | `` ``` INFO `` | `Always` | `Container` | `fence` |
| `comment` | `Verbatim` | `%--` | `LeafOwned` | `Container` | `comment` |
| `heading` | `Leaf` | `={1,6} TEXT <label>?` | `Always` | `None` | `heading` |
| `rule` | `Leaf` | `---` | `Always` | `None` | `rule` |
| `let` | `Stmt` | `#let` | `Always` | `Container` | `-` |
| `stmt` | `Stmt` | `#{` | `Always` | `Container` | `-` |
| `para` | `Leaf` | `DEFAULT` | `-` | `None` | `para` |

## Sugar slots

| slot | form | payload | dump |
|---|---|---|---|
| `para` | Block | — | `para` |
| `heading` | Block | level:u8 label:str | `heading level={level}{?label label="{label}"}` |
| `list` | Block | ordered:bool start:i32 | `list {ordered?ordered:bullet} start={start}` |
| `item` | Block | — | `item` |
| `quote` | Block | — | `quote` |
| `rule` | Block | — | `rule` |
| `fence` | Block | lang:str args:src bodyOffset:u32 bodyEnd:u32 lines:str info:str? label:str? | `codeblock lang="{lang}"{?info info="{info}"}{?label label="{label}"} body="{$str}"` |
| `region` | Block | args:src label:str? | `region name="{$str}"{?label label="{label}"}` |
| `strong` | Inline | — | `styled marker=*` |
| `em` | Inline | — | `styled marker=_` |
| `code` | Inline | — | `code str="{$str}"` |
| `link` | Inline | url:str | `link url="{url}"` |
| `note` | Inline | — | `note` |
| `ref` | Inline | — | `ref target="{$str}"` |
| `math` | Both | display:bool label:str | `math display={display} src="{$str}"{?label label="{label}"}` |
| `arg` | Inline | — | `arg` |
| `row` | Block | — | `row` |
| `cell` | Inline | — | `cell` |

## Character classes

- `SpliceHead`: `A-Za-z_$`
- `SpliceCont`: `A-Za-z0-9_$`
- `IdStart`: `A-Za-z_`
- `IdCont`: `A-Za-z0-9_`
- `IdJoin`: `-.:`
- `LabelChar`: `^ <>[]@,;\`
- `Escapable`: `` !-/:-@[-`{-~ ``

## Keywords and reserved splice heads

Keyword forms (P2-12): `if`, `for`, `while`. Not yet supported as heads: `if`, `else`, `for`, `while`, `use`, `let`. Reserved: `break`, `case`, `catch`, `class`, `const`, `continue`, `debugger`, `default`, `delete`, `do`, `export`, `extends`, `finally`, `function`, `import`, `in`, `instanceof`, `new`, `return`, `switch`, `throw`, `try`, `typeof`, `var`, `void`, `with`, `yield`, `static`, `enum`, `await`.

## Token tags

`keyword`, `string`, `number`, `comment`, `function`, `type`, `constant`, `variable`, `operator`, `punctuation`, `property`, `attribute`, `label`, `embedded`
