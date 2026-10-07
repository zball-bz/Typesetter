<!-- GENERATED from skills/tsm.src.md and the engine's tables by tools/gen-skill.mjs — do not edit; edit the source and run node tools/gen-all.mjs -->
# .tsm reference tables

Generated from the engine's schema and math tables; the syntax itself is SKILL.md.

## Constructors

Leading arguments bind to the parameters while they fit, then one options object, then the content (`#name(params…, {options})[content]`). Every constructor also takes the universal options `label`, `role`, `slot`, `syn`, `copy`, `class`, `ext`, `attach` and `style: {…}`.

| constructor | parameters | options |
|---|---|---|
| `para()` | — | — |
| `heading(level, label)` | `level`, `label` | — |
| `list(ordered, start)` | `ordered`, `start` | `numbering` string |
| `item()` | — | — |
| `quote()` | — | — |
| `codeblock(lang, lines)` | `lang`, `lines` | `wrap` true / false; `lineNo` integer 0–1048576; `hl` "1,3-5"; `sidecar` string; `snapKerning` true / false; `sidecarFrac` number 0.1–0.9; `contIndent` integer 0–40; `features` font features; `overlays` list of names |
| `rule` (bare: `#rule`) | — | — |
| `group()` | — | `role` name; `label` label; `name` string; `kind` name |
| `table(content)` | `content` | `cols` integer 1–64; `align` string; `label` label; `tracks` column list; `rules` "grid" / "booktabs" / "none"; `header` integer 0–64 |
| `row()` | — | — |
| `cell()` | — | `colspan` integer 1–64; `rowspan` integer 1–1000; `align` "l" / "c" / "r"; `valign` "top" / "middle" / "bottom" |
| `term(name)` | `name` | — |
| `collect()` | — | `what` name; `form` "all"; `cited` "cited" / "cited-then-all" |
| `mathblock(src, label)` | `src`, `label` | — |
| `comment(text)` | `text` | — |
| `styled()` | — | a style patch (Style keys) |
| `link(url)` | `url` | `target` string; `to` name |
| `code(text)` | `text` | — |
| `ref(target)` | `target` | `form` name; `supplement` string |
| `mathinline(src)` | `src` | — |
| `raw(html)` | `html` | `w` number 0–100000; `h` number 0–100000; `measure` "declared" / "host"; `minWidth` length |
| `image(src)` | `src` | `alt` string; `w` number 0–100000; `h` number 0–100000; `scale` number 0–100; `side` "left" / "right" |
| `note()` | — | — |
| `field(name)` | `name` | `of` string |
| `entry()` | — | `key` string; `sortKey` string |
| `slot(name)` | `name` | `or` string |
| `when(of)` | `of` | — |
| `each(of)` | `of` | `sep` string |
| `math()` | — | `display` true / false |
| `equations()` | — | — |
| `fill` (bare: `#fill`) | — | — |
| `terms()` | — | — |
| `strong()` | — | — |
| `em()` | — | — |
| `style()` | — | a style patch (Style keys) |
| `figure(content)` | `content` | any (passed to the implementation) |
| `toc` (bare: `#toc`) | — | — |
| `glossary` (bare: `#glossary`) | — | — |
| `lof` (bare: `#lof`) | — | — |
| `lot` (bare: `#lot`) | — | — |
| `index` (bare: `#index`) | — | — |
| `notes` (bare: `#notes`) | — | — |
| `pagebreak` (bare: `#pagebreak`) | — | — |
| `linebreak` (bare: `#linebreak`) | — | — |
| `attach(attach)` | `attach` | — |
| `bibliography(src)` | `src` | any (passed to the implementation) |
| `counterUpdate(counter)` | `counter` | any (passed to the implementation) |
| `node(kind)` | `kind` | any (passed to the implementation) |

Functions: `val`, `m`, `plain`, `use` (`use` returns a promise; a splice of it awaits).

## Style keys

A dotted key nests: `par.indent` is written `{par: {indent: …}}`. Lengths are CSS lengths; a bare number is em.

