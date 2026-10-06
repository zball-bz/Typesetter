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

## P4-04 TextProps v1；标点、空白、autospace 数据化（T5 步骤 7）

**前置：** 先建标点矩阵 e2e（test/e2e/punct.spec.mjs）和设置 render.runWidths（每个 run 写出引擎设定的宽度 data-w）。矩阵覆盖 11 类组合 × 3 种挤压模式，两端对齐行与末行都测，在 4 个 dsf 下比较渲染宽度与 data-w。用改动前的代码录下基线 punct-baseline.json：最大偏差 0.016px。改动后最大偏差仍是 0.016px，各项均不变。

**变化：**
1. **标点空白数据化。** compat.def 的 BLANK 行给出各类的前后空白，压缩规则写成一条：两段空白相遇时，none 两段都保留，book 保留一段（前者的，二者之间可断），full 都不保留。开括号之后再跟开括号时：book、full 实排；none 保留空白但不可断。闭合后再跟闭合时：book、full 去掉前一段空白；none 保留但不可断。这与旧的分支逐例相同。
2. **挤压与上标抬升改为显式 px。** 字形一侧没有自己的空白时，用 margin px 挤压，取代 tsr-sqL/R 类；上标抬升改为 top px（superRaiseEm × 本 run 的 em），取代 CSS 的 -0.45em。契约 CSS 不再含引擎数值。
3. **定义宽度数据化。** ADVANCE 行（—— 2em、— 1em、…… 2em、… 1em）取代代码中对 U+2014/U+2026 的特判。
4. **标点字形加 ε。** 测量宽度减去空白后再加 ε（与词间空格相同）。
5. **新文字属性。**
   - `text.wrap`（nowrap：内部不可断）；
   - `text.autospace`（none：两侧不加边界胶）；
   - `text.hyphens` 与 `text.overflowWrap`（覆盖块的规则，P4-06 接手）；
   - `text.space: pre`（空格保留在刚性盒内）。
   opsVersion 15→16，现有录制不变。
6. **拼接字符串中的连续空格折叠为一个**，与浏览器一致。旧版按多个空格预算，两端对齐行因此偏短。

**范围：** 211 个 golden 文件：
- hlist 60、blocks 60：标点字形宽 +1su ε；
- breaks 32：只有代价数值，断点全部不变；
- html 57、paged 2：tsr-sqL/R 改为 margin px（102 处右侧、11 处左侧），上标加 top px；
- layout 不变。

新用例 style/text-props 覆盖五个属性（overflowWrap normal 的 URL 有意溢出，产生 overfull-line）。rules-diff 0；语义页 340 篇不变。

**审阅结论：等价实现，并修正一处缺陷。** 渲染与预测的偏差没有变化（≤0.016px）；拼接空格的预算修正属于改进。

## P4-05 UCD 字符类（RULES_VERSION 1）与 Unicode 控制符（T5 步骤 8）

**变化：**
1. **默认规则改为 RULES_VERSION 1。** 由 engine/rules/locale/default.def 编译，链为 und ← en ← zh-Hans：
   - und 由 UAX #14 派生字母、数字、窄标点、谚文和控制符；
   - zh-Hans 保留 compat 的宽区间与 clreq 标点，再补上新覆盖。
   tools/rules-diff 按行为投影（引擎读取的列、空白、歧义/控制类）比较。test/golden/RULES 记录允许的变化：60 段，另加"任意码位仅字距列"。--check 通过；unitTextRules 把清单外的每个码位钉在 compat 的字面谓词上。
2. **禁则（nostart 列）。** 闭合与句读、小假名、迭代记号、ー、〜、・不在行首：它们之前不断，经过空格或边界胶也不断；・的前空白不可断。代码网格的折行与 lintHList 读同一列；typst 语料 199 篇过 lint。
3. **谚文。** 作为 CJK 盒，音节间可断；不加中西间距；作为歧义标点的证据时不算 CJK；软换行读作空格。U+3000 是 CJK 盒，不加间距。
4. **纯文本断行控制符。**
   - NBSP、U+2007：可伸展、不可断、不折叠；
   - NNBSP：留在词内；
   - ZWSP：只是断点；
   - WJ、BOM：前后都不断；
   - SHY：词的唯一连字点，断开时出连字符。
