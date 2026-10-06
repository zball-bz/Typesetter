# 排版性 golden 变化审阅记录（PLAN.md §4.4）

每条记录写明：步骤、变化范围、逐段结论。机械性变化（只改数值格式、属性拼写）不在此列。

## P0-12 断行语义包（T6 S1）

**语义（同一提交落地）：** 断点上的 Glue 丢弃、行首（含段首）Glue/Penalty 丢弃到第一个 Box/Disc；BREAK_INF → Forbidden，不再是候选断点；连字符是 Disc（断开时只加 `pre`，不再同时计入接合字距）；段末是 Forced 断点，末行 fil 拉伸、正常收缩；代价 `min(mapped(x)^3, 1e4)`，Overfull 是独立类别；罚分 i32 千分位；平局按（demerits，行数，更晚的父节点）；最终一遍救援：所有活跃节点到某合法断点都 Overfull 时，按总序最优的活跃节点在此断开、不加 demerits，该行标 Overfull（布局把它放在收缩极限处，HTML 带 `data-overfull`，诊断 `overfull-line`）。断点下标取"下一行第一个块"（丢弃之后），所以丢弃后相同的行报告同一个（最晚的）位置，与布局裁剪后的范围一致。

**范围：** 95 个用例中 64 个 `breaks.txt` 变化（其中多数只有代价数值变化：旧代价计入了断点处的尾随空格、用 float 罚分），29 个用例共 34 个段落断点变化。WASM 与 native 断点逐字节一致（`tools/wasm-goldens.mjs --check`，95/95）。

### A. 计划列出的 12 段（11 个用例）——全部出现，结论：改进

| 用例 | 段 | 旧 → 新 | 结论 |
|---|---|---|---|
| cite/basic | pid 2 | 16,31,49,… → 14,28,49,… | 旧解为了把尾随空格算进宽度，在第 3 行留下 ws=16px 的极松行；新解三行 14.4/11/−1px，消除极松行 |
| cite/unknown-diag | pid 1 | 同上（同一条目） | 同上 |
| cjk/punct | pid 0 | 22,46,65,73 → 25,49,67,73 | 行首「的半宽不再计入（与布局裁剪一致）；行末收缩落在句读上：三行 ±0.48px，旧解首行 +3.9px |
| cjk/softwrap | pid 0 | 19,38,56 → 20,40,56 | 旧第 2 行 ws=3.1px；新解 −0.39/+0.85px，代价因语义变化（尾随字距不再计入）略高但最大松紧明显下降 |
| code/json-hl | pid 0 | 15,32,40 → 18,40 | 旧首行 ws=22.7px（灾难级）；新解首行以「，」结尾，半宽丢弃后恰好排满，2 行 |
| doc/refs | pid 6 | 17,35,50 → 19,39,50 | 4.4/4.6px → −1.4/1.1px |
| doc/refs | pid 10 | 18,27 → 20,27 | 改为连字符断开（sin-gle），松紧 4.4px → −1.4px |
| doc/refs-diag | pid 3 | 18,19 → 19 | 末行收缩：两行（末行只剩 "ing."）→ 一行（−0.23px） |
| inline/emph | pid 0 | 16,31 → 18,31 | 4.7px → −1.1px（在 *es-caped* 处断开） |
| inline/quotes | pid 0 | 16,32,50,51 → 18,36,51 | 4 行（末行 "same."）→ 3 行，最大 ws 3.8 → 1.3px |
| splice/ascii-cut | pid 0 | 17,18 → 18 | 末行收缩：孤字 "ary." 消失 |
| style/kern-boundary | pid 0 | 7 行 → 6 行 | 去掉尾随空格偏置后少一行；最大 ws 11px 不变（同一行），其余 ≤2.3px |

### B. 计划测量之后新增的用例里的同一段落

cite/group-unknown-diag pid 1、cite/in-note pid 2、cite/two-bibs pid 2 与 pid 3：都是 A 中同一条参考文献条目（P0-09 新增的用例），变化与 cite/basic 完全相同。

### C. P0-01…P0-10 新增的守护用例