| key | value |
|---|---|
| `weight` | integer 100–900 |
| `italic` | true / false |
| `decoration` | "under" / "over" / "strike" or a list of them |
| `fontRole` | "body" / "mono" |
| `baseline` | "super" / "sub" |
| `code.hang` | "indent" / "content" |
| `size` | length or percent |
| `font` | font list |
| `lang` | BCP-47 tag |
| `color` | colour |
| `sizePx` | number 1–2000 |
| `par.indent` | length |
| `par.align` | "justify" / "start" / "center" / "end" |
| `par.hyphenate` | "auto" / "true" / "false" |
| `par.singleLine` | "align" / "center" |
| `block.gap` | length |
| `block.indent` | length |
| `block.keepWithNext` | true / false |
| `list.marker` | string |
| `codeblock.snapKerning` | true / false |
| `codeblock.sidecarFrac` | number 0.1–0.9 |
| `codeblock.contIndent` | integer 0–40 |
| `codeblock.overlays` | list of names |
| `keep` | "together" / "with-next" / "both" |
| `space.before` | length |
| `space.after` | length |
| `break.before` | "auto" / "page" |
| `break.after` | "auto" / "page" |
| `par.hang` | length |
| `par.hangAfter` | integer 0–100 |
| `box.padding` | 1–4 lengths (CSS order) |
| `box.border` | 1–4 lengths (CSS order) |
| `box.borderColor` | colour |
| `box.background` | colour |
| `media` | "all" / "screen" / "paged" |
| `beside` | "clear" / "shrink" |
| `breaker.tolerance` | number 0–100000 |
| `breaker.emergencyStretch` | length |
| `place.float` | "none" / "left" / "right" / "top" / "bottom" / "page" / "inline" |
| `place.width` | length or percent |
| `place.gap` | length |
| `text.features` | font features |
| `text.punct` | "full" / "book" / "none" |
| `text.space` | "normal" / "pre" |
| `text.wrap` | "wrap" / "nowrap" |
| `text.autospace` | "none" / "normal" |
| `text.hyphens` | "none" / "manual" / "auto" |
| `text.overflowWrap` | "normal" / "separators" / "anywhere" |

Sugar: `bold` is `weight: 700`, `italic` is `italic: true`, `underline` is `decoration: "under"`, `overline` is `decoration: "over"`, `strike` is `decoration: "strike"`.

## Math functions

A call binds only on a `(` directly after the name; without it the name means its bare symbol, else it is an upright name.