5. **字距资格按类判定**（kern 列），弯引号与破折号也有资格。
6. **代码网格宽度按 UAX #11。** W、F 占两列，半角片假名改为一列。
7. **分类表改为两级表**（约 38KB，两次读取）。RULES_VERSION 1 有 2,094 个区间，逐字二分查找曾使 87K 引擎时间 +2.7ms。

**范围：** 引擎 golden 只有 hlist 变化（201 个文件：类名 Other → Alpha/Digit/Infix 等；引号、破折号、↩、⚠ 旁的词间胶新增 kern 上下文，mock 下宽度仍为 257su）。breaks、blocks、layout、html 均不变；WASM 一致。新用例 cjk/controls 覆盖控制符、谚文与新覆盖字符。真实语料 340 篇（mock）断点 0 变化；语义页不变。

**审阅结论：改进。** 新覆盖字符与控制符按规范处理，现有用例与语料的排版不变；真实字体下引号、破折号旁的字距预算更准确。

## P4-06 连字注册表、ExHyphen、hyphens/overflowWrap（T5 步骤 9）

**变化：**
1. **词典注册表（D-X09）。** Liang 模式统一用 TeX 模式文本，编译成同一种 trie：
   - en-US 常驻：tools/hyphc.mjs 生成 engine/gen/hyphen_en_us.h，首次使用时编译；
   - 其他语言经新资源行 hyphPatterns（RES_VERSION 2）由宿主按需提供，Session 缓存（包括"宿主没有"）；
   - Emit 等所有词典到齐才开始，断点不会逐步变化；
   - 运行时 provider 读 runtime/assets/hyph（tools/hyphc.mjs --assets：hyphen 包的 88 个标签与 index.json），golden 用 test/hyph（德语）；
   - 宿主没有词典时：英语变体回退 en-US（info），其他语言只在软连字符和显式连字符处断（warning hyph-unavailable）；CJK 语言文中的拉丁词用 und 的 en-US，与旧行为相同。
2. **词与字母按 UCD。** 词的核心是首个字母到末个字母（General_Category L*，ucdc 从 UnicodeData.txt 生成），由字母、数字、撇号和连字符组成。按 UnicodeData 的简单小写映射，一个部分（一串字母）的字母全在词典字母表内才连字。因此：
   - 英文中的 Übersetzung 不再被 en-US 断开；
   - 复合词按部分连字（Ad-di-son-Wes-ley）；
   - 撇号前的部分也连字（con-tent's）。
3. **ExHyphen（D-X02）。** 字母之间的显式连字符（U+002D、U+2010）之后可断，断开时不加字形。罚分 break.exHyphenPenalty = 0.7，即 TeX 的 \exhyphenpenalty 50（与 \hyphenpenalty 相同）。两侧片段须满足词典的最少字母数，所以 e-mail、X-ray 不断。
4. **紧急断行表（D-X05）。** und.def 的 EMERGENCY 行按芝加哥手册的 URL 规则：
   - 冒号和 // 之后；
   - / ~ . , - _ ? # % 与 @ 之前；
   - = 与 & 两侧；
   - scheme:// 之内不断，每段至少 3 个字素簇（CONST emergencyMinPiece）；
   - 长度阈值按字素簇计；overflowWrap: anywhere 时任意簇边界都可断。
   词内的每个断点都是 Disc，带接合字距（emitter/kern-context-postpass 的 URL 部分）。
5. **noHyphen 拆成两个属性。** 删除 ICtx::noHyphen：
   - text.hyphens 未设时取块的 par.hyphenate（auto 或 manual）；
   - text.overflowWrap 未设时为 separators。
   角色样式表给标题 hyphens manual 与 overflowWrap separators，题注、行内代码 overflowWrap separators，题注标签 hyphens manual。标题中的长 URL、题注中的长路径、行内代码中的长标识符都能在分隔符处断开。
6. **连字字形取自词典。** Disc 的 pre 是词典的 hyphenChar，paint 输出它（data-syn="hyphen"），不再写死 '-'。