| 用例 | 变化 | 结论 |
|---|---|---|
| doc/url-overlong | 1 行（ws=−154px，单词重叠）→ 4 行：正文一行、两条超长 URL 各占一行（Overfull，`data-overfull`，第二行收缩到极限 −95su）、末行 | 缺陷 #19 的预期救援；e2e AUDIT_XFAIL 清空 |
| code/snap, code/snap-sidecar | 孤立的 "#21)." 并回上一行；sidecar 段末行收缩 | 改进 |
| conform/appa-splices | 两处连字符断开代替 4.7px 松行 | 符合语义（罚分 0.7 < 松行代价） |
| doc/wrap-heading-caption | 标题 4 行 → 3 行；图注 3 行不变但断点后移；正文孤字 "1." 并回 | 改进 |
| exec/contain-fence-header-diag, line/block-unclosed-diag | 错误块文字改为连字符断开、去掉松行 | 改进 |
| inline/hyphen-link | 首行改在 exercis-es 处断开（8px/1px 代替 5.3px/8px） | 代价按新语义更低 |
| line/crlf | 在行内公式 `a +` 之后断开（mathBinAfterPenalty 0.95），代替 4.7px 松行 | 符合 TeX（二元运算符后可断）；两者代价相差 0.05，属配置的罚分取舍 |
| pages/paged-keep-fallback | 图注两行（孤词 "sheets."）→ 一行 | 末行收缩，改进 |

### D. 计划列表外、已存在的用例（逐个审阅）