| call | bare name means |
|---|---|
| `sqrt(x)` | — |
| `root(n, x)` | — |
| `abs(x)` | — |
| `norm(x)` | — |
| `floor(x)` | — |
| `ceil(x)` | — |
| `binom(n, k)` | — |
| `overline(x)` | — |
| `underline(x)` | — |
| `bar(x)` | — |
| `hat(x)` | ˆ |
| `tilde(x)` | ˜ |
| `vec(x)` | — |
| `dot(x)` | cdot |
| `ddot(x)` | ¨ |
| `breve(x)` | ˘ |
| `check(x)` | ˇ |
| `ring(x)` | ˚ |
| `acute(x)` | ´ |
| `grave(x)` | ` |
| `thin` | — |
| `med` | — |
| `thick` | — |
| `quad` | — |
| `wide` | — |
| `display(x)` | — |
| `inline(x)` | — |
| `script(x)` | — |
| `sscript(x)` | — |
| `limits(x)` | — |
| `scripts(x)` | — |
| `bb(x)` | — |
| `cal(x)` | — |
| `frak(x)` | — |
| `bold(x)` | — |
| `italic(x)` | — |
| `sans(x)` | — |
| `mono(x)` | — |
| `mat(r: cells)` | — |
| `pmat(r: cells)` | — |
| `bmat(r: cells)` | — |
| `Bmat(r: cells)` | — |
| `vmat(r: cells)` | — |
| `Vmat(r: cells)` | — |
| `cases(r: rows)` | — |
| `aligned(r: rows)` | — |
| `overbrace(x, t?)` | — |
| `underbrace(x, b?)` | — |
| `overbracket(x, t?)` | — |
| `underbracket(x, b?)` | — |
| `overparen(x, t?)` | — |
| `underparen(x, b?)` | — |
| `big(d: sym)` | — |
| `Big(d: sym)` | — |
| `bigg(d: sym)` | — |
| `Bigg(d: sym)` | — |
| `hphantom(x)` | — |
| `vphantom(x)` | — |
| `smash(x)` | — |

Primitives callable by name: `frac(a, b)`, `attach(x, t: …, b: …, tl: …, tr: …, bl: …, br: …)`, `class(rel, …)`, `lr(…)`.

## Math symbols

Name → symbol, by spacing class. A typed symbol behaves as its name.

- **bin**: `*` ∗, `+` +, `+-` ±, `-` −, `-+` ∓, `amalg` ⨿, `ast` ∗, `bullet` ∙, `cap` ∩, `cdot` ⋅, `circ` ∘, `cup` ∪, `div` ÷, `mp` ∓, `odot` ⊙, `ominus` ⊖, `oplus` ⊕, `oslash` ⊘, `otimes` ⊗, `ox` ⊗, `pm` ±, `setminus` ∖, `sqcap` ⊓, `sqcup` ⊔, `star` ⋆, `times` ×, `uplus` ⊎, `vee` ∨, `wedge` ∧, `xx` ×
- **close**: `rangle` ⟩, `rceil` ⌉, `rfloor` ⌋
- **op (large)**: `bigcap` ⋂, `bigcup` ⋃, `bigodot` ⨀, `bigoplus` ⨁, `bigotimes` ⨂, `bigsqcup` ⨆, `biguplus` ⨄, `bigvee` ⋁, `bigwedge` ⋀, `coprod` ∐, `iiint` ∭, `iint` ∬, `int` ∫, `oint` ∮, `prod` ∏, `sum` ∑
- **open**: `langle` ⟨, `lceil` ⌈, `lfloor` ⌊
- **operator name (upright)**: `Pr`, `arccos`, `arcsin`, `arctan`, `arg`, `argmax`, `argmin`, `cos`, `cosh`, `cot`, `coth`, `csc`, `deg`, `det`, `dim`, `exp`, `gcd`, `hom`, `inf`, `ker`, `lg`, `lim`, `liminf`, `limsup`, `ln`, `log`, `max`, `min`, `mod`, `sec`, `sin`, `sinh`, `sup`, `tan`, `tanh`
- **ord**: `...` …, `:.` ∴, `AA` 𝔸, `BB` 𝔹, `CC` ℂ, `DD` 𝔻, `Delta` Δ, `EE` 𝔼, `FF` 𝔽, `GG` 𝔾, `Gamma` Γ, `HH` ℍ, `II` 𝕀, `Im` ℑ, `JJ` 𝕁, `KK` 𝕂, `LL` 𝕃, `Lambda` Λ, `MM` 𝕄, `NN` ℕ, `OO` 𝕆, `Omega` Ω, `PP` ℙ, `Phi` Φ, `Pi` Π, `Psi` Ψ, `QQ` ℚ, `RR` ℝ, `Re` ℜ, `SS` 𝕊, `Sigma` Σ, `TT` 𝕋, `Theta` Θ, `UU` 𝕌, `Upsilon` Υ, `VV` 𝕍, `WW` 𝕎, `XX` 𝕏, `Xi` Ξ, `YY` 𝕐, `ZZ` ℤ, `aleph` ℵ, `alpha` α, `angle` ∠, `because` ∵, `beta` β, `bot` ⊥, `cdots` ⋯, `chi` χ, `ddots` ⋱, `degree` °, `delta` δ, `ell` ℓ, `empty` ∅, `emptyset` ∅, `epsilon` ε, `eta` η, `exists` ∃, `forall` ∀, `gamma` γ, `grad` ∇, `hbar` ℏ, `infinity` ∞, `infty` ∞, `iota` ι, `kappa` κ, `lambda` λ, `ldots` …, `mu` μ, `nabla` ∇, `neg` ¬, `nexists` ∄, `not` ¬, `nu` ν, `omega` ω, `omicron` ο, `oo` ∞, `partial` ∂, `phi` φ, `pi` π, `prime` ′, `psi` ψ, `rho` ρ, `sigma` σ, `square` □, `tau` τ, `therefore` ∴, `theta` θ, `top` ⊤, `triangle` △, `upsilon` υ, `varepsilon` ϵ, `varphi` ϕ, `varpi` ϖ, `vartheta` ϑ, `vdots` ⋮, `wp` ℘, `xi` ξ, `zeta` ζ, `|` |, `||` ‖, `varrho` ϱ, `varsigma` ς
- **punct**: `,` ,, `;` ;
- **rel**: `-->` ⟶, `--|` ⊣, `-=` ≡, `->` →, `:` ∶, `:=` ≔, `<` <, `<-` ←, `<--` ⟵, `<->` ↔, `<<` ≪, `<=` ≤, `<==` ⟸, `<=>` ⇔, `=` =, `=:` ≕, `==>` ⟹, `=>` ⇒, `>` >, `>=` ≥, `>>` ≫, `Downarrow` ⇓, `Uparrow` ⇑, `Updownarrow` ⇕, `approx` ≈, `cong` ≅, `dashv` ⊣, `downarrow` ↓, `equiv` ≡, `geq` ≥, `gets` ←, `gg` ≫, `hookleftarrow` ↩, `hookrightarrow` ↪, `in` ∈, `leftharpoonup` ↼, `leftrightarrows` ⇆, `leq` ≤, `ll` ≪, `mapsto` ↦, `mid` ∣, `models` ⊨, `nearrow` ↗, `neq` ≠, `ni` ∋, `notin` ∉, `nwarrow` ↖, `parallel` ∥, `perp` ⊥, `prec` ≺, `preceq` ⪯, `prop` ∝, `propto` ∝, `rightharpoonup` ⇀, `rightleftharpoons` ⇌, `searrow` ↘, `sim` ∼, `simeq` ≃, `sqsubseteq` ⊑, `sqsupseteq` ⊒, `subset` ⊂, `subseteq` ⊆, `succ` ≻, `succeq` ⪰, `supset` ⊃, `supseteq` ⊇, `swarrow` ↙, `to` →, `uparrow` ↑, `updownarrow` ↕, `vdash` ⊢, `|-` ⊢, `|--` ⊢, `|-->` ⟼, `|->` ↦, `|=` ⊨, `|==` ⊨, `~` ∼, `~=` ≅, `~>` ⇝, `~~` ≈, `::=` ⩴