**范围：** 55 个用例的 blocks/breaks/hlist/layout 变化，其中 14 个有 html、1 个有 paged 变化，行数全部不变：
- 15 个用例（104 处）是 URL、DOI、路径按芝加哥规则断开：doc/url-break、doc/url-overlong、inline/prose-guards、style/text-props、cite/*（参考文献中的 URL 和 DOI 断在 . 和 / 之前，不再之后）、inline/hardbreak、inline/object-raw、cjk/cross-node、region/table-overflow-diag；
- 30 个用例（51 处）在显式连字符后新增断点（well-|known、Snap-|kerning；D-S11 这类单字母片段不断）；
- 31 个用例（59 处）新增连字点：复合词的各部分、撇号前的部分、浮动题注的行（原 noHyphen：figure/float-in-list、figure/float-pair、pages/paged-float-bottom）；
- ref/structured、ref/supplements-en、locale/auto-en 的题注标签不再连字（Fig-ure）。

新用例：
- doc/hyphen-langs-diag：德语词典经资源行；英文中的 Übersetzung 不断；en-GB 回退；fr 没有词典时的警告；复合词、e-mail、X-ray、U+2010；
- doc/emergency：标题中的 URL、行内代码、题注中的路径、anywhere、邮件地址、无 scheme 的 URL。

e2e +2：浏览器中德语按 de-1996 的点断开（且至少一处是 en-US 不会断的），没有词典的语言给出诊断。

真实语料 340 篇（mock）：
- 333 篇的行界变化，行数 132,324 → 132,141；
- 行末连字的行 24,297 → 26,147（+7.6%）；
- Σ|dw| −6.2%，过松行（dw > 256su）24,020 → 22,286（−7.2%）；
- 收紧行（dw < −64su）6,698 → 6,870（+2.6%）。

**审阅结论：改进。**
- URL 断在芝加哥规则规定的位置（旧：https:/ | /example）；
- 德语等语言按自己的模式连字；
- 复合词可在连字符后及各部分内断开，语料的词间距整体更均匀。

代价是行末连字符多了约 8%，连续连字的惩罚（doublehyphendemerits）属 P4-08 的断行器。题注保持自动连字，这是对设计的偏离，见偏差记录。

## P3-36 博客（zball-io）需要的配合改动（MD-07：本计划不修改博客仓库）

重新 vendor 引擎（`scripts/fetch-engine.mjs --local`）后，博客侧建议做如下改动；未改之前现有用法仍可工作（`renderTsm` 的旧字段都保留）。

1. 水合：`eleventy.config.js` 手写的引擎选项（`fontFamily`、`cjkFontFamily`、`lang`、`paraIndentEm`、`fonts`）改为站点设置文档（`fonts.*`、`par.indent` 等，见 docs/settings-table.md）传给 `renderTsm(src, {settings})`，客户端 `createEngine().typeset(…, {settings: bundle.settings 去掉 host 行})`；`exportStatic` 生成的水合脚本即如此。
2. 语言：删去正则读取 front matter 的 `lang`。文档语言来自 `bundle.docinfo.lang`（文档自己的 `$.doc({lang})`、宿主设置 `doc.lang` 或自动检测，P3-30）。若把含 front matter 的原文交给引擎，设置 `source.frontMatter: true`（P3-35）。
3. CSS：不再自行 import `TSR_CSS` 并复制引擎样式；用 `bundle.styles.contract` / `theme` / `rules`，或 `exportStatic` 的 `parts.head`。
4. 页面与订阅：文章片段可由 `exportStatic(bundle, {template: (p) => …})` 生成；RSS 用 `renderTsm(src, {profile: 'feed'})`（公式为源码，无水合）。
5. 双语：每种语言一个引擎实例不再必要——一个 `createEngine()` 按容器管理多个会话；各语言用各自 bundle 的 settings。
6. 资源：`bundle.resources`（清单）列出图片、`#bibliography`、`$.load` 文件；用 `exportStatic` 的 `copy` 列表或 `embedResources`。
7. 内容：`src/docs/example-hott.tsm` 与 `example-huozi.tsm` 需从本仓库 `examples/real-world` 重新复制（P3-33 起 `\x` 保留反斜杠、URL 自动链接，P3-35 的转换器修正）；其余语法变化见 docs/tsm-changes.md 的 P3-29…P3-35 条目（`&` 为公式对齐点、`/ 术语: 描述`、`*`/`_` 词内不成对等）。