| 用例 | 变化 | 结论 |
|---|---|---|
| figure/block pid 1、pid 2；figure/pull-diag pid 2 | 居中图注两行（末行只剩「取。」/「放。」/「框。」）→ 一行，收缩 −0.8px | 末行收缩的直接结果，消除孤字。计划的测量把这些居中图注算作"中性"，是因为它针对的是后续居中预设（LineEnds）下的行为；在 S1 的两端对齐语义下单行更优。偏差已记入 PROGRESS |
| notes/cjk-glue pid 1 | 列表项两行（末行「列。」）→ 一行（−1px） | 同上，改进 |
| math/parse-diag pid 0 | 两行（末行「断。」）→ 一行 | 计划要求审阅的 math/*：改进 |
| math/inline pid 1 | 在 `φ` 与 `=` 之间（关系符前，罚分 0.85）断开，代替 4.1px 松行；新行 ws=0.003px | 计划要求审阅的 math/*：符合 TeX 的关系符断行，罚分取舍同 line/crlf |

### E. 计划要求审阅、断点未变的用例

figure/float、figure/stack、region/table-tiny 的正文与图注、math/* 其余用例、全部表格单元格与浮动图注（cell 流）：断点不变（只有代价数值变化）。figure/pull-diag pid 1 的 [19,27] → 若按块下标取断点会变成 18，取"下一行第一个块"后保持 19（计划所说的"更晚父节点"平局）。

### 需要救援的用例

只有 doc/url-overlong（预期）与 region/hott-row（五栏表在 300px 下单元格 47px，公式单元格过宽，旧版同样溢出但整格塌成一行）。两者都给 `overfull-line` 警告，e2e 在 `EXPECTED_DIAGS` 中声明。

## P4-01 按 run 实例成 run（T5 步骤 4）

**变化：**
1. 接合字距（KernCtx）的条件从"同一 StyleId 且同一链接"改为"同一成形 run"：两侧是文字盒（Plain/Rigid），且两词与其间空格的 FaceStyle（字体由之决定的样式字段：族、角色、文字体系、字号、字重、拉丁斜体、特性、语言）相同。依据：Chromium 与 Firefox 实测（DejaVu Serif，"AV" 连写 56.0px、分开 58.0px），两者都跨 `<span>`、`<a>`、颜色边界成形与配对字距，字体变化、letter-spacing、inline-block 处不跨。
2. 行内代码为 Rigid：两端对齐的行上，含词分隔符的行内代码 run 写 `word-spacing:0`（其空格在盒内按原样测量）。
3. 锚点是点：带标签的引用（脚注标记）的第一个盒开启并携带锚点 run，后续同键项可并入；id 只在 run 起点写一次。

**范围：** 引擎 golden（mock 测量器的宽度可加，接合字距为 0）：69 个 hlist golden（178 处词间胶新增 kern 上下文，均在链接/引用/颜色边界两侧同字体处；36 个行内代码 run 由 plain 变为 rigid），1 个 html（locale/auto-en：`doc.lang: auto` 在两端对齐行上加 `word-spacing:0`）；breaks/blocks/layout 与 WASM 断点不变（213/213）。真实语料与用例 553 篇的排版 HTML：DOM 节点数 733,686 → 733,686（87K 基准文档 15,178 → 15,178，增长 0%，D-X08 门限 ≤10%）；75 篇有字节变化，全部只是 151 个行内代码 run 加 `word-spacing:0`。语义页（review-corpus）340 篇不变。

**审阅结论：改进。**
- 浏览器中，链接末字母与其后空格/逗号、引文编号之间的字距此前未计入，两端对齐行因此有亚像素到 1px 的右缘误差；e2e 审计 semantics/appendix（"Appendix A, …"）由此前的预期失败（P2-07 记入 AUDIT_XFAIL）变为通过，AUDIT_XFAIL 清空。
- 默认契约 CSS 中行内代码带 `tsr-pre`（`word-spacing: 0`），视觉不变；新属性让 Rigid 的实现不依赖主题给代码的类。
- 斜体标题与正体之间仍不计接合字距（字体不同，real-world-report #1 的修正保持）。
- 锚点规则在现有用例与语料中没有可见变化（带标签的引用都是单盒标记）。

## P4-02 段落级成形器（T5 步骤 5）

**变化：**
1. 段落上下文：一个单元的行内内容先展平成字素簇流，跨样式、链接、引用、错误边界；行内代码、公式记为拉丁类证据，图片、原始标记记为不透明。文字节点从段落中前一个字符的状态开始，不再从空白状态开始。因此 CJK–拉丁边界胶（0.25em，可断可伸）出现在每个文字体系边界：强调、链接、引用、行内代码两侧。例外有两处：上标或下标的标记（脚注序号）两侧不加；attach 边缘不加，因为此处的胶会成为 attach 禁止的断点。
2. 歧义标点按上下文解析：引号成对联合判定，任一侧的证据决定整对，宽标点算 CJK 证据；U+2019 夹在字母之间是撇号；破折号、省略号的判定也读跨节点的邻居。
3. 对象之后的断点看后邻：后面是闭合标点（CJK，或 `, . ; : ! ? ) ] } %` 与引号）时禁断；公式紧贴拉丁文字或代码时禁断（AL × AL、AL × OP）。
4. 闭合 CJK 标点之前的键入空格不可断（UAX #14 LB13）。
5. 软换行在规范形之后、按段落上下文以 joinsWithoutSpace 解析，可跨节点（`这是*强调*⏎中文` 无缝）。
6. 长 token 的门限按字符而不是字节计数，并跨样式边界计算。
7. CJK 盒按字素簇划分。

**范围：** 引擎 golden 共 57 个文件：16 个 hlist、10 个 breaks/layout/html、8 个 blocks、tree/semantic/paged 各 1 个。
- 有排版变化的用例：figure/block、figure/float、math/eqref、pages/paged-doc、inline/prose-guards、locale/quote-lang、splice/ascii-cut（引用、URL、lang 引语、拼接结果与 CJK 之间加边界胶）。
- math/grid、math/symbols：公式后不再于 `,`、`.` 前断行。旧版 math/symbols 第二行以 ", and" 开头，现改在公式内的关系符后断，第二行为 "b, and"。
- style/patch：`、⏎#style(…)[楷体片段]` 处软换行无缝；tree、semantic 也随之去掉多余空格。
- 只有 hlist 变化（去掉公式后的 pen 0）：doc/toc-clone、inline/bracket-island、math/decl、math/holes-diag、math/negation-diag、notes/basic。
- 新用例 cjk/cross-node 逐项覆盖以上情形。WASM 断点一致（214/214）。

真实语料 340 篇（mock 测量）：
- 171 篇 hlist 有变化，116 篇断点有变化（多为 pbr-en 公式后的标点）；
- 边界胶 2,658 → 4,015：49 篇增加，没有一篇减少（个别处减少是错判为拉丁的引号改判为 CJK 标点，例如 `！”——`，同篇他处有增加）；
- 语义页 340 篇不变；
- typst 语料 199 篇全部通过新增的 HList lint。旧版在两篇 `！ ？` 文档中有闭合标点前的断点。

**审阅结论：改进。** 抽查博客语料：引用 `(1)` 后接"所"、`.tsm` 后接 `——`、链接 `§1` 两侧，均为应有的中西间距与断点。e2e 审计矩阵 1091 项通过（right-edge、line-integrity、overflow、copy）。

## P4-03 逐项源 span（T5 步骤 6）

**机械性变化，无排版变化。** 751 个 golden 文件（blocks 210、hlist 210、html 165、layout 154、paged 12）。去掉 span 字段（`@[s,e)`、`data-s`/`data-e` 的值、`data-s0`）后，与 P4-02 逐字节相同。其中 `data-s` 的有无有三处变化：
- 连字符 run 不再带 `data-s`：连字符是合成的点；
- 只有解析器插入的空格的 run 不再带 `data-s`；
- 以 run-in 空格开头的 run 带上了 `data-s`：取其第一个有源的项。

tools/check-spans.mjs 检查全部 213 个 html golden 的 2,835 个 run，结果为 0：
- 每个内容 run 的 `data-s` 指向其首字符；
- 同行 run 按源顺序；
- 同一流的各行按源顺序。

同一脚本用在 P4-02 的 golden 上报 396 处，即旧版 run 都取文字节点起点。语义页 340 篇不变；e2e 新增一项，验证 run 级 offsetAt 与行级 elementsAt。

## P3-36 博客（zball-io）需要的配合改动（MD-07：本计划不修改博客仓库）

重新 vendor 引擎（`scripts/fetch-engine.mjs --local`）后，博客侧建议做如下改动；未改之前现有用法仍可工作（`renderTsm` 的旧字段都保留）。

1. 水合：`eleventy.config.js` 手写的引擎选项（`fontFamily`、`cjkFontFamily`、`lang`、`paraIndentEm`、`fonts`）改为站点设置文档（`fonts.*`、`par.indent` 等，见 docs/settings-table.md）传给 `renderTsm(src, {settings})`，客户端 `createEngine().typeset(…, {settings: bundle.settings 去掉 host 行})`；`exportStatic` 生成的水合脚本即如此。
2. 语言：删去正则读取 front matter 的 `lang`。文档语言来自 `bundle.docinfo.lang`（文档自己的 `$.doc({lang})`、宿主设置 `doc.lang` 或自动检测，P3-30）。若把含 front matter 的原文交给引擎，设置 `source.frontMatter: true`（P3-35）。
3. CSS：不再自行 import `TSR_CSS` 并复制引擎样式；用 `bundle.styles.contract` / `theme` / `rules`，或 `exportStatic` 的 `parts.head`。
4. 页面与订阅：文章片段可由 `exportStatic(bundle, {template: (p) => …})` 生成；RSS 用 `renderTsm(src, {profile: 'feed'})`（公式为源码，无水合）。
5. 双语：每种语言一个引擎实例不再必要——一个 `createEngine()` 按容器管理多个会话；各语言用各自 bundle 的 settings。
6. 资源：`bundle.resources`（清单）列出图片、`#bibliography`、`$.load` 文件；用 `exportStatic` 的 `copy` 列表或 `embedResources`。
7. 内容：`src/docs/example-hott.tsm` 与 `example-huozi.tsm` 需从本仓库 `examples/real-world` 重新复制（P3-33 起 `\x` 保留反斜杠、URL 自动链接，P3-35 的转换器修正）；其余语法变化见 docs/tsm-changes.md 的 P3-29…P3-35 条目（`&` 为公式对齐点、`/ 术语: 描述`、`*`/`_` 词内不成对等）。
