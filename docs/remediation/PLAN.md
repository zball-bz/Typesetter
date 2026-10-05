# Typesetter 审计修复计划

- 日期：2026-10-05
- 基于：`main` @ `ecc3a89`（2026-09-01）
- 依据：两轮审计。第一轮是 10 个领域调查加对抗式核查；第二轮是 9 个主题的通用化设计、对抗评审和跨主题整合。之后本计划又经过两次独立复核（依赖顺序、对照代码的事实核查），复核意见已并入本版。
- 用途：本文件是 goal 运行的**唯一执行依据**。它规定迁移方案、全部设计决策、执行守则、101 个有序步骤及验收标准，并在附录 C 给出步骤依赖表。
- 冲突裁决顺序：本文件 §3 决策 > `design/INTEGRATION.md` 的冲突裁决 > `design/T*.md` 的主题设计 > 仓库现有 `docs/`。

---

## 0. 目标与完成定义

**目标：** 修复审计发现的全部问题，共 369 条：251 条特设（ad-hoc）设计问题，118 条缺陷与文档漂移。其中 26 条为已复现的高严重度缺陷（见 §7）。做法是把特设功能换成通用机制，同时保持确定性、排版质量与编辑性能。

**完成定义（全部满足才算完成）：**
1. `TRACEABILITY.md` 中 369 条发现全部标记为已修复（填提交哈希），或按 §9 "保留特设"标记为 `kept:` 并附理由。没有未处理项。
2. §7 的 26 条高严重度缺陷各有一个回归用例，修复后通过。
3. §4.3 的全部门禁为绿，契约检查的 xfail 清单为空。
4. 原则 P1–P13（`design/INTEGRATION.md` §Principles）都有机械检查：`tools/lint-arch.mjs`、编译选项、golden runner 检查、双执行差分。每个注册表都有"同等地位"用例：文档以新名字重新声明内建行，输出除名字外完全相同。
5. 性能：update 模式编辑延迟在 7.8K、35K、87K 三个规模上都不超过基线 ×1.10 + 0.3ms；冷启动首次排版和 relayout 同此标准（§4.5）。
6. 文档：
   - 新子系统都有 as-built 的 `docs/<feature>-design.md`；
   - `architecture.md`、`document-model.md`、`design-decisions-v2.md`、`CLAUDE.md` 的修订已落地；
   - `docs/tsm-changes.md`（面向作者的变更与迁移指南）完整。
7. 真实语料（`examples/real-world/**/*.tsm`、`../zball-io/src/docs/*.tsm`）全部渲染且无新增 error 级诊断，排版变化有审阅记录（`REPORT.md`）。

---

## 1. 本目录文件

| 文件 | 内容 | 维护 |
|---|---|---|
| `PLAN.md` | 本计划（中文，手写） | 偏差只记在 PROGRESS，PLAN 不改，除非用户要求 |
| `PROGRESS.md` | 每步状态、提交、门禁结果、偏差记录、阻塞记录、性能曲线 | goal 运行每步更新 |
| `TRACEABILITY.md` | 369 条发现 ↔ 步骤（按步骤、按发现两个视图），带状态列 | goal 运行每步更新状态列 |
| `FINDINGS.md` | 369 条发现全文：描述、位置、核查更正（英文，生成） | 只读 |
| `design/T1…T9-*.md` | 9 个主题的通用化设计：抽象、接口、不变量、迁移子步骤、golden 影响（英文，生成） | 只读；as-built 结果写进 `docs/*-design.md` |
| `design/INTEGRATION.md` | 层图、共享机制、30+ 条跨主题冲突裁决、原则、保留特设、残留缺口（英文，生成） | 只读 |
| `REPORT.md` | 收尾报告（P5-02 生成） | goal 运行 |

本计划中"详见 T2·S5"指 `design/T2-constructor-ir.md` → Migration → `S5`。design 文档中 Migration 小节的"→ plan"标注以本计划附录 A 为准。

---

## 2. 迁移方案（总决策）

**MD-01 渐进式、每步可验证，不做大爆炸重写。**
- 每一步结束时全部门禁为绿。
- 结构重构与行为变化分开提交：先做字节不变的结构重构（golden 全部不变即为证明），再单独提交有意的行为变化，并逐项说明 golden 差异。
- 理由：golden 是唯一可靠的安全网，单 agent 执行时可验证性比速度重要。

**MD-02 阶段顺序：P0 → P1 → P2 → P3 → P4 → P5。**
- **P0：** 不改 IR。修复已验证的高危缺陷，同时装上契约检查、fuzz 和守护用例。
- **P1：** 在兼容模式下引入全部共享机制，以字节不变为主。涉及模式、语法表、设置、阶段、注册表、HList、盒树、资源和数学字典。
- **P2：** 一次 IR 线格式波次，包括一次降级、构造器注册表、出现级 span、通用属性、DECL、新样式线格式和 math 节点。
- **P3：** 在这些机制上把剩余特设功能通用化，包括级联、slot/flow、排除区/表格/分页、渲染协议、数学族、宿主定位器和工具。
- **P4：** CJK/拉丁排版变化集中在一波：段落级成形、数据化标点、UCD 字符类、连字、统一伸缩。
- **P5：** 剩余审计条目与收尾。

**MD-03 线格式按缓冲区增量版本演进。**
- 每个词汇行带不可变的 `since`，缓冲区的版本字节 = 它用到的最大 `since`。
- 读取器接受 `MIN_COMPAT..OPS_VERSION` 窗口。
- 整个计划只在 **P2-08**（样式线格式，P2 的第一个语义变更）提升一次 MIN_COMPAT。P2 内其后的语义变更不再提升，因为分支上没有发布过中间缓冲区，录制的用例在每一步都会重录。
- 新增词汇只取下一个 `since`，只有用到它的缓冲区才变化。

**MD-04 降级模型：LowerProgram + 用户代码洞模块 + 单一解释器（修订 v2 §2）。**
- 做法：静态标记结构不再变成 JS 文本，只有用户代码进入洞模块。文档、`m```、`ctx.m.parse` 和 sidecar 共用一种程序格式和一个解释器（`runtime/src/shared/lower.mjs`）。
- 理由：
  - 通用：所有入口语义一致；
  - 卫生：生成的名字永远不进入用户作用域；
  - 容错：静态结构不可能产生 SyntaxError，每个块都有帧；
  - 效率：静态标记不经 V8 解析，静态文本可以按字节直拷进 ops。
- 门禁：bench 中 `compileMs + executeMs` 不劣于 P0 结束时 5%。
- 未达标时：先优化（静态 TEXT 批量直拷、绑定调用免分配）。仍不达标，则退回后备方案：从同一 CallTree 打印卫生 JS（P0-05 的过渡形态即其第一步）。两种实现跑同一套 LoweringContract 用例。

**MD-05 单一事实来源 + 代码生成。**
- 来源：`schema.json` + `schema.lock.json`、`syntax.def`、`engine/data/elements.json`、属性与设置行、`stages.def`/`products.def`、`resources.def`/`inputs.def`、`diagnostics.def`、`languages.json`、`symbols.tsv`/`stdlib.tsv`、TextRules 表。
- 生成物提交入库，由门禁 G9 检查新鲜度。
- `engine/src/ops/ops.def` 从 P0-06 起改由 `schema.json` **生成**（保留 X-macro 格式，`ops.h`/`ops.cc` 照旧 include）。`ops.h` 里手写的 `OPS_VERSION`（:7）和 `KIND_COUNT`（:27）也改为生成。`tools/gen-ops-ts.mjs` 并入 `tools/gen-all.mjs`。
- 任何手写镜像（tree-sitter、TextMate、mock 分类器）都要有一致性测试和显式允许清单。

**MD-06 兼容垫片。**
- 旧 C ABI（`tsr_*`）从 P1-03 起变为 `tsr2_*` 的包装，标记为 deprecated，保留到计划结束。
- 删除留到下一次 engine-dist 发布之后，不在本计划内。

**MD-07 只在本地分支提交，不 push，不发布 engine-dist。**
- `../zball-io` 只作为只读回归语料，不修改。
- 博客需要的配合改动写进 `REPORT.md`。

**MD-08 每个缺陷先有复现用例（testing.md §0.4）。**
- 修复提交里，该用例从"记录错误输出"变为正确输出。
- P0-01 的守护用例先按现状录下错误输出，后续步骤以 golden 差异的形式展示修复。

**MD-09 设计先行（CLAUDE.md 约定）。**
- 每个新子系统在实现前写或更新 `docs/<feature>-design.md`，内容取自 `design/T*.md` 和本计划的决策。
- 落地后补 as-built 差异。
- 每步要修订的现有文档在该步列出。

**MD-10 并行与子代理。** goal 运行可以用子代理做只读调查、编写互不重叠的新用例和文档。同一文件不并行编辑。不使用 Workflow 多代理编排，除非用户另行开启。

**MD-11 偏差处理。** 实现中发现设计不成立时可以调整，但必须在 `PROGRESS.md` 的"偏差记录"写明原因、替代方案和受影响的后续步骤，并在 as-built 设计文档中体现。

---

## 3. 设计决策登记

下表裁定全部开放问题（主题设计中的 Q1–Q75 和整合残留缺口）。选择原则依次为：**通用性 > 排版质量 > 运行效率**，同时不破坏不变量 I1–I9。"来源"指 `design/*` 中的问题编号。

### 3.1 语言与前端（L）

| ID | 决策 | 理由 | 来源 |
|---|---|---|---|
| D-L01 | `@id[…]` 和 `@[label][…]` 中，若 id 或 `]` 与 `[` 之间没有空白，`[…]` 是附加内容（`extra`）。其含义由目标元素行的引用模板决定：普通引用时替换前缀词（"图"/"Figure"，即 Typst 的 supplement），引文时作为定位附注（"p. 5"，即 Typst 的 locator）。字面 `[` 写作 `\[`。行为变化：今天 `@fig[p. 5]` 解析为"引用 + 字面文字"，`@fig[x](u)` 解析为"引用 + 链接"。语料中 0 处使用，写入 tsm-changes。在 P2-06 解析，在 P2-09 接通。 | 与 Typst 一致，规则全局统一；含义由元素数据决定，不写死在语法里。CJK 或带空格的标签用 `@[label][…]`。 | T1 Q1 |
| D-L02 | 同一作用域重复 `#let` 合法，降级为对已提升绑定的再赋值。闭包看到的是最新值，文档中注明。**在 P0-05 落地**：提升时必须去重，而今天这种写法是整篇致命的 SyntaxError。 | 符合 Typst 习惯；不改写用户 JS，无需 JS 解析器。 | T1 Q2，整合 |
| D-L03 | CJK 行连接对 `——`、`……` 保持"无空格"。 | 现行行为正确：两侧都是宽标点。 | T1 Q3 |
| D-L04 | 标题 span 不含 ` <id>`，标签有自己的 span。 | 不引起 golden 抖动；锚点使用标签 span。 | T1 Q4 |
| D-L05 | 表格竖线分隔符只在**行内顶层**切格，成对强调、链接、岛和 splice 参数内部不切。`*a \| b*` 是一个格，字面竖线用反斜杠转义。成对结构跨越行时给 `row-spans-markup` 诊断。 | 结构化解析，不加启发式；解析器不必知道哪个区域是表格。 | T1 Q5 |
| D-L06 | 不开放配置声明的行内语法（T1 S15 不采纳）。关键字形式保持封闭（`#let/#if/#for/#while/#use`），不允许用户自定义行级语法。区域和 fence 就是开放的块形式。 | 避免注册悖论（v2 §4.1），保住段落打断保证。 | T1 Q6，保留特设 |
| D-L07 | `m.parse(src, {scope})`：片段中的裸值头（`#x`、`#a.b`）在 scope 对象上按属性查找，不用 eval。 | 片段可以引用值，同时满足 CSP，结果确定。 | T1 Q7 |
| D-L08 | 新增描述列表语法 `/ 术语: 描述`（与 Typst 同形），按列容器处理，降级为 `terms(item({term:…})[…])`。语义页输出 dl/dt/dd。排版默认 run-in：术语加粗，描述同行接排，续行悬挂 2em。 | 补上审计指出的缺口；复用列表容器协议。 | 残留缺口 |
| D-L09 | front matter 是宿主选项（`FrontEndOptions.frontMatter`），不是语言构造。 | 这是静态站的约定，应留在宿主层。 | 保留特设 |
| D-L10 | 单值的元素身份保持叫 `role`，不改名为 `element`；多值 token 用 `class`。 | 线格式键、`data-role` 和现有文档都已用 `role`。 | T2 Q10 |
| D-L11 | 文档设置层有两个来源：front matter（由宿主解析，按文档优先级入层），以及 `#{ $.doc({...}) }`（DECL）。`ctx.settings` 只暴露"宿主 + front matter"的预执行视图。 | 不违反 I4，脚本看不到解析结果。 | T9 Q71 |
| D-L12 | 嵌套位置（列表项、区域体）里的 `#{ $.set(...) }` 作用到所在容器结束，降级为对后续兄弟节点的 `style.where`。**在 P3-01 实现**。P2-12 到 P3-01 之间，嵌套语句里的样式压栈由帧弹出并给 `style-in-value`，嵌套 `$.set` 报错。 | 与 Typst `set` 的语义一致。 | T4 Q24，残留缺口 |
| D-L13 | 数学洞只允许 `#ident` 和 `#(expr)`，不允许 `#p.x` 链。 | 数学里的 `.` 是小数点或标点，这样不产生歧义。 | T8 Q65 |
| D-L14 | 严格成对强调、`$` 总是开启数学、ASCII 拼接头、` <id>` 只用于无歧义形式：这些保持文档化的现状，靠转义解决，不加启发式。 | 见 v2 §5、§3。 | 保留特设 |
| D-L15 | `__` 前缀保留给生成代码。用户绑定以 `__` 开头时产生 error 节点和 `reserved-name` 诊断。 | 保证生成代码的卫生。 | P0-05 |

### 3.2 IR 与执行（I）

| ID | 决策 | 理由 | 来源 |
|---|---|---|---|
| D-I01 | 采用 LowerProgram（MD-04），后备方案为卫生的打印 JS。 | 见 MD-04。 | T2 Q8 |
| D-I02 | 块出现在行内位置时：若处在 Blocks 位置，段落拆成 `para, block, para{cont}`（`cont` 段没有首行缩进和段前间距）；其他行内上下文产生 error 节点。显示数学也按此处理；P3-17 删除 `mathblock` 的 `INLINE_FALLBACK`。 | 与 Typst 一致；一条通用规则。 | T2 Q9，整合 |
| D-I03 | InstLimits 默认：节点数 ≤ max(1M, 64 × 原始节点数)，深度 ≤ 256。属于 HostOnly 设置。 | 防止指数膨胀，又不限制正常文档。 | T2 Q11 |
| D-I04 | 采用 DIAG op：执行器警告以带 span 的记录写进 ops，golden 可见。 | 只保留一个通道。 | T2 Q12，整合 |
| D-I05 | splice 结果为 `undefined` 或 `null` 时不渲染任何内容，并给 `splice-undefined` 警告。 | 与 Typst 的 `none` 一致。 | T2 Q13 |
| D-I06 | 不设区域作用域的 DECL。`element`、`counter`、`collector`、`counter-system`、`doc`、`locale`（含文本裁剪）、`fontRoles` 为提升式，后声明者胜出，并给 `decl-redeclared` info。`math.*` 为位置式，按 declEpoch 绑定。区域内的样式变化用 `$.set`（D-L12）。 | 绑定方式简单、可预测。 | T2 Q14，T8 Q60，残留缺口 |
| D-I07 | Phase 0 在完整解码后预扫描 `RawOps.decls`，线格式不要求特定顺序。 | 读取器本来就先完整解码。 | T3 Q15，整合 |
| D-I08 | `#use` 改为宿主解析的动态导入 `__use(spec)`：以 URL + `?h=<内容哈希>` 导入，因此内容变化就是新 URL，相对 import 照常可用。模块实例按 worker 复用。<br>确定性约定写进文档：模块不得跨执行保留可变状态，每次执行传入新的 `$`。<br>机械检查：录制器和 golden runner 在同一进程中对每个用例执行两次，断言 ops 字节相同；worker 的 dev 模式同样做双执行比对。<br>修订 architecture.md:143（今天写的是"生成模块中的静态 import"）。**在 P3-31 实现**；在此之前 `#use` 为 `keyword-unsupported`。 | 效率高；确定性由可执行的差分检查保证；实现排在安全评审（P3-20）之后。 | T9 Q72，复核 V2 |
| D-I09 | 静态导出按词法扫描 `import` 复制模块；遇到动态 import 给警告。 | 实现简单，不强迫用户打包。 | T9 Q73 |
| D-I10 | 语句例外：顶层声明绑定的 `#{…}`，以及不是"单个标识符"形式的 `#let`（解构、多声明符），不加帧，原位执行；其余语句加帧。<br>不加帧的语句运行时抛异常，会使模块函数失败，**其后所有块都不再执行**。此时执行器保留已发出的 ops，追加一个覆盖剩余源码范围的 error 块，并给 `script-error` 诊断。 | 声明必须在帧外，后续代码才看得见；失败时尽量保留已有内容。 | T2 S5，复核 |
| D-I11 | SyntaxError 隔离只在失败路径上进行。<br>codegen 在带外返回单元表（JS 字节范围、源 span、是否含用户代码），不在 JS 文本里插标记。<br>导入失败时对含用户代码的单元二分：k 个坏单元需要 O(k·log n) 次导入，上限 2⌈log2 n⌉+4 次；超限时把剩余可疑单元全部记为 error 单元。<br>未改动的单元复用按哈希缓存的洞模块（P2-02）。 | 正常路径零开销；用户字符串里不会撞到标记。 | T2 S5，复核 |

### 3.3 语义（S）

| ID | 决策 | 理由 | 来源 |
|---|---|---|---|
| D-S01 | figure 的 kind 自动推断：主体（不含题注）恰好是一个表格时，用表格计数器（表/Table），否则用 figure；可用 `kind:` 显式覆盖。 | 与 Typst `figure(kind: auto)` 一致，符合作者预期。现有用例里没有 figure 包 table，golden 不变。 | T3 Q16 |
| D-S02 | 嵌套的 figure 成为 subfigure：计数器从属于父 figure，编号格式为 `(a)`，引用显示"图 1(a)"。 | 排版惯例；现有用例未覆盖。 | T3 Q17 |
| D-S03 | TOC、lof 中克隆的标题保留节点自身的作者样式（强调、数学）；作用域样式取目的地（collector）的上下文。 | 目录外观统一，作者语义不丢。 | T3 Q18 |
| D-S04 | 用户只能引用 `fn-n`；`fnref-*` 是引擎保留名。 | 永久链接稳定。 | T3 Q19 |
| D-S05 | 标题编号是否显示，由 heading 元素行的 `display` 决定。内建默认 `false`。用户为 heading 声明 `numbering` 时，除非显式写 `false`，否则 `display` 为 `true`。 | 用户声明时与 Typst 语义一致；内建默认保持博客现状。 | T3 Q20 |
| D-S06 | AnchorNamer 生成的 id = 前缀 + 标签原文，只对空白、`%`、`#`、`"`、`<`、`>` 和控制字符做百分号编码。 | 编码是单射；CJK 保持可读；现有标签字节不变。 | T3 Q21 |
| D-S07 | 项目配置放在 `tsm.project.json`（章节顺序、manifest 列表、编号偏移），与静态导出清单共用。 | 配置只放一处。 | T3 Q22 |
| D-S08 | 不设单独的 `terms:` 简写。`lang:` 通过 text.lang 的继承驱动 termsLang；需要分开时写 `style:{termsLang}`。 | 一个作用域只有一种语言。 | T3 Q23，整合 |
| D-S09 | 被提升的 flow（如脚注体）默认用目的地上下文（类似 LaTeX `\normalfont`），flow 可声明 `context:'site'`。节点自身的显式样式保留。 | 斜体段落中的脚注正文不应跟着变斜。 | T4 Q27 |
| D-S10 | 段落内容的行号：机制用 T3 计数器加逐子块的 marker 槽实现，本计划不默认启用。 | 先提供机制，不改默认。 | T6 Q49 |
| D-S11 | 多行显示公式：<br>• 段落中连续出现的显示数学行（≥2 行，中间没有其他文字）构成一个 `equations` 块，各行在 `&` 处共享对齐；前后的文字按 D-I02 拆成 para 和 para{cont}。<br>• 带 ` <label>` 的行各自编号，不带的行不编号，与现状"只编号带标签的显示公式"一致。<br>• 单个岛内也可以用 `\` 加换行分行（兼容 Typst），这时岛后的标签给整块编一个号，编号放在末行。<br>前提：P1-08 起显示岛可以跨同一段落的多行，不受缩进影响；`&` 改为对齐点（它今天在 `math.cc:99` 的运算符组合字符表里，P3-29 移出并核对语料）。 | 不引入新语法，复用现有的标签后缀。语料中连续显示行出现 0 次，没有兼容风险。 | T8 Q66，复核 |
| D-S12 | 引文序号按文档顺序中的首次引用排定，脚注体在其标记处计入；采用 refsection 作用域的键控计数器（T3 S6）。 | 修复已验证的缺陷。 | T3 S0 |
| D-S13 | Resolve 不修改输入：输出是 SemInfo 覆盖层加一个合成节点 arena。这样文档语言或设置变化时可以重新进入 Resolve，不必 fork。过渡期例外：resolver 仍会写入引用的 `url`（P3-04 移除）和公式的 `name`（P3-26 移除）。 | 结构更清晰，也更快。 | T9 Q70，复核 I7 |

### 3.4 样式、设置、语言（T）

| ID | 决策 | 理由 | 来源 |
|---|---|---|---|
| D-T01 | `text.size` 采用 CSS 语义：绝对值直接替换，相对值（em、%）相对父值计算。修订 document-model §3。 | 符合直觉，与 CSS 一致。 | T4 Q25 |
| D-T02 | 作者可以使用任何**通过校验**的 CSS 颜色：命名色、hex、rgb、hsl、`var(--x)`。非法值给 `style-value` 诊断并丢弃。 | 更通用，校验同时消除注入。 | T4 Q26 |
| D-T03 | 宿主规则可以标 `force: true`，优先级高于节点自身样式（类似 `!important`）；设置层使用 HostForce。 | 站点主题需要一个强制层。 | T4 Q28，残留缺口 |
| D-T04 | MetricKey = FaceKey + sizePx + lang + features + dppx + faceDigest。FaceKey 只表示字体面身份，不含 lang 和 features。 | locl 变体随 lang 变化。 | T4 Q29，整合 |
| D-T05 | `list.marker` 的取值域是 T3 的 NumberingPattern 字符串。 | 只用一种编号小语言（原则 P8）。 | T4 Q30，整合 |
| D-T06 | `doc.lang` 默认 `auto`。<br>**检测：** Phase 0（解码后、实例化前）扫描 RawOps 文本，确定性地判定：含假名 → ja；含谚文 → ko；汉字 → 按数据表中繁体特有、简体特有常用字的计数，取 zh-Hant 或 zh-Hans；拉丁字母 → en；其他 → und。<br>**覆盖：** 宿主、front matter、`$.doc` 都可以覆盖检测结果。<br>**宿主侧：** 结果由 `tsr2_get('docinfo')` 返回。shell 用它设置容器 `lang`，并删掉 `shell.mjs:333` 的 `'zh-CN'` 默认值和 `:356/:467` 的预设；export-static 用它写 `<html lang>`，取代 `export-static.mjs:56`。<br>**golden：** 配置固定为 zh-CN。 | T4 曾以"会静默改变不传 lang 的宿主"为由否决改默认值。但 auto 对简体中文文档的结果与今天相同（zh-Hans）；会变的只有今天本来就错的文档：英文得到 Figure，日文得到図，繁体得到圖。这是纯粹的质量提升。 | T4 Q31，复核 |
| D-T07 | 流式页和语义页通过 env 哈希类加生成的 CSS，反映文档中部作用域内的 `$.set` 规则。按 role 或 class 选择的规则，要等 P3-18（`tsr-c-*`）和 P3-23（`data-role`）提供钩子后才在这两类页面上生效。 | 让无 JS 的页面与排版页一致。 | T4 Q32，复核 R6 |
| D-T08 | 级联优先级从低到高：引擎 kind 默认（`defaults.json`）< 内建元素样式段 < 宿主规则 < 文档元素样式段 < 作用域内的文档 `$.set` < 节点自身样式及声明的别名 < 宿主 force 规则。 | 采纳整合结论，并加上 D-T03。 | 整合 |
| D-T09 | letter-spacing 和 word-spacing 永远不作为作者属性开放。 | 它们是引擎做两端对齐的通道（v2 §8）。 | 保留特设 |
| D-T10 | 设置的优先级格为 HostOnly / HostDefault / DocOnly / HostForce。未知键给警告，作用域不对给 `config-scope`。 | 采纳 T9 的设计。 | 整合 |

### 3.5 成形与断行（X）

| ID | 决策 | 理由 | 来源 |
|---|---|---|---|
| D-X01 | 伸缩模型采用 v2 §8：容量 = (n_latin + k·n_cjk) × 每段的 juSu。在 P4-08 一次切换，同时修订 App C 并重新校准 shrinkThreshold。 | 断行代价与渲染"构造上一致"。 | T5 Q33，整合 |
| D-X02 | ExHyphen 默认开启，罚分 50（TeX 的 `\exhyphenpenalty`），只用于拉丁词内部，不用于 URL 和代码。 | TeX 默认如此，排版质量更好。 | T5 Q34 |
| D-X03 | 文档裁剪 v1 只开放三项：逐码位字符类、定义宽度、连字例外；不开放 blank/邻接矩阵。 | 保住禁则保证。 | T5 Q35 |
| D-X04 | Unicode 版本固定为 17.0.0（与 Node ICU 78 的 unicode 17.0 一致）。RULES_VERSION 只能有意提升，并同时重录 golden。 | JS 与引擎的字符分类保持一致。 | T5 Q36 |
| D-X05 | 紧急断行：行内代码、标题、题注默认 `overflowWrap:'separators'`。URL 用 Chicago 断行规则，链接不另设更低罚分。 | 既不溢出，也不乱断。 | T5 Q37 |
| D-X06 | 行内 image/raw 默认边界类为 Ideo，可用 `edge:'alpha'` 覆盖。 | CJK 正文中的图标不额外加空隙。 | T5 Q38 |
| D-X07 | 行内错误文字是合成文本，复制时省略。 | 复制保真。 | T5 Q39 |
| D-X08 | 接受更细的 run 粒度。P4-01 验证 87K 文档的 DOM 节点数增长 ≤10%。 | 留出数据核验点。 | T5 Q40 |
| D-X09 | 连字词典：en-US 常驻；其他语言通过资源行 `hyphenPatterns` 按需提供，缓存在 Session。 | 兼顾体积与通用性。 | T5，T9 Q75 |
| D-X10 | ItemKind = {Box, Glue, Penalty, Disc}，没有独立的 Kern；Kern 的枚举值保留不用。 | 采纳整合结论。 | 整合 |

### 3.6 布局与分页（Y）

| ID | 决策 | 理由 | 来源 |
|---|---|---|---|
| D-Y01 | 不齐行（ragged）的行末伸缩：左对齐和右对齐为 2em，居中每侧 1em；所有角色相同。 | 在 1–3em 范围内 golden 不变。 | T6 Q41 |
| D-Y02 | 浮动旁边的非段落块默认 Clear（figure-design §4），Shrink 需要显式开启。 | 避免出现过窄的块。 | T6 Q42 |
| D-Y03 | 分页器用贪心字典序，线性时间。 | 高效，且能复现现状。 | T6 Q43 |
| D-Y04 | keep 分层，放宽顺序：先放宽 keep-together（块整体），再放宽 widow/orphan，再放宽 keep-with-next（标题随后文），最后是 Structural（原子不拆，可见溢出并给诊断）。若 `pages/paged-doc` 需要别的顺序，作为有意变更重录。 | 标题落在页底最难看，所以最后放宽。 | T6 Q44 |
| D-Y05 | 题注对齐统一：单行居中；多行两端对齐，末行不齐。块题注与浮动题注相同。 | LaTeX `caption` 包 singlelinecheck 的惯例。 | T6 Q45 |
| D-Y06 | 页码引用的槽宽 `page.refDigits` 默认为 3；页码引用功能本身属于 P5 可选项。 | — | T6 Q46 |
| D-Y07 | 列表间距保持 1/3 段距，按整数除法取 409su，golden 不变。 | — | T6 Q47 |
| D-Y08 | 段间 margin 由 su 写出（T7 S10）。33 个 html golden 由 19.2px 变为 19.203px。 | 几何只有一个权威（I2）。 | T6 Q48 |
| D-Y09 | 屏幕上的超宽表格：先把轨道缩到 min-content；仍然超宽就放进横向滚动容器（`overflow-x:auto`）。分页时允许可见溢出，并给 `table-overflow` 诊断。 | 不侵入页边。 | T6 Q50 |
| D-Y10 | 表格行不在内部拆分；列表标记默认不测量、右锚定；屏幕视为一张无限长的页。 | 保留特设。 | 整合 |
| D-Y11 | 内建 figure 和 quote 默认无边框，主题可以开启。 | 保持现有外观。 | T7 Q54 |

### 3.7 渲染、复制、无障碍（R）

| ID | 决策 | 理由 | 来源 |
|---|---|---|---|
| D-R01 | 复制默认值：类正文的生成文字（引用、引文、题注前缀、显示出来的标题编号）复制为文本；装饰性的生成文字（脚注标记、列表标记、公式编号、行号、断字连字符、错误文字）省略。同步更新 e2e 断言（`typeset.spec.mjs:137-150`）。 | 看到什么就复制什么，与语义页的原生复制一致。 | T7 Q52 |
| D-R02 | 内建 group 的行默认带 `data-role`。 | 给主题提供钩子，成本很低。 | T7 Q53 |
| D-R03 | sidecar 的复制：选区跨代码行时只复制代码；选区完全在 sidecar 轨内时复制 sidecar 文本，不带标记。 | 最常见的需求是粘贴代码。 | T6 Q51，T7 Q55 |
| D-R04 | `a11y.mathLabel` 默认开启，数学 span 带 `role=math` 和取自源码的 `aria-label`。 | 改善无障碍，成本低。 | T7 Q56 |
| D-R05 | 分页输出是 HTML 版面，交给浏览器打印。不写 PDF；DisplayList 保留文本 run，不存字形位置。 | 不在本计划范围内。 | T7 Q57 |
| D-R06 | 语义页序列化器给生成文字标 `data-syn`/`data-copy`。 | 两个阶段的复制契约一致。 | T7 Q58 |
| D-R07 | 语义页和静态页的数学使用最终盒子加 `aria-label`。MathML 是可选的 feed，不在本计划内。 | 外观一致。 | T8 Q63 |
| D-R08 | 只有全部代码单元都命中 Session 时，语义页首绘才等待 token。 | 只在零成本时等待。 | T9 Q74 |
| D-R09 | RawHtml 是唯一受信的不转义路径。元素和属性白名单只能随引擎版本扩展，并且要经过安全评审（P3-20）。 | 见 document-model §9。 | 保留特设 |

### 3.8 数学（M）

| ID | 决策 | 理由 | 来源 |
|---|---|---|---|
| D-M01 | 只有圆括号 `(…)` 在上下标、分式和参数位置被剥去外层；`{…}` 与 `[…]` 在任何位置都可见（与 Typst 相同）。原子组用 `class(ord, …)`。<br>**这是行为变化：** 今天 `shed()`（`math.cc:300-301`）会剥去任何括号组，`a^{b c}` 不显示花括号。语料中 0 处使用，写入 tsm-changes。在 P3-24 落地（T8 S4 的"(…)-only shedding"）。 | 可见的括号就保持可见，作者不用记哪种括号会消失。 | T8 Q59，复核 |
| D-M02 | 数学声明从声明处起对整篇文档生效（按 declEpoch），不随区域结束而失效。 | 同 D-I06。 | T8 Q60 |
| D-M03 | 默认不启用逐样式的参考墨迹（reference ink）。 | 保持现状。 | T8 Q61 |
| D-M04 | `implicitNames` 诊断的默认级别为 `info`。 | 能暴露转换器错误，又不打扰写作。 | T8 Q62 |
| D-M05 | `cancel` 不在本计划内：tex2tsm 遇到时给诊断，不增加斜线绘制原语。 | 收敛范围。 | T8 Q64 |
| D-M06 | 内嵌 Euler 作为默认数学字体；宿主提供的数学字体通过声明输入（`.tsmf`）在 P5-01 支持。 | 采纳整合结论。 | 整合 |

### 3.9 宿主、资源、性能（H）

| ID | 决策 | 理由 | 来源 |
|---|---|---|---|
| D-H01 | 图片尺寸：<br>• 作者给的 w/h 是声明的固有尺寸；只给 w 时，h 按提供方返回的宽高比计算；提供方永远不覆盖作者值。<br>• 显示尺寸 = 固有尺寸 × scale，上限为版心宽度。<br>• 保留 figure-design §8，修订 §3。 | 修复已验证的缺陷。 | T9 Q67 |
| D-H02 | 测量缓存移到引擎侧，由宿主持有的 Session 管理（修订 v2 §6）。 | 内容键完整，缓存永不过期。 | T9 Q68 |
| D-H03 | 迟到的字体面加载完成后，默认重新排版（P1-03 起用 fork），宿主可以关闭此行为。 | 否则测量与绘制用的不是同一个字体，违反 I2。 | T9 Q69 |
| D-H04 | 增量编辑仍然每次整篇 compile/execute。复用靠三样东西：洞模块哈希缓存、Session 的内容键答案、BreakMemo。本计划不做增量前端（非目标），由性能门禁兜底。 | 基线显示 87K 文档的 compile + execute 约 5.3ms，可以接受。 | 残留缺口 |
| D-H05 | 性能预算见 §4.5：每阶段结束时不劣于上一阶段 5%，计划结束时不超过基线 ×1.10 + 0.3ms；分段监控 compile/execute/ingest/engine/render 和 relayout。 | 把 I9 落成可执行的门禁。 | 残留缺口 |
| D-H06 | ABI 握手只有一个：`tsr2_abi()` 返回 {opsWindow, schemaHash, programAbi, resVersion, renderVersion, syntaxVersion}；ops 头只带版本字节。programAbi、syntaxVersion、resVersion 在各自子系统出现之前取占位值（分别在 P2-02、P1-05、P1-19 出现）。 | 采纳整合结论。 | 整合，复核 I6 |
| D-H07 | 诊断带来源（Stage/pid、Settings、Input、Resource、Execute）和 span。 | 采纳整合结论。 | 整合 |
| D-H08 | 每个边界解码器在**引入它的那一步**就配一个 libFuzzer 目标（clang 21）：ops、linepass、inline、settings、RES 答复、LowerProgram、片段、inputs、`.tsmf`。 | testing.md §6。 | T9 M1F，复核 R5 |
| D-H09 | 安全评审（P3-20）覆盖当时已经存在的面：RawHtml、白名单、定位器的限域与请求方类别、文档 provider、全部已有解码器。后来引入的面（inputs、`#use`、`.tsmf`）在引入它的步骤里做评审补遗。 | 评审的对象必须真实存在。 | 残留缺口，复核 R5 |

### 3.10 采纳的跨主题冲突裁决

`design/INTEGRATION.md` §Conflicts resolved 中的 30 条裁决全部采纳，在下列步骤落实：

| 裁决 | 落实步骤 |
|---|---|
| 软换行在线格式上是行内文本中的 U+000A，不新增 kind | P2-10 |
| 只有一个 DECL op，绑定方式由命名空间在模式中声明 | P2-05 |
| 每个类一条元素行，分 semantic/style/layout/html 四段；取消 `render.classes` 和 `$.role` | P1-10、P3-01、P3-14、P3-23 |
| 只有一个 Selector，membership 用的子集在 P1-10 先落地 | P1-10、P3-01 |
| 部件统一用通用属性 `slot` 命名 | P2-05 |
| `attach` 是一个 Extent 级属性 | P2-08、P4-07 |
| `tag` 是通用块槽；`equations` 是 schema kind | P2-16、P3-29 |
| 只有一个 InlineObject 协议 | P1-13 |
| 布局只有 VList/Fragment 一种输出 | P1-18 |
| 复制用 `syn` + `copy` 两个正交属性 | P2-05 |
| 只有一套设置 ABI 和一种用例格式 | P1-03 |
| 只有一个 ABI 握手 | P1-01 |
| 只有一个片段导出 | P2-13 |
| sidecar 在默认 fence 中拆分 | P2-13 |
| stages.def 与 T6 同步编辑 | P1-03、P1-15、P1-16、P1-18、P3-12、P3-28 |
| 硬换行按"T6 S1 → T5 步骤 3 → T1 S10 转义与构造器同一提交"的顺序落地 | P0-12 → P1-13 → P3-33 |

---

## 4. 执行守则（goal 运行）

### 4.1 分支与提交
- 开工时从 `main` 建分支 `remediation/audit-2026-10`，第一个提交是 `docs/remediation/`（P0-00）。
- 每步至少一个提交，可以拆成"结构重构（golden 不变）"与"行为变化（golden 有变化）"两个提交。标明"单提交落地"的步骤除外。
- 提交信息格式为 `<area>: <摘要> (plan <步骤ID>)`。
  - 正文列出：golden 变化的文件及原因、修复的发现数量、门禁结果摘要。
  - 结尾按 CLAUDE.md 约定写 `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`，再加上当前会话要求的会话行（如果有）。
- 不 push，不发布，不改 `../zball-io`。

### 4.2 每步循环
1. 读本计划中该步的条目、附录 C 的依赖、对应的 `design/T*.md` 迁移子步骤，以及相关的 FINDINGS。
2. 需要新设计时，先写或更新 `docs/<feature>-design.md`（MD-09）。
3. **先写用例：** 该步修复的每个缺陷类发现都要有回归用例（MD-08）。结构重构的步骤先补齐守护用例。
4. 实现。
5. 跑门禁（§4.3）。有 golden 差异时按 §4.4 处理。
6. 更新 `PROGRESS.md`（状态、提交、门禁、偏差）和 `TRACEABILITY.md`（状态列）。
7. 提交。

### 4.3 门禁
所有命令都在仓库根目录 `/home/dev/typeset/Typesetter` 下执行。tsr_tests 的 `.` 参数相对当前目录。汇总脚本 `tools/gate.sh` 是 bash 脚本（因为 G4 要 `source`），由 P0-00 建立，提供 `--quick` 和 `--full` 两档。

| 门禁 | 命令 | 起始步骤 |
|---|---|---|
| G1 原生构建与 golden | `cmake --build engine/build && ./engine/build/tsr_tests .` | 现在 |
| G2 ASan/UBSan | `cmake --build engine/build-debug && UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ./engine/build-debug/tsr_tests .`。gate.sh 先确认该目录的 `CMAKE_BUILD_TYPE=Debug`。P0-00 在 CMake 的 Debug 块加 `-fno-sanitize-recover=all`，让 UB 直接失败。 | 现在 |
| G3 录制新鲜度 | `node tools/record-fixtures.mjs --check`（依赖 G1 生成的 tsrc） | 现在 |
| G4 WASM 构建 | `source ~/emsdk/emsdk_env.sh >/dev/null 2>&1 && cmake --build engine/build-wasm` | 现在；必须在 G5 之前 |
| G5 e2e | `runtime/assets/hl` 不存在时先 `npm run hl-assets`，然后 `npx playwright test`（4 个 dsf 项目） | 现在 |
| G6 语料 | `npm run corpus`（199 通过、0 findings）+ `node tools/review-corpus.mjs --check`（真实语料没有新增 error 诊断） | 现在；review-corpus 自 P0-01 起 |
| G7 契约检查 | golden runner 内置；xfail 清单只减不增 | P0-01 |
| G8 fuzz | 每步 `tools/fuzz.sh --smoke`（每个目标 10 秒）；阶段末 `--long`（总预算 30 分钟，各目标平分，每个至少 3 分钟） | P0-03 |
| G9 生成物新鲜度 | `node tools/gen-all.mjs --check`（模式、ops.def、ops.gen.mjs、语法、元素、资源、阶段……） | P0-06 |
| G10 架构 lint | `node tools/lint-arch.mjs`：include 规则、字符串比较、名字字面量、switch-enum | P0-02 起逐步加规则 |

- 每步至少跑 G1–G3、G7–G10；涉及 runtime、wasm 或渲染的步骤还要跑 G4–G6。
- **每个阶段结束时**跑全部门禁、`--long` fuzz 和 §4.5 的性能门禁，并记入 PROGRESS。
- 每新增一个门禁（G7–G10、新的 fuzz 目标、生成物检查），同一步内同步更新 `.github/workflows/ci.yml`，让 CI 与本地门禁一致。CI 只在用户日后 push 时运行。`gen-ops-ts` 那一步改为 `gen-all --check`。
- 临时 Playwright 探针按 CLAUDE.md 放在仓库根目录，命名为 `probe-*.mjs`，用完删除。

### 4.4 golden 政策
- **默认字节不变。** 只有该步"golden 影响"里声明的文件或属性可以变化。
- 有变化时：
  1. 跑 `./engine/build/tsr_tests . --update`；
  2. 看 `git diff --stat test/golden` 和具体差异；
  3. 机械性变化（例如 margin 19.2 → 19.203、版本字节、`url` → target）用脚本核对"只改了这些属性"，脚本放 `tools/golden-diff/`；
  4. 排版性变化（断行、间距）逐个审阅，理由记入 `REPORT.md`，P4 尤其如此。
- 声明之外的差异一律视为缺陷：修代码，不重录。
- "golden 原本就错"一类的修正单独提交，并在提交信息中说明，例如 notes/*.ast 漏掉 Note、语义页脚注 id 悬空。
- `.ops` 的变化只能来自声明过的线格式变化。

### 4.5 性能门禁与基线
基线（2026-10-05，`node tools/bench-edit.mjs --sections N --mode update`，取中位数；复核时 7.8K 三次分别为 4.2、3.8、3.7ms）：

| 规模 | 编辑延迟 | compile | execute | ingest | engine | render | 冷启动 |
|---|---|---|---|---|---|---|---|
| 7.8K（N=18） | 3.8ms | 0.2 | 0.7 | 0.1 | 0.7 | 0.7 | 83ms |
| 35K（N=80） | 11.9ms | 0.6 | 2.0 | 0.1 | 2.7 | 2.9 | 110ms |
| 87K（N=200） | 28.1ms | 1.3 | 4.0 | 0.3 | 6.5 | 6.9 | 151ms |

typeset 模式的中位数分别为 8.5、26.9、66.9ms。

规则：
- **测量方法：** P0-00 先改进 `bench-edit.mjs`：`--runs 3` 自动取中位数的最小值；阶段耗时打印两位小数；新增 `--mode relayout`，测一轮宽度变化的耗时。改完后重新录一次基线，记入 PROGRESS。
- **阶段门禁：** 每阶段结束时，三个规模的 update 中位数都不劣于上一阶段结束值的 5%，另加 0.3ms 噪声余量。relayout 同此规则。
- **终点门禁：** 不超过基线 ×1.10 + 0.3ms。
- **按步测量：** 标了【性能】的步骤在该步就测。未达标先剖析并优化，不能把超标带入下一步。

### 4.6 进度与断点续做
- `PROGRESS.md` 是唯一的进度真相，包括：步骤表（状态 todo/doing/done/blocked、提交、日期、golden 变化数、备注）、偏差记录、阻塞记录、性能曲线。
- 上下文被压缩或会话中断后，先读 `PROGRESS.md`，再用 `git log --oneline remediation/audit-2026-10` 核对，然后从第一个非 done 的步骤继续。

### 4.7 阻塞处理
- 某步经过合理努力仍过不了门禁时，按以下顺序处理：
  1. 把工作存到 `git stash` 或 WIP 分支 `wip/<步骤ID>`；
  2. 工作树恢复到上一个绿提交；
  3. 在 PROGRESS 中标 blocked 并写明原因；
  4. 按附录 C 跳到不依赖它（直接或传递）的后续步骤。
- 分支上永远不留红提交。
- 同一阶段内 blocked 的步骤超过 3 个时，停下来向用户报告。

### 4.8 文档修订
每步列出的文档修订随该步提交。全局修订在 P3-37 和 P5-02 收尾。凡是改变作者可见行为的步骤，都要在 `docs/tsm-changes.md` 里追加条目（该文件由 P0-00 建立）。

---

## 5. 目标架构摘要

各层的职责与禁止项详见 `design/INTEGRATION.md` §Layer map。执行时必须守住以下边界：

| 层 | 拥有 | 禁止 |
|---|---|---|
| L0 语法 | `syntax.def`、SurfaceLexer、BlockAutomaton、CallAST、`parseContent`、前端导出 | 决定块/行内放置、CJK 连接、竖线切格；注册标签；扫描越出叶子；读取 FrontEndOptions 以外的设置 |
| L1 降级 | `sugar.def`、LowerProgram、洞模块、片段程序 | 把静态结构打印成 JS；把生成的标识符放进用户作用域；特判构造器名 |
| L2 执行 | 解释器、注册表、帧、Node 值、DECL 发射、ops 写入、DIAG | 读取解析结果或度量（I4）；让异常逃出帧；按内建名字分派 |
| L3 模型 | 模式驱动的读取与校验、Phase 0、显式栈实例化、`normalize()`、Selector、Cascade | 编号；构建呈现；读度量；绕过 `Cascade.make` 造节点 |
| L4 语义 | 元素行的语义列、计数器 + NumberingPattern、Index、分阶段 resolver、模板、集合 | 写绝对样式、CSS、URL、DOM id；依赖遍历顺序；比较 role 字符串 |
| L5 成形 | TextRules、Shaper、HList、run 实例、InlineObject | 读版心宽度；输出 DOM；把文字体系的判断留给后续层 |
| L6 布局 | BoxTree、LineBreaker、BlockLayouter 注册表、ParShape/排除区、VList/Fragment | include `model.h`；读 role 或 kind；依赖哈希表迭代顺序 |
| L6.5 分页 | PageBuilder、PageSpec | 重新断行；按 kind 判断 keep；回馈 resolve |
| L7 绘制 | DisplayList、HtmlWriter、AnchorNamer、PresentationMap、CopyPolicy | 决定位置、间距、keep、断行；保留跨块状态；在 AnchorNamer 之外生成 id |
| H 宿主 | `drive()` 循环、ResourceHost、Session、worker 信箱、shell 核心 + Behavior | 抓取 DOM 或标签拼写；直接改活文档的宽度而不使下游失效 |
| X1–X4 横切 | 共享模式、设置与级联、元素注册表、阶段/资源/诊断表 | 手写镜像；复用 id；让行为依赖类名 |

---

## 6. 步骤

每个条目包含：**来源**（design 中的主题步骤）、**要点**、**验收**（通用门禁之外的部分）、**golden 影响**、**ops 影响**、**决策**。

- 依赖关系见**附录 C**。
- 每步修复的发现清单见 `TRACEABILITY.md` §A。
- 文件行号以 `ecc3a89` 为准；实现前先确认。

### P0 安全网与已验证的高危缺陷（不改 IR）

**P0-00 准备**
- 要点：
  - 建分支，提交本目录，PROGRESS 置为 doing。
  - 建 `tools/gate.sh`（bash；G1–G10 汇总；`--quick` 和 `--full`；先 cd 到仓库根；检查 build-debug 是否为 Debug）。
  - 建 `tools/env.sh`（emsdk 环境）和 `docs/tsm-changes.md`（空模板）。
  - `engine/CMakeLists.txt` 的 Debug 块加 `-fno-sanitize-recover=all`（编译和链接都加）。
  - 按 §4.5 改进 `bench-edit.mjs`（`--runs`、两位小数、`--mode relayout`），然后重录基线。
- 验收：在未改动的代码上，`tools/gate.sh --full` 为绿；基线写入 PROGRESS。

**P0-01 契约检查与守护用例** — 来源：T7 S1、T6 S0
- 要点：
  - golden runner 对所有 html/semantic/paged 输出做以下检查，已知失败列入 `test/golden/XFAIL`：
    - 每个内部 `href=#x` 恰好对应一个 `id=x`；
    - 无重复属性；
    - id 唯一；
    - raw 之外只出现白名单元素；
    - 非合成文本行带 `data-s/e`。
  - 已知失败包括：`notes/*.semantic` 的悬空 id，代码行和题注没有 span。
  - tsrc 增加 `--snap` 和 `--page-height`。
  - 新增守护用例，先录下现状的错误输出：
    - snap 与 sidecar 同用；
    - 列表内的左浮动；
    - 240px 页上，带标签的标题后跟带标签的显示公式；
    - 折行的标题与题注；
    - 页边界上的 3 行段落；
    - 16px 与 18px 下的嵌套列表、引用、代码；
    - 标题后跟 200px 的 figure（keep 回退）；
    - 距页底 100px 的浮动；
    - 同一段中两个超长 URL。
  - 新增 `tools/review-corpus.mjs`：
    - 把 `examples/real-world` 和 `../zball-io/src/docs` 渲染到 `test-results/review/<label>/`；
    - `--check` 断言没有新增 error 诊断；
    - `--diff a b` 输出差异摘要。
- 验收：现有 48 个用例全部字节不变；契约检查对非 xfail 项全部通过。
- golden：只新增文件。ops：无。

**P0-02 编译器防护** — 来源：T1 S1
- 要点：
  - 删除 `codegen.cc:198-200` 和 `fragment.cc:89-91` 的 `default:`，改为显式 case。
  - `dumpAst` 补上 Note。
  - 对 `linepass.cc`、`inline.cc`、`fragment.cc`、`codegen.cc` 加 `-Werror=switch-enum`。ast.h 是纯头文件，`parseDoc` 在 inline.cc 里。今天会报的有 3 处：`inline.cc:664`（缺 Note）、`fragment.cc:39`、`codegen.cc:27`。
  - 建立 `tools/lint-arch.mjs` 骨架。
  - 新增 App A/B 的一致性用例。
- golden：`notes/{basic,explicit,cjk-glue}.ast.txt` 补上 `note` 行。原 golden 是错的，单独提交。

**P0-03 Fuzz 基础设施** — 来源：T9 M1F，D-H08
- 要点：
  - 在 `engine/fuzz/` 建 `fuzz_opreader`、`fuzz_linepass`、`fuzz_inline`：clang 编译，`-fsanitize=fuzzer,address,undefined`，CMake 选项 `TSR_FUZZ`，以现有用例作种子。
  - `tools/fuzz.sh --smoke|--long`。
  - 跑出的崩溃一律先转成用例，再修复。
- 验收：每个目标 `--long` 3 分钟无崩溃。

**P0-04 前端越界修复（过渡）** — 来源：T8 S0，以及 T1 S3、S4 的过渡子集
- 要点：
  - **扫描不越出叶子。**
    - `contiguous()`（`inline.cc:68-72`）越过当前叶子最后一个 span 时返回 false。
    - 行内注释的扫描也限定在叶子内。今天 `inline.cc:243-255` 用的是 `hardEnd = all.size()`，并且不调用 `contiguous()`。
    - 数学、注释、splice 在叶子内没有闭合时，按字面文本处理并给 `unclosed-*` 诊断。
  - **CRLF。**
    - `SourceText::lineEnd`（`source.h:24-28`）把 `\r\n` 当作行终止符。
    - 以下原始范围的切片在 cooked 文本中把 `\r\n` 规范成 `\n`：fence 体（`linepass.cc:195`）、块注释（`linepass.cc:214`）、行内注释体（`inline.cc:260`）、多行数学，以及 `linepass.cc:148` 处的行尾处理。
    - span 仍然是原始字节偏移。
  - **竖线切格感知岛（过渡）。**
    - `splitCells`（`inline.cc:454-508`）跳过数学岛（含 `\$` 转义）和代码 span，只在顶层竖线处切格。
    - 未闭合的反引号按字面处理，与 InlineParser 一致（今天 `inline.cc:464` 的两个分支都是 `q + 1`）。
    - 这是一个过渡用的岛扫描副本，代码注释里写明它会由 P1-06/P2-11 取代。
  - **未闭合语句的恢复。**
    - 顶层 `#let`（`linepass.cc:364-366`）今天会吞到 EOF。
    - `#{`（`linepass.cc:376-381`）今天在行尾恢复，但残缺的 JS 会被 `codegen.cc:220-223` 粘进模块，导致整篇失败。
    - 二者在平衡扫描失败时都改为在第一个空行恢复，丢弃该语句并给 `statement-unclosed` 诊断。
    - error 节点要等 P0-05，因为现在产生 KIND.error 会改动全部 `.js.txt`。
    - `#{` 的恢复点由行尾改为空行，写入 tsm-changes。
- 不在本步：行内注释拥有后续行（P1-08）、括号匹配感知岛（P1-06）。
- 验收（新回归用例）：
  - `para one $x⏎⏎= Heading⏎⏎second $ end`：标题照常输出，且不重复；
  - `[price $5](u) and $x$`：链接文字是 `price $5`，其后的 `$x$` 是公式；
  - `#!aside` 中的 `$|x|$` 是一个公式；
  - `region/hott-row.tsm`（取自 `hott-introduction.tsm:146/158`，`#!table(cols: 5)`）五格齐全，末格为 `path space $A^I$`；
  - CRLF 版本与 LF 版本的 AST 除 span 外完全相同；
  - `#let x = f(1` 之后的标题和段落保留，并给出诊断。
- golden：现有 48 个用例不变。

**P0-05 执行容错（过渡，即 MD-04 后备方案的第一步）** — 来源：T2 S5 的过渡子集；D-L02、D-L15、D-I10、D-I11
- 要点：
  - **模块形状。** 复核在 87K 文档上实测：import + execute 与原来持平，只给含用户代码的单元加帧时为 +6%。
    ```js
    export default async (__s, $) => {
      const {para: __para, text: __text, /* …全部生成用名字… */} = __s.std;  // 生成代码专用别名
      const {para, text, strong, /* …同一批名字… */} = __s.std;              // 用户可见名字：外层，可被遮蔽
      return (async () => { /* 用户语句与各单元 */ })();
    };
    ```
    - 生成的调用一律使用 `__` 别名。
    - 不要用 `__s.<name>(…)` 形式：实测 +25%，与 try 合用时 +200%。
  - **只给含用户代码的单元加帧。** 含用户代码指：splice/`val`、`__fence`、`__region`、语句。纯标记单元不可能抛异常，不加帧。
    - 帧写成 `try { … } catch (e) { __s.fail(i, e, h) }`，放在 async 上下文中，因为单元里可能有 `await __fence`。
    - 进入帧时记下 `$.style` 的栈高 h，**只在 catch 时** `popTo(h)`（v2 §12）。正常路径不弹栈：style/patch 依赖 `#{ $.style.push }` 跨块保持。
  - **`#let 标识符 = 表达式`。**
    - 提升为 `let 标识符;`，同名去重，即 D-L02 的再赋值语义；赋值放进帧。
    - 解构或多声明符形式的 `#let` 保持原样，不加帧（D-I10）。
    - 绑定模式中出现 `__` 开头的标识符时，报 `reserved-name`。
  - **`#{…}` 与运行时异常。**
    - 不声明绑定的 `#{…}` 加帧；声明绑定的保持原样。
    - 不加帧的语句在运行时抛异常时，按 D-I10 处理：保留已有 ops，追加覆盖剩余部分的 error 块。
  - **SyntaxError 隔离（D-I11）。** codegen 的 `JsProgram` 增加带外的单元表，worker、Node 渲染器和录制器把它随 JS 一起传给 executor，导入失败时二分。
  - **不再把非法 JS 粘进模块。** 以下情况改为 error 节点加诊断：
    - 保留字开头的 `#if/#for/#while/#use/#else`：报 `keyword-unsupported`，在 P2-12 和 P3-31 实现；
    - 不是 `k: v` 列表的区域头和 fence 信息参数：报 `header-positional`。例如 ```` ```js(3) ```` 今天生成 `({3})`（`codegen.cc:150-152`），会使整篇失败；
    - 嵌套的 `#let`/`#{}`：报 `statement-nested-unsupported`，取代今天静默输出的 `text("")`；
    - P0-04 报过诊断的未闭合语句。
  - **诊断通道。** P0 阶段 error 节点还进不了 DiagSink（`emit.cc:94-106, 763-772` 只负责渲染）。本步在 ingest 增加一次扫描，把 error 节点转成带 span 的诊断（来源 Execute）。P2-01 的 DIAG op 之后接管执行器那一侧。
  - **不处理的情况。** `#let x = [..]` 不特殊处理：今天它是 JS 数组字面量，继续有效，直到 P2-12 按 v2 App A:333 改为内容字面量。
  - **worker。** 块级错误时保留文档。只有二分也隔离不了的失败，才使整篇失败（`worker.mjs:220-222`）。
  - **文档。** 修订 v2 §2 和 App A:319（重复 `#let` 等于再赋值）；tsm-changes 记录 D-L02 和 D-L15。
- 验收：
  - 输入 1–5、8、9 各自只让所在单元成为 error，其他单元不变；6、7 不产生 error。
    1. `issue #todo here`
    2. `#!table(3)`
    3. 孤立的 `#aside!`
    4. `#if (c) [..]`
    5. ```` ```js(3) ````
    6. `#let list = 1` 之后的 `- item` 列表照常渲染
    7. `#let x = 1` … `#let x = 2` 是再赋值
    8. `#{ let = 1 }`：括号平衡但语法非法，走二分
    9. `#{ throw new Error("x") }`：不声明，加帧
    10. `#{ let a = f(); }` 中 f 抛出异常：前文保留，其余部分成为一个 error 块
    11. 帧内先 `$.style.push` 再抛异常：样式栈恢复
  - **【性能】** compile + execute 不劣于基线 5%。
- golden：全部 `*.js.txt` 一次性变化；`.ops` 字节不变（由 G3 证明）。

**P0-06 模式抽取、读取器校验、样式值校验** — 来源：T2 S1，以及 T4 M2 的校验子集
- 要点：
  - **模式文件。** `engine/schema/schema.json` + `schema.lock.json`：沿用现有 kind/ARGK id，`since` 全部为 6；每个 kind 的属性写入顺序与现在构造器的顺序相同。
  - **生成器 `tools/gen-schema.mjs`**，生成：
    - `ops.def`（X-macro 格式，MD-05）、`ops.h` 中的 `OPS_VERSION`/`KIND_COUNT`；
    - C++ 读取器表、类型化访问器、KindInfo、`argName`/`kindName`；
    - document-model §2.1 的表格；
    - `runtime/src/shared/ops.gen.mjs`（并入原 gen-ops-ts 的产出）。
  - **读取器校验：**
    - ArgK 范围检查；
    - 按 (kind, key) 校验取值域；
    - 数值规范化；
    - 拒绝 ARG_NODE 悬空；
    - 未知 kind 变为 error 节点；
    - 位掩码只保留公开成员；
    - STYLE_PUSH 的补丁按 `styled` 的规格校验；
    - `hl` 解析为 rangeset（取代 emit 里的 atoi 和 10000 上限）。
  - 用类型化访问器替换所有 `findArg` 循环和 `(int)` 强转（含 `emit.cc:656`）。
  - **样式值校验器：**
    - 颜色按 D-T02；
    - 字体族列表只允许带引号的名字或标识符，以逗号分隔，不允许 `;{}<>` 和控制字符；
    - lang 必须符合 BCP-47；
    - 数值必须有限且有界；
    - 非法值给 `style-value` 诊断并丢弃。这一项关闭了 CSS 注入。
  - **工具与文档。**
    - 建立 `tools/gen-all.mjs`（G9），ci.yml 中的 gen-ops-ts 改为 `gen-all --check`。
    - 修订 CLAUDE.md（"ops.def = single source of truth"改为 schema.json；"Bumping OPS_VERSION requires gen-ops-ts"改为 gen-all）。
    - 修订 architecture.md:127/181。
- 验收：
  - 48 个用例解码时没有任何 `ops-invalid`/`ops-arg` 诊断；
  - 注入用例 `#style({color:"red;letter-spacing:5px"})[x]` 的输出不含 `letter-spacing`，并有诊断；
  - fuzz_opreader 跑 60 秒通过。
- golden：无。ops：无。

**P0-07 实例化加固** — 来源：T2 S3
- 要点：
  - `copy()`（`model.cc:36-54`）改为显式栈，加上 InstLimits（D-I03）。超限时给 `inst-limit` 诊断，并截断为 error 节点。
  - 新建 `engine/src/model/normalize.cc`，承载 N0–N3（今天 `resolve.cc:469-492` 的 unwrap 和 emptyPara），由 KindInfo 的层级驱动：raw 为 Block，group/term 为 Adaptive，comment 为 Trivia。
  - 删除 `isInlineKind`。
- 验收：
  - 143 字节的指数 DAG 用例在 100ms 内以诊断结束，峰值内存 < 64MB；
  - 规范化重放触发的规则与今天相同（7 次 unwrap，2 次 empty-para）。
- golden：无。

**P0-08 样式卫生与统一 em** — 来源：T4 M0、M1
- 要点：
  - Styling 的浮点数规范化：-0 → +0，拒绝 NaN；哈希与 `==` 使用同一套规范位。
  - MetricStore 的键改为 `(u64)str<<32 | style`（`measure.h:21`）。
  - JS 的 `popTo` 做钳制，越界报 `style-underflow`；`$.style.push(number)` 给弃用诊断。
  - 新增 `emPx(StyleId) = (sizePx>0 ? sizePx : base) × sizeMul`，替换以下位置：
    - `emit.cc:251`，它会连带覆盖 `:115/:324`；
    - `emit.cc:274-275/289/478/120/759/958`；
    - MathTextCtx。
  - `emit.cc:514/541/697`（列表缩进、引用缩进、raw 默认高度）直接使用文档基础字号，这是块级几何，本步不动，在 P3-01 改为块自身 em 的 NodeProps。
- golden：只有 `style/patch.{blocks,breaks,layout,html}` 变化：22px 的 run 得到正确的 glue 和标点宽度。

**P0-09 语义正确性修复** — 来源：T3 S0（a–m 全部）
- 要点：
  - **引文与引用：**
    - 引文序号按文档顺序，脚注体在其标记处计入（D-S12）；
    - 分组引用逐键解析，未知键在自己的槽位显示 `??`；
    - 新增 `ref-shadowed` 诊断。
  - **标签：**
    - `label-duplicate` 适用于所有 kind；
    - 所有带标签的节点都注册，没有类的目标显示标题或标签文字，并给 `ref-unnumbered`；
    - 形状为 `h-<数字>`、`fn-<数字>`、`fnref-<数字>`、`bib-*` 的用户标签给 `label-reserved`（D-S04）。
  - **呈现与集合：**
    - compose 辅助函数移到 `model/`，不再写绝对样式；
    - 重复的 `#notes()` 什么都不放置；
    - 参考文献行按每个 collector 克隆，只有第一份带锚点。
  - **语义页与 dump：**
    - 语义页里紧凑列表项的段落 id 提升到 `<li>`；
    - 样式位名称表由生成得到，tree dump 中出现 SUP；
    - 语义页打印兼容的公式编号。
  - **检查与诊断：**
    - golden runner 增加锚点闭包检查；
    - 新增 post-ingest diags 阶段；
    - p1/p2/p4/p7/p8 等探针转为用例。
- 验收：
  - 脚注内引文的回归用例：liang83=[1]、knuth84=[2] 按出现顺序编号，两条都进入参考文献，链接都能解析；
  - XFAIL 中移除 notes/*.semantic。
- golden（分开提交）：
  - `notes/*` 的 tree 中标记样式变为 `[SUPx0.70]`；
  - `notes/*.semantic` 的 `<li>` 带上 `id="tsr-fn-n"`；
  - `math/eqref.semantic` 显示 (1)、(2)。

**P0-10 渲染正确性修复** — 来源：T7 S2、S6、S7
- 要点：
  - **HtmlWriter + AnchorNamer**，两个序列化器都改用它：
    - 每个元素只有一个 style 属性；
    - 属性先缓冲，再按白名单输出；
    - 只有一个转义器；
    - id 全部经 AnchorNamer 生成，`id()` = 前缀 + HTML 属性转义，与今天字节相同；
    - 重复属性在 release 构建取第一个并给诊断，在 debug 构建断言失败。
  - 断字连字符所在的 run 在其所属链接内部打开。
  - **过渡 run 键**：paint 的 run 边界纳入合成/引用标志（BF_REF），引用 run 不再吞掉相邻的正文和括号。涉及 `typeset_html.cc:454, 584-590`；P4-01 用完整方案取代。
  - 语义页的列表投影把紧凑项的锚点提升到 `<li>`。
- 验收：
  - snap-kerning 用例只有一个 style 属性，并且含 `letter-spacing`（缺陷 #21）；
  - e2e：复制引文旁的正文不再丢字。
- golden：`cite/basic.html`、`cite/unknown-diag.html`；P0-01 的 snap 守护用例。

**P0-11 宿主卫生** — 来源：T9 M0、M1；含 D-H01 的过渡修复
- 要点：
  - **worker 消息处理。**
    - 取代 `worker.mjs:269-280` 的即发即弃分发器：按 docId 建信箱，合并消息；每一轮、每次 await 之后都检查 generation（缺陷 #25）。
    - capability RPC 带请求 id。
  - **relayout 与 paginate（缺陷 #16）。**
    - 引擎侧：在已 emit 的文档上调用 `tsr_set_width` 会使 emit 失效（今天 `doc.h:392` 只把 `laidOut` 置为 false）。
    - 这样 relayout 和 paginate（`worker.mjs:228-260`）会在新宽度下重新 emit，并由信箱串行化。
    - 不新建文档：fork 原语在 P1-03 才有。
  - **资源与缓存。**
    - 用 `'\0'` 转义取代源码中的原始 NUL（`worker.mjs:28`）。
    - `imageSize`：用 `Promise.all`、`new URL(src, base)`，先嗅探文件头取尺寸，失败再回退到解码。
    - 字体只有成功加载才标记为已加载；迟到的字体面会清空测量缓存；加载失败加 TTL。
    - `tokens.mjs` 的语法失败加 TTL；literate 偏移表改为基于原文构建。
    - executor 的 `loadResource` 限定在 rootDir 内。
  - **引擎卫生。**
    - DiagSink 带来源，每个 pass 开始时截断属于自己的那一片。
    - KP 缓存逐字段混合，命中时比对键字节，按 LRU 淘汰。
    - `tsr_provide_tokens` 做 tag 边界检查，`provideImage` 做有限值检查。
  - **D-H01 过渡修复（缺陷 #24，`doc.h:212-227`）。** 只给 w 时按宽高比算 h，不覆盖作者给的 w。
- 验收：
  - e2e：resize 后图片宽度正确；连续两条文档消息不会相互覆盖；
  - w-only 图片用例的宽度等于作者的 w；
  - **【性能】** 记录 relayout 的新耗时（现在会重新 emit），作为后续门禁的基准。
- golden：无。

**P0-12 断行语义包** — 来源：T6 S1
- **提交 1，字节不变：**
  - 新增 `break/items.h` 和适配器：
    - space → Glue；
    - CJK 字 → Box、Penalty(Forbidden)、Glue、Penalty；
    - 连字符 → Disc{pre}；
    - 罚分标签 `PenTag {Normal, Forbidden, Forced}` 可以出现在任意位置，不限于段末。
  - `tsr_core` 加 `-ffp-contract=off`（复核 I9）。
  - 凡是改变字节的内容，一律移到提交 2。
- **提交 2，语义包（单提交落地）：**
  - `BREAK_INF` → Forbidden（缺陷 #18）。
  - 罚分改为 i32 千分位。
  - 采用 TeX 的丢弃规则。
  - 段末是 Forced 断点，带末行 fil 和末行收缩。
  - 显式类别，上限 1e4。
  - 最后一遍救援并给 `overfull-line` 诊断，不再把整段塌成一行（缺陷 #19，`break.cc:103-122`）。
  - dp 改用 vector，平局按 (demerits, lines, 更晚的父节点) 排序（缺陷 #26，`break.cc:57-58`）。
  - 立方用乘法计算。`CostParams::exponent` 改为 1–4 的整数，用重复乘法实现。
  - 布局把 Overfull 行放在收缩极限处。
  - 理由：T6 S1 指出，单独修 BREAK_INF 会把原先靠救援才排好的标题变成过满行。
- 验收：
  - 两个超长 URL 的守护用例按预期救援；
  - 每个用例在 native 和 WASM 下断点一致（e2e 与 tsrc 输出比对）。
- golden（提交 2）：
  - 11 个用例共 12 个段落断点变化：cite/basic、cite/unknown-diag、cjk/punct、cjk/softwrap、code/json-hl、doc/refs（2 段）、doc/refs-diag、inline/emph、inline/quotes、splice/ascii-cut、style/kern-boundary；
  - 25 个 breaks.txt 的代价数值变化；
  - math/*、figure/float、figure/stack、region/table-tiny 逐个审阅；
  - 审阅记录写入 REPORT.md。

**P0 阶段结束：** 全部门禁、`--long` fuzz、性能门禁。

### P1 基础设施（兼容模式，以字节不变为主）

**P1-01 版本窗口与 ABI 握手** — 来源：T2 S2；D-H06
- 要点：
  - 写入器：版本 = max(MIN_COMPAT=6, 所用词汇行的 since)。
  - 读取器接受 `6..OPS_VERSION` 窗口。gen-schema 拒绝修改 lock 中的 id 和 since。
  - 新增 `tsr2_abi()` 握手；worker 和 Node 核对，洞模块从 P2-02 起核对。尚未出现的子系统字段取占位值。
  - 修订 `architecture.md:127` 和 schema 的说明文档。
  - 修订 CLAUDE.md 约定：新增词汇只取下一个 since；只有语义变化才提升 MIN_COMPAT 并全量重录。
- golden：无（所有缓冲区仍是 v6，字节不变）。

**P1-02 属性注册表** — 来源：T4 M2
- 要点：
  - 共享模式加入 `props` 段，生成：
    - Styling、`==`、Hash、applyPatch；
    - 各 dump（各自保留自己的 token 顺序）；
    - JS 键表；
    - C++ 与 JS 的校验器，取代 P0-06 手写的校验器；
    - `render/style_css.h`（两个序列化器共用的 runAttrs）。
  - 新建 `docs/style-design.md`。
- golden：notes/* 的 tree 在标记上显示 SUP（若 P0-09 未做）；html 不变。

**P1-03 设置文档 ABI、阶段模型、驱动循环、用例配置** — 来源：T4 M3 + T9 M2 + T9 M3（一次落地）
- 要点：
  - **ConfigCodec 与 ABI。**
    - 分节的 ConfigCodec：T4 负责属性行和校验。
    - `tsr2_set_config(json) → {diagCount, rebuild: none|REBUILD|REEXECUTE}`：T9 负责 ABI 和 Affects。
    - 旧 setter 改为包装（MD-06）。
    - 用 host-policy 模式取代运行时字面量。
    - worker、shell、`render.mjs`、`export-static` 只转发一个设置对象。
    - 新增 `fuzz_settings`（D-H08）。
  - **阶段模型。**
    - 新增 `stages.def`（带重跑类别）和 `products.def`。
    - Resolve 从 ingest 中拆出。
    - `tsr2_doc_fork`：从保留的 ops 派生文档。MetricStore 一并复制，P1-21 起改用 Session。
    - REBUILD/REEXECUTE 规则。relayout 和 paginate 改用 fork。
    - 按阶段划分设置视图：先手写，P3-02 起生成。
  - **驱动与用例配置。**
    - `driver.h` 中的 `driveToCompletion`，以及 ProviderSet；原生 token provider 移入 `engine/src/code`。
    - `X.fixture.json {profile, settings, inputs}` 取代按文件名配置（`tests.cc:414-426`）；`--profile=golden` 必须复现每个 golden。
    - Paginate 成为独立阶段，`page.height` 成为设置。
  - **测试。**
    - 差分测试：fork 的结果等于重新构建的结果。
    - 新用例：code/snap、code/snap-sidecar、code/nowrap-snap、ref/supplements-en。
  - 新建 `docs/host-protocol-design.md`。
- 验收：
  - `tsrc --profile=golden` 逐个复现所有 golden；
  - relayout 性能不劣于 P0-11 的记录；
  - 浏览器默认正文字体变为注册表默认值，写进 tsm-changes 的发布说明。
- golden：无。

**P1-04 字体面与根契约** — 来源：T4 M4
- 要点：
  - FaceTable 和 `faceOf`；MetricStore、vmets、resolveWidths 和请求都以 FaceId 为键。
  - 明确的族解析顺序，包括 mono×cjk 规则。
  - `fontRoles`（body、mono、用户角色）取代 bodyFont/cjkFont/monoFont。
  - `.tsr-doc` 输出角色变量、基础字号和 lang。
  - shell 的 `chunkParas` 解析根开标签，并配 e2e。
- golden：48 个 html 的根行变化，由脚本核对确实只有根行变化。

**P1-05 syntax.def 与 CallAST** — 来源：T1 S2
- 要点：
  - `engine/src/syntax/syntax.def` 定义 CLASS/INLINE/BLOCK/KEYWORD/SUGAR 五类行。
  - CallAST 节点 ≤32 字节，配旁路溯源记录。
  - 通用 dump。
  - codegen 按 slot/payload 分派，经逐 slot 的旧适配器保持输出不变。
  - 新建 `docs/syntax-design.md`，同时更正 PackCC 等文档漂移。
  - `syntaxVersion` 加入握手。
- golden：无。门禁：AST 字节数不超过今天。

**P1-06 SurfaceLexer** — 来源：T1 S3
- 要点：
  - 表驱动、岛优先的词法器。内容体只有一个能感知岛的括号计数器，取代 `inline.cc:114/130/205/387-391/411/477-486` 各处匹配器。行内栈只放 Pair 和弱 `[` 帧。
  - `@[` 的内容按 IdList 解析。
  - 支持多反引号的代码 span。
  - 数学中只解码 `\$`。
  - SourceText 的 CRLF 处理正式化，行连接改为结构性的，扫描不越出叶子。删除 P0-04 的过渡副本。
- 验收：新用例 CRLF、尾部空行、`$` 越界、`^[a \] b]`、``#f[code `a]b` here]``、`[range $[0,1)$](u)`、``` ``a`b`` ```、`*range [0, 1) only*`、`_see [sic_ ok` 全部通过（缺陷 #9）。
- golden：现有用例不变。

**P1-07 BlockAutomaton** — 来源：T1 S4
- 要点：
  - 统一的容器协议：Prefix、Column、Explicit 三类；容器 span 扩展到它覆盖的每一行。
  - fence 在容器退出时结束，缩进按内容列计算。
  - 区域按名字重新同步：孤立的闭合行成为 Error 叶子；被迫闭合时给 `region-unclosed`。
  - tab 宽为 4；列表身份由（标记类别、列）决定；段落打断逐条规则处理。
  - 语句平衡到容器退出为止，取代 P0-04 的过渡恢复。
- golden：`doc/structure`、`notes/cjk-glue`、`region/figure`、`code/tsm-hl` 的容器 span 变化，涉及 skeleton/ast/js/tree/semantic/.ops。

**P1-08 行所有权与内容体** — 来源：T1 S5
- 要点：
  - Phase 1 生成 AtomTape。
  - 所有权分两类：
    - Leaf 拥有行内岛（显示数学岛可以跨同一段落的多行，不受缩进影响；D-S11 的前提）、行内 JS、行内形式的体；
    - Container 拥有注释（行内 `%--` 拥有后续行直到 `--%`，缺陷 #5）和块形式的体。
    - Pair 帧和链接帧不拥有行。
  - 暂定提交与回退（RevertedWindows）。
  - 以下体都是 Content 体，以 Blocks 模式解析，只有一个段落时自动解包：`^[`、`#f[`、`@id[`、`#let x = [`。
- 验收：
  - `#callout[⏎…⏎]` 中的列表和空行正确；
  - 行中注释能隐藏 `= ` 和 `- `，而代码 span 里的 `` `%--` `` 不会隐藏后续标题；
  - 缩进的多行显示数学岛。
- golden：现有用例不变。

**P1-09 前端导出与工具链** — 来源：T1 S6
- 要点：
  - `syntaxTokens`、`outline`、`parseJson` 加上 api 包装和 tsrc 阶段。
  - 引擎导出 tsm 词法器：本步先经 P1-03 的 ProviderSet 接入；P1-21 起改为 Session answerer。
  - `syntax.gen.json` 供 tree-sitter 的 `grammar.js` 和 TextMate 构建使用（escapeTsm 在 P3-35 接入）。只保留一个 `highlights.scm`。
  - VS Code：
    - 扩展宿主内运行一个 WASM 实例，处理 UTF-16 转换；
    - 提供引擎 token、大纲、折叠；
    - 冷启动时回退到 tree-sitter；
    - 处理器名补全依赖 P2-03 的 manifest，在 P2-03 接入。
  - 一致性测试 (b)、(e)。
  - 修订 code-design §2 和 editor-design §5。
- golden：新增 tokens/outline/astjson golden；`code/tsm-hl` 的高亮变化。

**P1-10 元素注册表、索引与分阶段解析器** — 来源：T3 S1；D-S13
- 要点：
  - **新增模块：**
    - `engine/src/elements`：注册表；
    - `engine/src/semantic`：numbering、index、collect、materialize；
    - `engine/data/elements.json`：内建行用用户数据形式书写，包括 heading、table、figure、equation、footnote、term、bibentry，以及 toc/glossary/notes/bibliography 预设。
  - **统一 Selector：** 结构体和匹配器在这里落地（membership 子集：kind/where/inside），P3-01 再扩展。不另立 Pred 匹配器（复核 R3）。成员关系在 instantiate 阶段计算，写入 `SemInfo.cls`。
  - **术语表：** 以 LocalePack 格式放在 `engine/data/locale/`，包含 zh-Hans、zh-Hant、ja、en，root 为 en。删除 `applyLang`；`tsr_set_lang` 改为宿主默认文档语言。
  - **Resolve 分阶段：** PHASE0 → LOCATE（只读）→ BIND → MATERIALIZE，不修改输入（D-S13，过渡例外见该条）。
  - 新增 `tsrc --stage=index`。
  - 删除 `resolve.cc` 中所有按功能写的代码。
  - 新建 `docs/semantics-design.md`。
- 验收：
  - 所有 tree、blocks、breaks、layout、html、semantic golden 字节不变；
  - ja 得到 図/表/式，zh-Hant 得到 圖/表/式（新用例）；
  - 同等地位（过渡）：用**仅测试用**的替代 `elements.json`，把 figure 行改名后输出除名字外相同。文档内声明的版本在 P2-07 验收。
- golden：只新增 index golden。

**P1-11 TextRules 兼容表与单一分类器** — 来源：T5 步骤 1；D-X04
- 要点：
  - `tools/ucdc.mjs` vendor Unicode 17.0.0 的 UCD 文件：LineBreak、EastAsianWidth、Scripts、emoji-data、GraphemeBreakProperty。
  - `engine/rules/locale/compat.def` 生成 `cpInfo()`，其兼容类与今天的分类逐位相同。五处分类器全部改走这个 API。
  - `mock.h` 保留冻结的 `mockIsWide()` 字面副本，并用 CI 钉住。
  - 新建 `tools/rules-diff`：比较两个规则版本的成对断行结果和类别，给 P4 使用（复核 R8）。
  - 新建 `docs/shaping-design.md`。
- golden：无。

**P1-12 HList 与 run 实例** — 来源：T5 步骤 2
- 要点：
  - emit 产出 HItem（TeX 形式）；ColdRec 带源 span、rawPx、capSu。
  - `fuseLegacy` 是一张规定好的降级表，结果必须与旧块逐字段相同，由 CI 做等价检查。
  - 新增 `tsrc --stage=hlist` 和合法性 lint。
  - layout 和 render 改为读取 item 与 run 实例。
- golden：新增 49 个 hlist golden，其余不变。

**P1-13 InlineObject 注册表与扁平化表** — 来源：T5 步骤 3；D-X06
- 要点：
  - 数学成为一种 ObjectKind（两阶段，Deferred）。
  - 各部分的高度范围经过一个垫片，接到 `layout.cc:332-335, 446-449, 528-531` 三处现有副本；P1-17 统一取代它们（复核 R9）。
  - 扁平化表由 schema 的行内 kind 生成：
    - 行内 image/raw 成为对象，修复行内图片被静默丢弃；
    - hardbreak 经 fuseLegacy 和 P0-12 的适配器，映射为 `Penalty(Forced)`（复核 R4）；
    - 其余不支持的 kind 成为 error 对象，并给 `shape-unsupported`。
  - CI 扫描每个用例树，确认没有未处理的 kind。
- 验收：新用例覆盖行内 image、raw、hardbreak（raw ops 构造），以及一个不支持的 kind。
- golden：现有不变；数学用例的 hlist 增加对象记录。

**P1-14 KP 正式化与校验缓存** — 来源：T6 S2；【性能】
- 要点：
  - 活动表遇 Overfull 即失活；去掉窗口、±1 剪枝和重试阶梯。
  - 引入 BreakParams。
  - 缓存键为 128 位，覆盖全部 DP 输入，命中时校验。
  - 前缀和用 i64 su。
- 验收：bench 和 corpus 在预算内；超出时实现有界活动模式。
- golden：无。

**P1-15 断行移入布局** — 来源：T6 S3
- 要点：
  - layout 的段落路径调用断行器。
  - 浮动追踪器改为 layout 中的 ExclusionMap，先复现今天的前缀 ParShape。
  - 删除 `Doc::typeset` 的逐 kind 循环、表格 colW 与 sidecar 的断行，以及 5 个重放字段。
  - 同步编辑 stages.def：Break 并入 Layout。
  - 修正 `figure-design.md:115-117`。
- golden：无。

**P1-16 与宽度无关的 emit** — 来源：T6 S4，以及 T9 M12 的 Affects 部分
- 要点：
  - emit 记录 SizeSpec/IntrinsicSize，由 layout 用相同公式解析。
  - image-src 诊断移到 ingest 扫描。
  - `setWidth` 只让 layout 失效，这是缺陷 #16 的结构性修复。
  - stages.def：viewport.width 只影响 Layout；relayout 原位重新进入 Layout，不再 fork（复核 R2）。
- 验收：relayout 性能明显优于 P0-11 的记录，并记入 PROGRESS。
- golden：5 个图片用例的 `blocks.txt` 变化（dump 改为打印 spec）。

**P1-17 统一行物化** — 来源：T6 S6
- 要点：
  - 表格格、浮动题注、sidecar 行都成为带锚点的 ItemList。
  - 一个 `materializeLines` 取代四个循环和 P1-13 的垫片：
    - join 扫描被丢弃的 run；
    - `endsWithHyphen` 取自 Disc，修复 sidecar 断字时丢连字符；
    - 行高计入 vmet 和数学；
    - 表格格用 `JoinPolicy::Never`。
- golden：
  - figure/block、figure/pull-diag、region/figure 中折行的居中题注获得 `data-join`；
  - 5 个 breaks.txt 增加格流记录。

**P1-18 盒树、布局器注册表、Fragment、DisplayList** — 来源：T6 S7 + T7 S3（联合）
- 要点：
  - **盒树：**
    - `engine/src/boxtree/` 在 resolve 之后构建 LayoutBlock，按内容模型选择布局器；
    - TraitTable 手写，复现今天的默认值；P3-01 起改为由 NodeProps 编译出的视图（复核 I1）；
    - 布局器：Paragraph、Stack、Replaced、Grid、Table；
    - 每个带锚块恰好有一个带锚 fragment（修复 anchor-opt-in-per-kind）。
  - **输出与绘制：**
    - `layout/laid_out.cc` 产出 Fragment；
    - `paint/` 构建 DisplayList；typeset 后端改为遍历 DL，不再读 Config 和 model；
    - 分页切割器移到 `layout/paginate.cc`；paged 后端无状态，删除 `lastAnchored`。
  - **阶段：** stages.def 增加 BoxTree 阶段；products.def 增加 blocktree、vlist、dl。
  - **守界与文档：**
    - lint 禁止 `layout/`、`break/`、`paginate/`、`render/typeset_html` include `model.h`（G10）；
    - 修正 `layout.h:20` 和 `emit.h:67` 的注释；
    - 新建 `docs/layout-design.md` 和 `docs/render-design.md`。
- 验收：新的 paged 用例（240px，标题 + 带标签的显示公式）中，之前被丢掉的 id 出现了。
- golden：现有全部字节不变；新增 blocktree、vlist、dl golden。

**P1-19 资源表** — 来源：T9 M4
- 要点：
  - `resources.def` 加 gen-res 编解码。
  - `tsr2_requests`/`tsr2_provide` 带批次号。
  - 完整的 MetricKey（D-T04）：表中存原始 px，由 Measure 阶段量化。
  - boxInfo 保留作者给的单独 w，这是缺陷 #24 的结构性修复。
  - 失败时只降级被消费的那个量，并给诊断（如 `provider-missing`）。
  - 旧的 JSON/provide 接口保留为垫片。
  - `resVersion` 加入握手。
  - 新增 `fuzz_resanswer`（D-H08）。
- golden：无。新用例：w-only 图片。

**P1-20 按段延迟** — 来源：T9 M5
- 要点：
  - `emitTop(child, pid)` 按 pid 执行；代码、图片、数学所在的 pid 延迟处理，不走整篇屏障。
  - 删除 MathTextCtx 和整篇重新 emit。
- golden：无。

**P1-21 Session 内容键缓存** — 来源：T9 M6；D-H02
- 要点：
  - `tsr2_session_new/free`。
  - 按内容键写穿缓存。
  - KP memo 移进 memo 槽。
  - answerer 注册表，tsm token 改为引擎内答复。
  - JS 侧的 canvas memo 缩为每轮去重。
- 验收：warm 与 fresh 的差分测试一致；87K bench 不退化。
- golden：无。

**P1-22 MathDict** — 来源：T8 S1
- 要点：
  - `symbols.tsv` 由今天的生成头文件播种（322 行）。
  - vendor 固定版本的 UCD 与 MathML Core 数据。
  - `tools/mathdict.py` 生成 `math_dict.h` 和 `atom.h`；`mathc.py` 不再产出 OpEntry 和类别枚举。
  - CI 检查 `kGlyphs` 的码位集合是旧集合的超集。
  - 修正 architecture §1、§5。
- golden：无。

**P1-23 MathFont 运行时对象** — 来源：T8 S2
- 要点：
  - Euler 成为 MathFont id 0，由注册表提供 family 和 hhea。
  - shell 通过 `ensureFontFaces` 安装数学字体，删除第二条 @font-face 路径和 STIX 回退。
  - MathPolicy 收纳今天的各项常量。
- golden：无。

**P1-24 MathRow 注册表与 Call IR** — 来源：T8 S3
- 要点：
  - 原语表加上 `stdlib.tsv` 模板（abs、norm、floor、ceil、binom、accents、bar、sqrt、root），以及 checkRow。
  - MNode 从 Sym 到 Error；由 slot 规格给出元数。
  - Error 叶子可以重新同步，并带子 span 诊断。
  - 只有紧邻 `name(` 时才绑定为调用；新增 `bare`；`not` 仍为 ¬。修复缺陷 #23（`math.cc:231-235, 437-439, 480-483`）：裸的 dot/hat/bar/abs 不再拖垮整个公式。
  - 新增 `--stage=mathir`。
- golden：
  - parse-diag 用例由整式文本盒变为局部布局加 Error 叶子；
  - binom 按复核结果处理；
  - 新增 mathir golden。

**P1-25 数学惰性布局** — 来源：T8 S6
- 要点：
  - `prepareMath` 在 emit 阶段产出分段计划和需求；finalize 走通用的待定对象钩子。
  - 删除 `Doc::mathTextMissing` 和手工合并。
  - 叶子分别使用 measureStyle 和 paintStyle。
  - 未覆盖的码位作为测量文本叶子处理。
- golden：无（诊断不再重复）。

**P1 阶段结束：** 全部门禁、`--long` fuzz、性能门禁。

### P2 IR 波次（一次 MIN_COMPAT 提升）

**P2-01 Node 值与 DIAG 通道** — 来源：T2 S4；D-I04、D-I05
- 要点：
  - Node 值冻结、带私有品牌、不属于任何缓冲区；`toContent`/`plain`。
  - `undefined`/`null` 不渲染任何内容，并给 `splice-undefined`。
  - DIAG op（since 7），error 节点带稳定代码。
  - P0-05 中执行器侧的 error→诊断扫描改用 DIAG。
  - 删除裸数字形式的 `$.style.push`。
- golden：`splice/dot-rule` 去掉 'undefined' 文本，并因为有了 DIAG 成为 v7。

**P2-02 LowerProgram 与帧** — 来源：T2 S5；MD-04；【性能】
- 要点：
  - codegen 输出 LowerProgram 和洞模块，`lower.mjs` 负责解释。
  - 每个块深度都有帧（语句例外见 D-I10）。
  - `#let` 提升。
  - 静态 abi 导出和程序哈希；`programAbi` 加入握手。
  - SyntaxError 增量隔离和洞模块缓存（D-I11）。
  - 内容参数改为结构化插入。
  - 新增 bench 变体。
  - 新增 LowerProgram 解码器的 fuzz 目标（D-H08）。
  - 修订 v2 §2、v2 §12:251、document-model.md:87。
  - 新建 `docs/lowering-design.md`。
- 验收：LoweringContract 用例全部通过；门禁按 MD-04，不达标时走后备方案并记录偏差。
- golden：48 个 `*.js.txt` 一次性替换为 `*.lower.txt` 加洞模块的 `*.js.txt`；`.ops` 字节不变（G3）。

**P2-03 构造器 ABI 与注册表** — 来源：T2 S6
- 要点：
  - 绑定器的参数种类为 Attr、Projected、Text、Lines、Slot、Body，规格复现今天的签名。
  - 为每个公开 kind 生成构造器，包括 group、table、row、cell、raw、error、collect、field 和 `node()`。**hardbreak/linebreak 除外**（P3-33）。
  - figure 等派生构造器带规格；提供静态和运行时 manifest。
  - 注册表：trampoline、绑定调用覆盖、`next` 委托、深度保护。
  - 区域就是带 Body 参数的构造器；内建项经 `define()` 注册，table/figure 不再按名字分派（`executor.mjs:127-137`）。
  - `#heading(2)[T]`、`#list(false)[a]` 不再错绑。
  - VS Code 的处理器名补全接入 manifest。
- 验收：以下等价 golden 在一次 `$.ctor` 覆盖之前和之后都成立（P6）：`*x*` ≡ `#strong[x]` ≡ m`*x*`；`#!f(H)…#f!` ≡ `#f(H)[…]`。
- golden：无（48 个 `.ops` 字节不变）。

**P2-04 出现级 span 与偏移表传输** — 来源：T2 S7（复核 V5）
- 要点：
  - 洞结果第一次出现时写 SPAN，复用时写 AT（since 8）。
  - `copy()` 实施 span 的包含与继承。
  - 默认 fence 给代码体打上 span。
  - **cooked→raw 偏移表上线**：一个增量 since 行，由执行器根据 T1 的溯源写入，L3 实例化到文本节点上，并有 dump 检查。这是 P4-03（逐项源 span）的前提。
  - 编辑器回归测试：在 `#let` 与对应 splice 之间编辑，只重绘被编辑的段落。
- golden：
  - tree 中的 `@[0,0)` 变为真实 span，html/semantic 增加对应的 `data-s/e`；
  - 含 splice 或区域的 `.ops`，以及偏移表与 span 不同的文本节点，会重录。

**P2-05 通用属性、EXT、DECL、field** — 来源：T2 S9；D-I06、D-I07
- 要点：
  - `role`、`label` 通用化。
  - 新增 `slot`、`syn`、`copy`、`class`、`style` 和 EXT 名值属性。
  - DECL op 的命名空间声明绑定方式（提升或位置）。
  - `$.declare`；`field` kind。
  - 以上全部为 since 9。
- golden：无（没有用例用到新词汇）。

**P2-06 统一参数/区域头/标签/引用语法** — 来源：T1 S7；D-L01、D-L04
- 要点：
  - ArgList：只有具名参数时就是 opts 对象，用于 splice、区域头和 fence 信息。
  - 位置参数或混合参数的头成为 Error 并给出 fix-it，取代 P0-05 的过渡诊断。
  - Markdown 式的 fence 信息。
  - 只有一个 LabelChar 类。
  - ` <id>` 后缀可用于区域、fence 开启行和每个数学岛（依赖 P2-05 的通用 label）。
  - 诊断：`label-orphan`、`label-conflict`、`label-like-text`。
  - `@[a, b]` 解析为 IdList。`@id[..]` 和 `@[label][..]` 在本步只做解析（D-L01），降级在 P2-09 接通。
- golden：现有不变（lint 只出诊断）。

**P2-07 语义声明、事件与公开语义构造器** — 来源：T3 S2
- 要点：
  - 新增 kind：`event`、`entry`、`slot`、`when`、`each`。
  - stdlib：`$.element`、`$.counter`、`$.counter.system`、`$.collector`、`counterUpdate`、`slot`、`when`、`each`、`ref(target, {form, supplement})`、`collect(spec)`、`entry()`。
  - `$.labels.import` 先做占位（P3-31 实现）。
  - `#bibliography` 原位返回它的 collector。
  - 宿主的 `semantics` 节产生同样的记录。
  - 修订 document-model §2.1/§4/§5/§11、v2:238 和 notes-design。
- 验收：
  - HoTT 风格用例：定理与引理共用计数器并按章编号，`@thm` 显示 "Theorem 2.10"；
  - 附录使用字母编号；
  - 同等地位：在文档里声明 figure 行，改名后输出除名字外相同。
- golden：cite 的 bibliography span 改为调用处（有意变化）。

**P2-08 样式线格式变更（唯一一次 MIN_COMPAT 提升）** — 来源：T4 M5；MD-03
- 要点：
  - 退役 Styling.bits：
    - weight、italic、decoration 成为属性行；
    - size 改为 {absPx, mul}（D-T01）；
    - fontRole 取代 CLS_CODE；
    - CJK 改由 T5 分类后传给 `faceOf`；
    - 去掉 CLS_LINK；
    - CLS_SUP 拆成 `baseline:'super'` + `size 0.7em` + `attach:'prev'` + role `fn-marker`，emit 改读 attach（`emit.cc:89`）。
  - STYLE_PUSH 改为携带 delta 节点。
  - 区域的 `style:` 并入节点自身的 delta，取代元参数嗅探。
  - `foldTokens` 设置 class `tok-<tag>`；代码注释改用 `code.hang`，不再比较颜色字符串（`emit.cc:618-622`）。
  - **本步提升 MIN_COMPAT**，此后 P2 内不再提升。
- golden：
  - 48 个 `.ops` 全部重录；
  - style/patch 的 styled 包装消失；
  - 6 个 figure 用例的题注段落显示 role caption；
  - 3 个 notes 用例的标记显示 role fn-marker；
  - code/hang 的 ast/js/ops/tree 变化；
  - html/blocks/breaks 不变。

**P2-09 结构化引用** — 来源：T3 S3；D-L01
- 要点：
  - `@[a, b]` 成为父 ref 带子 ref。
  - `@x[…]` 和 `@[x][…]` 成为 `extra`，由元素行的引用模板决定是替换前缀词还是作为定位附注。
  - refGroup 统一渲染分组引用和单个引用。
  - `form` 参数生效；支持区间压缩。
- golden：tree/html 不变；`cite/basic.ops` 重录。

**P2-10 软换行上线** — 来源：T1 S13（按整合裁决改为 U+000A）
- 要点：
  - 行内模型文本里的换行就是软换行，在模式中写明；code/verbatim 不受影响。
  - emit 用从 `inline.cc:48-59` **原样**提取到 `support/` 的 cjkish 谓词决定如何连接。
  - 删除 `inline.cc:48-59`。
- golden：9 个多行用例的 ast/js/.ops 变化；tree/blocks/html 不变。

**P2-11 区域无损溯源与层级范式** — 来源：T1 S9 + T2 S8（原子落地）；D-L05
- 要点：
  - 删除 splitCells、Row/Cell、codegen 的 Row 分支（`codegen.cc:157-193`）和 regionJoin。
  - 区域体走普通降级；`body.rows()`/`body.blocks()` 取代嵌套数组。
  - 段落带 ParaProv：Sep/Join 切点，作为 JS 侧的影子元数据。
  - 显示数学一律写成 `mathblock`，放置由层级范式决定，取代按 AST 形状的窥孔（`codegen.cc:95-117`）。
  - 新增 N4–N6 规则，只诊断，不拆分。
  - raw/image 的层级在本步改为 Adaptive（复核 R12），避免 N5 产生误报。
- golden：10 个区域用例在所有阶段变化：
  - span 变为真实值；
  - 非表格文本无损；
  - 区域内的显示公式被提升并带上标签；
  - `*a | b*` 成为一个格。

**P2-12 任意位置语句、关键字形式、内容字面量** — 来源：T1 S8 + T2 S12；D-L02
- 要点：
  - Error 降级为 `error` 节点。
  - 统一的作用域规则；含语句的块用 ANF 单元；声明根据 StmtSide.binds 提升。
  - 行内和行首都支持 `#if/else`、`#for`、`#while`。
  - `#let x = [..]` 成为内容字面量（v2 App A:333）。这是行为变化：JS 数组改写为 `#{ let x = [...] }` 或 `Array.of(...)`。写入 tsm-changes。
  - 嵌套的 `#let`/`#{}` 成为嵌套帧中的语句洞，取代 P0-05 的 `statement-nested-unsupported`。
  - 嵌套语句中的 `$.style.push` 由帧弹出并给 `style-in-value`；嵌套的 `$.set` 在 P3-01 之前报错（D-L12）。
  - **`#use` 仍是 `keyword-unsupported`**（D-I08），在 P3-31 实现。
- 验收：
  - 列表项、引用、区域中的嵌套语句；
  - if/else 和 for 生成列表；
  - 重复声明（D-L02）；
  - error 块。
- golden：只新增用例。

**P2-13 片段程序与默认 fence 中的 sidecar** — 来源：T1 S11 + T2 S10；D-L07
- 要点：
  - `tsr2_parse_fragment(_many)` 返回 LowerProgram，洞在带外传递。
  - `m```、`m.parse`、`ctx.m.parse` 走同一个解释器；`m.parse` 支持 `{scope}`。
  - 默认 fence 负责拆分 sidecar，生成 `group{slot:'margin'}`。
  - 删除 `fragment.cc` 和 `Doc::extractSidecars`。
  - 新增片段解码器的 fuzz 目标（D-H08）。
  - 修订 document-model §4.1 和 architecture.md:120。
- golden：sidecar 用例的 tree/blocks/html/semantic/.ops 变化：
  - 出现 margin 槽；
  - span 变精确；
  - margin 里的 `^[..]` 成为真正的脚注。

**P2-14 参考文献就地生成** — 来源：T2 S11
- 要点：
  - `bibliography()` 等待加载完成后，在帧内用 `format` 注册项格式化每一条，在调用处返回 collect 节点。
  - 删除 `finishBibliographies` 和占位段落。
  - `ctx.load` 用最小实现：包装现有 executor 的 `loadResource`，限定在 rootDir 内（P0-11）。P3-21 再改走统一的定位器、缓存和失败策略，接口不变。
- golden：cite 的 `.ops` 顺序变化，tree 不变。

**P2-15 math/mathsrc 节点、洞与数学声明** — 来源：T8 S8（含 T8 S4 的命名部分；复核 V6）；D-L13、D-M02
- 要点：
  - ops 新增 `math`、`mathsrc` 和 `decl{ns:'math'}`。
  - 数学岛降级为逐行的 `mathsrc` 片段加 `__hole`。
  - **带点名字和 `std.` 限定名的词法**（MathDict.byName 和 MathEnv.find 的 `std.` 路径）提前到本步，供生成的 `std.`-限定包装使用。
  - API：`math()`、math``、`math.sym`、`math.call`、`$.math.symbol/op/fn`。`math.equations` 在 P3-29 提供。
  - instantiate 应用 MathEnv，并打上 declEpoch 戳。
- 验收：
  - `$.math.symbol('defeq', …)` 之后，后续公式里的 ≝ 是 Rel；
  - 声明之前出现的脚注不受影响；
  - HoTT 宏（eqv、idtype、prd）。
- golden：含数学的 js 与 tree，以及含数学用例的 `.ops`；mathbox/blocks/layout 不变。

**P2-16 其余 schema 变更**
- 要点：
  - tcell 的体改为 Blocks。
  - 新增 `equations` kind：Block，体为显示数学行（D-S11）。
  - `tag` 成为通用的块槽。
  - 新增 `fill` kind：行内 Glue fil，供 P3-03 的追加型 site（如证毕 ∎）使用（复核 R13）。
  - 每项单独提交；不再提升 MIN_COMPAT（MD-03）。
- golden：无（没有用例用到这些新语义；版本字节只在用到新词汇时变化）。

**P2 阶段结束：** 全部门禁、`--long` fuzz、性能门禁；`docs/tsm-changes.md` 写完 P2 中所有作者可见的变化。

### P3 通用化

**P3-01 级联、规则、默认样式表、NodeProps** — 来源：T4 M6；D-T01、D-T03、D-T07、D-T08、D-L12、D-S09
- 要点：
  - **级联核心：**
    - Selector 扩展到完整键集（role、cls、textLang、depth）；新增 RuleEnv、`$.set`、`style.where`；
    - 嵌套 `$.set` 作用到容器结束（D-L12），配用例；
    - `defaults.json` 作为 env 0；节点上保存 CascadeState。
  - **NodeProps：**
    - 字段：`par.align/hyphenate/indent`、`block.gap/indent/keepWithNext`、`list.marker`；
    - TraitTable 改为由 NodeProps 编译出的视图（复核 I1）；
    - `emit.cc:514/541/697` 改为用块自身的 em 计算（P0-08 遗留）。
  - **emit 不再拼装呈现**（`emit.cc:47-105, 471-541, 551, 624`）。过渡：列表标记由 emit 把局部计数器格式化为 NumberingPattern，P3-03 改为由计数器支撑（复核 I2）。
  - **造节点统一走 `Cascade.make`：** resolver、sidecar、foldTokens、数学文本；脚注用 `Cascade.lift`（D-S09）。
  - **语义页与 CSS：**
    - 语义页序列化器内置最小的 role→元素映射：fn-marker → sup，caption-label/term-name → strong，以保持语义页 golden 不变（复核 R6）；
    - `rulesToCss` 生成流式页样式表，按 role/class 选择的规则待钩子就位后生效（D-T07）。
  - 修订 v2 §12 和 document-model §3。
- golden：
  - 约 22 个 tree 显示有效样式；
  - 3 个 notes tree 增加 note-body 包装；
  - semantic、blocks、breaks、layout、html 不变。
- 新用例：style/precedence、标题内的脚注、嵌套 sup、带样式的 ref、带 keepWithNext 的 role group、嵌套 `$.set`。

**P3-02 全局开关变为作用域属性** — 来源：T4 M7
- 要点：
  - 剩余的 Config 开关按粒度变为属性行：
    - Block：`codeblock.snapKerning/fontFeatures/sidecarFrac/contIndent`、`par.indent`；
    - Run：`text.punct`；
    - Doc：`break.*`、`cost.*`、网格、表格内边距、行距、cjkJustify、cjkGlue。
  - fence 与区域上可设置的参数作为属性别名。
  - 安全栏改为具名 constexpr。
  - 按阶段的设置视图改为生成；未声明的读取在编译期报错。
  - 代码字体特性改为属性（emitter/codeblock-args-in-emit）。
- golden：无。

**P3-03 slot、site、冻结标题克隆、计数器标记** — 来源：T3 S4；D-S01、D-S02、D-S03、D-S05
- 要点：
  - **figure 与表格：**
    - figure 题注标为 `slot:'caption'`，site 附着在这个槽上；
    - 新增三种行：table-figure（自动 kind，D-S01）、figure-table、subfigure（D-S02）。
  - **公式编号：** 编号作为节点值放进 `tag` 槽。兼容用的 `name` 字符串**保留**，直到 P3-26 由布局测量并放置 tag（复核 V3）。
  - **目录与标记：**
    - toc/lof/glossary 使用 cloneTitle（D-S03）；
    - 列表标记由计数器支撑（`SemInfo.number`），取代 P3-01 的过渡做法。
  - **site：**
    - 标题 site 与 display（D-S05）；
    - 追加型 site 和末段 site（证毕 ∎）使用 P2-16 的 `fill`。
  - 新增 dterm 类。
- golden：
  - region/figure、figure/*、pages/paged-doc 的题注段落带上 `slot="caption"`；
  - math/eqref 增加 tag 子节点，name 仍然保留；
  - doc/refs 的 TOC 链接文字被拆开；
  - 语义页的 `<figcaption>` 只包住题注槽的子节点。

**P3-04 身份与 DOM 拼写解耦** — 来源：T3 S5 + T7 S9；D-S06
- 要点：
  - MATERIALIZE 写入 SemInfo 的 anchor/targetAnchor/targetCls/flow，不再写 `url`。
  - AnchorNamer 统一拼写 id 和 href（D-S06）。
  - 只有在注册表中胜出的节点才有锚点。
  - 带标签的 raw 和 rule 也有锚点（S9b）。
  - 前缀暂时固定为 `tsr-`；`render.idPrefix` 设置在 P3-06 随 refPreview 一起开放（复核 R17）。
- golden：tree 中的 `url=` 变为 target（机械变化，脚本核对）；html/semantic 不变。

**P3-05 RenderResult 与提交路径** — 来源：T7 S4；【性能】
- 要点：
  - `tsr_render_result` 返回块表，加上宿主尚未持有的键所对应的 HTML，带 generation 和 anchors 表。
  - 进程级的块 HTML 缓存。
  - `commit()` 取代 `chunkParas`/`patchIn`/`swapIn`。
  - 新增 `handle.offsetAt`/`elementsAt`，VS Code 预览改用它们。
- 验收：e2e 覆盖段落增删补丁和过期键防护；bench 合格。
- golden：无。

**P3-06 shell 核心与 Behavior 注册表** — 来源：T7 S5
- 要点：
  - 核心：每个容器一个会话，并负责核心复制逻辑。
  - Behaviors：
    - refPreview：基于 anchors 表，取代抓取 `#tsr-fn-`；
    - print：本步仍用 A4 字面量加 `page.height`，P3-12 改用 PageSpec（复核 V4）；
    - devAudit。
  - `render.idPrefix` 设置开放。
  - image-dims 改为 T9 capability。
  - CSS 拆成契约 CSS 和 behavior CSS。
- 验收：
  - e2e：弹窗内容、弹窗打开时打补丁、CJK 和连字符文字、编辑中悬停、一个引擎两个文档；
  - 使用非默认前缀时弹窗仍然正常。

**P3-07 分隔符与复制契约** — 来源：T7 S8；D-R01、D-R03、D-R06
- 要点：
  - 每个流只有一个 Sep 生产者。
  - DOM 属性与结构：
    - `data-join` 增加 tab/row/para/custom；
    - 新增 `.tsr-band`；
    - `data-track` 取代 `data-cell`；
    - 新增 `data-ragged`；
    - 代码行带 `data-s`。
  - `audit.mjs` 和 `copy.mjs` 同步修改。
  - 复制策略按 D-R01、D-R03，并更新 e2e 的复制断言。
  - 修订 document-model §9.3 和 pages-design §2/§5。
- golden：约 25 个 html（runner 生成准确清单）和 paged golden。

**P3-08 ParShape 与侧向排除区** — 来源：T6 S5
- 要点：
  - 在保守行带内查询流根的排除矩形；浮动的推进量为 0。
  - 左右浮动可以并存，堆叠宽度任意。
  - 剩余宽度小于 minWrapWidth 时清除。
  - Clear 用完整内容盒计算（D-Y02）。
- golden：figure/float、figure/stack。

**P3-09 LineEnds 取代对齐标志** — 来源：T6 S8；D-Y01、D-Y05
- 要点：
  - LineEnds 预设取代对齐标志。
  - 题注的对齐按 D-Y05 统一。
- golden：
  - 居中与右对齐的行偏移几个 su：题注行，以及 region/table 的 c/r 格；
  - figure/float、figure/stack 的浮动题注改为单行居中、多行两端对齐。

**P3-10 表格布局器** — 来源：T6 S9
- 要点：
  - TableSpec：n × {Fr(1), align}。
  - 格是流根，格内的块内容正常布局。
- golden：region/table* 字节不变。

**P3-11 网格布局器；代码块 + sidecar 两轨表** — 来源：T6 S10
- 要点：
  - `layout/grid.cc` 配 GridParams 数据。
  - 对齐（Snap/Budget）与折行解耦，修复 `wrap:false` 会关闭 snap 的问题。
  - sidecar 降级为两轨表。
  - 代码测量探针改为生成。
  - 修正 grid.h、verbatim-design、code-design §4 中的漂移。
- golden：6 个代码用例不变；snap 加 sidecar 的守护用例按预期变化。

**P3-12 VList 与分页阶段** — 来源：T6 S11；D-Y03、D-Y04
- 要点：
  - 布局器给出分层罚分。
  - `paginate()` 负责：
    - 按 D-Y04 的顺序放宽 keep；
    - 浮动范围；
    - 可移动盒；
    - Inserts；
    - 重复表头；
    - 可见溢出。
  - 本步建好这些机制。可移动盒、表头重复、Inserts 的验收用例分别在 P3-15、P3-14、P3-13 加入，因为它们的输入在那时才有（复核 R15）。
  - shell 的 print 改用 PageSpec，不再写死 A4。
  - 修正 pages-design。
- golden：
  - `pages/paged-doc.paged.txt`：第 3 页不再以孤立的 `}` 开头；
  - P0-01 的分页守护用例。

**P3-13 新集合、flow 与计数器** — 来源：T3 S6
- 要点：
  - 索引与符号表；lof/lot；用户自定义 collector。
  - 作用域 flow（如节末）；按章重置。
  - 键控的引文计数器，加 refsection。
  - 具名的多标记脚注。
  - Deferred 放置，配合分页的 Inserts。
  - counter system；`gap:'one'`。
- golden：只新增用例：HoTT 形态、附录、gap 情形、三标记脚注、分页 Inserts。

**P3-14 作者面特征与表格扩展** — 来源：T6 S12（按整合裁决不设 `$.role`）
- 要点：
  - box、space、keep、break、place、beside、par、align、hyphenate、breaker、media 都作为 T4 属性行；内建构造器的 ParamSpec 可以声明 `aliasOf`。
  - 表格：cols、rules、header；格的 colspan/rowspan/align/valign；Auto 轨道。
  - `raw` 的 measure/minWidth；`#pagebreak()`。
  - `tableBuild` 不再截断多余的格，改为报告。
- golden：无（默认值等于现状）。
- 新用例：theorem、悬挂缩进参考文献、lemma keep、pagebreak、booktabs/合并格/auto 表、超宽表（D-Y09）、分页时表头重复。

**P3-15 通用放置与独立布局** — 来源：T6 S13
- 要点：
  - `layoutDetached` 由浮动、格、InlineBlock 共用。
  - 任意块都可以 place。
  - 页浮动（可移动盒）。
  - 子图使用 InlineBlock。
- golden：只新增用例，包括分页中的页浮动。

**P3-16 几何权威** — 来源：T7 S10；D-Y08
- 要点：
  - margin 由 su 写出；`.tsr-doc` 的 min-height。
  - 标记和行号使用 `Placement{edge=End}`，作为过渡编码；P3-26 起由带碰撞规则的测量位置取代。
  - 页面裁切。
  - error 渲染为 `tsr-err`。
- golden：33 个 html（19.2px → 19.203px，脚本核对）、figure/float、error 用例、paged。

**P3-17 块入行内的拆分策略** — 来源：T2 S8b；D-I02
- 要点：
  - 先在布局与渲染中实现 `para{cont}`：没有首行缩进，段前间距由 layout 的 su 决定为 0（复核 R11：放在几何权威之后）。
  - N5 由只警告改为拆分。
  - 删除 mathblock 的 `INLINE_FALLBACK`。
- golden：只新增用例。

**P3-18 类名渲染与主题拆分** — 来源：T4 M8（复核 R14：排在基线权威之前）
- 要点：
  - 渲染 `tsr-c-*` 类。
  - **新建 `theme.css`**：token 颜色从内联 var() 移到 `.tsr-c-tok-*` 规则。
  - `tsr-pre` 来自 `text.space`。
  - TSR_CSS 缩减为 `contract.gen.css` 加 T7 的模块。
  - `tsr-sqL/R` 改由 T4 的契约生成器按 T5 表生成，直到 P4-04 改为显式 px（复核 I3）。
- golden：6 个 html 和 2 个 semantic（token 类）；11 个 html（`tsr-pre`）。

**P3-19 基线权威** — 来源：T7 S11
- 要点：
  - head.container 带各字体的 content-height 因子，由 `contract.gen.css` 应用。
  - 代码行使用 `tsr-row`。
  - devAudit 检查基线偏差 ≤1px。
- golden：代码用例，以及使用用户字体族的样式用例。

**P3-20 安全评审检查点** — D-H09、D-R09
- 要点：
  - 逐项审查**当前已存在**的面：
    - RawHtml；
    - 元素与属性白名单；
    - 定位器与资源路径（P0-11 的 rootDir 限域）；
    - 已有解码器：ops、settings、RES、LowerProgram、片段。
  - 每项写结论，存入 `docs/security-review.md`；发现的问题在本步修复，并补用例或 fuzz 目标。
  - 后来才引入的面（P3-21 的文档 provider、P3-31 的 inputs 与 `#use`、P5-01 的 `.tsmf`）在引入它们的步骤里做评审补遗。
- 验收：每个现有解码器都有 fuzz 目标，`--long` 无崩溃。

**P3-21 ResourceHost、定位器、引用清单、静态导出** — 来源：T9 M7；D-I09
- 要点：
  - provider 注册表，其中文档 provider 只能提供 codeTokens 和 boxInfo。
  - 定位器带每个源的 base 和请求方类别。
  - `url_policy.def` 同时生成 `safeImageSrc` 和 JS 白名单。
  - LruCache。
  - `$.load`/`ctx.load`（取代 P2-14 的最小实现）；参考文献改为经 `ctx.load` 加载。
  - references 产品；`renderTsm` 返回 {html, diagnostics, ok, manifest, settings, css}。
  - export-static 复制资源，并按 `docinfo` 写 `<html lang>`。
  - 安全评审补遗：文档 provider。
- golden：无（录制器复现 cite/*.ops）。

**P3-22 代码高亮清单与引擎侧 overlay** — 来源：T9 M8
- 要点：
  - `languages.json` 生成 worker、CMake、编辑器和引擎的表。
  - literate overlay 成为引擎变换，通过 profile、fence `set:` 或规则开启，取代全局套用的 cpp 正则。
  - `pbr2tsm` 输出 `cpp-literate`。
- golden：原生 json-hl/tsm-hl 预期不变；新增 overlay 用例。

**P3-23 PresentationMap** — 来源：T7 S12；D-R02、D-Y11、D-L08
- 要点：
  - **中性移植：** `presentation.def` 成为 `engine/data/elements.json` 中的 html 段，并吸收 P3-01 的最小 role 映射；四个投影复现今天的语义页字节。
  - **有意修复：**
    - 题注槽；
    - sidecar 的行内投影；
    - §9.2 的 term → dl/dt/dd 和 collect → nav/section；
    - 语义页样式与排版页的分歧；
    - 白名单；
    - 默认 `data-role`（D-R02）；
    - 只有声明了 `typeset.frame` 的类才有框（D-Y11）。
  - 修订 document-model §9.2。
- golden：
  - 中性移植不变；
  - 有意修复会改 region/figure、figure/*、code/sidecar、style/*、term/collect 的 semantic，以及 group 用例的 html（`data-role`）。

**P3-24 SymbolInfo 身份与数据驱动的数学族** — 来源：T8 S4（带点名字与 `std.` 名字已在 P2-15 完成）；D-M01、D-M04
- 要点：
  - 谓词只读 SymbolInfo。
  - 由 Open/Close 类自动配 lr；`mid()` 降级。
  - `!` 规则加 UCD 否定表。
  - 字母表变体行：bb、cal、frak、bold、sans、mono、italic。
  - 只剥圆括号（D-M01，行为变化，写入 tsm-changes）。
  - 删除 `!word`、`_|_` 分支。
  - 生成转换器的映射表；CI 词法门禁。
  - 在 design-decisions 和 math-design §13 记录语言差异。
- golden：test/golden 预期不变（需验证）。语料重渲染的差异交审阅：54 处条件或集合构造中的竖线获得 Rel 间距。

**P3-25 运算符原子与单一 mlist→item 转换** — 来源：T8 S5
- 要点：
  - 删除 BigOp，Op 原子带 limits 模式。
  - 只做一遍降级。
  - `math.break` 设置表。
  - `fracPadEm` 单独提交。
- golden：
  - break 用例与 mathbox 变化；
  - fracPadEm 使每个分式移动：单独提交，做数值审阅。

**P3-26 数学采用通用协议；公式编号由布局测量** — 来源：T8 S7（含 T6 S15 的单行部分；复核 V3）
- 要点：
  - 删除 `LinebreakBlock::math`、`FlowUnit::K::Math`、`special=4` 和渲染侧的居中逻辑。
  - 新增 `Glue.synthetic`、`data-copy`、`SynKind::Math`。
  - **单行显示公式的 tag：**
    - 由布局测量，作为带碰撞规则的 margin tag Fragment，取代 P3-16 的过渡编码；
    - **删除兼容用的 `name`**（`ArgK::name`）；
    - math/eqref 的 golden 在本步变化。
  - `role=math` 和 `aria-label` 取自 copyText，这一项在 P3-27 落地（复核 I5）。
- golden：
  - html：`data-copy`、显式的显示公式基线、tag 位置；
  - layout：显示公式的跳过；
  - blocks：对象行；
  - 以上按格式分别提交。

**P3-27 语义页数学盒与无障碍** — 来源：T7 S13；D-R04、D-R07
- 要点：
  - 页面 profile 默认输出数学盒，带 `role=math` 和 `aria-label`。
  - 可选的 `a11y.textLayer`。
  - `a11y.mathLabel` 默认开启。
- golden：
  - 7 个数学 semantic golden；
  - 含数学的 html 增加 `aria-label`，单独提交。

**P3-28 宿主测量的替换盒** — 来源：T9 M11 + T6 S14
- 要点：
  - `NEED_BOX{payload, widthSu} → {h, baseline}`；stages.def 增加布局阶段的 NEED 状态和对应的重跑类别（复核 R2）。
  - svg viewBox 的 answerer；measureHtml capability。
  - `ctx.raw(html, {measure:'auto'})`。
  - 图片走 IntrinsicSize 路径；原生 mock 给出确定的答复。
- golden：只新增用例。

**P3-29 数学网格、equations 与多行显示** — 来源：T8 S9 + T6 S15；D-S11
- 要点：
  - Grid：`;` 分行，`,` 分格，`&` 对齐；模板 mat、pmat、cases、aligned。
  - `&` 从运算符组合字符表（`math.cc:99`）中移出，并核对语料。
  - `equations` kind：
    - 交给 T6 的 Replaced 做多行布局，每行有自己的 tag 和碰撞规则；
    - 实现 D-S11 的两条规则：连续的显示数学行，以及岛内 `\` 分行；
    - 新增 `math.equations` API。
  - 新增 HStretch、Delim、Phantom、Class，以及四角 attach。
  - tex2tsm/pbr2tsm 输出这些构造，不再展平；遇到 cancel 给诊断（D-M05）。
- golden：只新增用例；语料中 8 个宽帽公式交审阅。

**P3-30 LocalePack 与文档语言** — 来源：T4 M9；D-T06、D-S08
- 要点：
  - LocalePack 带 likely-subtags 和 CLDR 父链。
  - `$.doc`/`$.locale`（DECL）；`auto` 行。
  - `doc.lang:'auto'` 检测（D-T06）：
    - 在 Phase 0 对 RawOps 进行；
    - 繁体特有字表与简体特有字表放在 locale 数据里；
    - 检测结果由 `tsr2_get('docinfo')` 返回。
  - 宿主侧改动：
    - shell 删掉写死的 `zh-CN`（`shell.mjs:333/356/467`），改用 docinfo；
    - 导出器的 `<html lang>` 也用 docinfo。
  - T3 的术语改为来自 LocalePack。
- golden：无（golden 配置固定 zh-CN）。新用例：英文文档自动得到 "Figure"；日文得到 "図"；繁体得到 "圖"。

**P3-31 跨文档标签、项目驱动、`#use`** — 来源：T3 S7 + T9 M10；D-S07、D-I08
- 要点：
  - `inputs.def` 与 `tsr2_set_input`；`fuzz_inputs`。
  - labels 产品。
  - `runtime/src/node/project.mjs` 和 `tools/tsm-project.mjs`：
    - 先跑 Pass A 求起始值，再跑 Pass B；
    - 必要时再加一轮，上限 3 轮，超出给 `project-unstable`；
    - 读取 `tsm.project.json`。
  - **`#use` 实现**（D-I08）：`__use` 经 `host.load` 和 URL 内容哈希加载；修订 architecture.md:143。
  - `$.labels.import` 的真实实现；external 条目。
  - 安全评审补遗：inputs 与 `#use`。
- 验收：
  - 双执行差分（D-I08）对所有用例成立；
  - 多文档用例。
- golden：只新增多文档用例。

**P3-32 boxInfo 在布局阶段消费；宽度依赖的编译期证明** — 来源：T9 M12 的剩余部分
- 要点：
  - boxInfo 在 Layout 阶段消费。
  - 借助 P3-02 生成的按阶段设置视图，在编译期证明 emit 不再读取 viewport.width。Affects 的修改已在 P1-16 完成。
- golden：无。

**P3-33 正文防护、自动链接、转义、硬换行** — 来源：T1 S10（单提交落地）
- 要点：
  - 防护：`#`、`@` 前紧贴标识符时不生效；`*`、`_` 在词内不生效。
  - url Verbatim 行。
  - 可转义字符为全部 ASCII 标点。
  - `\` 加行尾 → 公开的 `linebreak` 构造器 → `Penalty(Forced)`，与 P1-13 已建立的 hardbreak 映射使用同一个 Forced 罚分。转义与构造器在**同一个提交**中落地（整合裁决）。
  - 合并相邻的 Text。
- golden：doc/url-break（斜体去掉，链接加上）、inline/emph、figure/pull-diag。

**P3-34 描述列表** — D-L08
- 要点：
  - `syntax.def` 新增块规则 `/ term: desc`，属于列容器。
  - `terms` 构造器；dterm 类（P3-03）。
  - layout 默认 run-in 加悬挂缩进；语义页输出 dl/dt/dd（P3-23）。
  - converters 改为输出该语法，不再模拟。
  - 写入 tsm-changes。
- 验收：新用例覆盖所有阶段；tree-sitter/TextMate 的生成产物同步（G9）。

**P3-35 打印器、转换器套件、front matter、语料重转** — 来源：T1 S12；D-L09
- 要点：
  - `tsm-print.mjs`、`escapeTsm`（接入 `syntax.gen.json`）。
  - 共享的转换器套件，含真正的 HTML 实体解码器。
  - translate-tsm 改为按 span 屏蔽，并做 AST 等价校验。
  - `FrontEndOptions.frontMatter`。
  - 重新转换 `examples/real-world` 并审阅差异。
- golden：引擎 golden 无。

**P3-36 导出包** — 来源：T7 S14
- 要点：
  - `renderTsm` 返回 RenderResult、设置、清单和 CSS。
  - exportStatic 增加模板函数、水合、资源处理和 profile。
  - 博客需要的配合改动写入 REPORT（MD-07）。
- golden：无；更新 Node 导出的冒烟测试。

**P3-37 ABI 收尾与文档修订** — 来源：T9 M9
- 要点：
  - `tsr2_get` 提供全部产品，包括诊断 JSON；VS Code 删掉 `preview.js:128` 的正则。
  - 修订以下文档：
    - architecture §2.1、§2.4、§2.5、§4.1；
    - document-model §6.4、§7、§10、§11；
    - v2 §6、§9、§11.1；
    - code-design §3、§5；
    - figure-design §3 与 §8；
    - `mock.h:1-2`；
    - CLAUDE.md 中的 "ops v5"。
- golden：无。

**P3 阶段结束：** 全部门禁、`--long` fuzz、性能门禁。

### P4 成形波次（CJK 与拉丁排版的变化集中于此）

**本阶段每步必做：**
- `tools/rules-diff` 报告；
- `tools/review-corpus.mjs --diff` 审阅；
- e2e 审计矩阵：line-integrity、right-edge ≤1px、overflow、copy；
- 有意的排版变化逐项写入 `REPORT.md`。

**审阅标准**（取代"CJK 负责人签字"）：
- 禁则零违例：行首没有避头字符，行尾没有避尾字符；
- 中西文间距一致；
- 标点挤压符合 clreq；
- right-edge 偏差不比之前差。

P4 按 T5 自身的顺序（4、5、6、7、8、9、10、11）执行（复核 R10：attach 需要步骤 7 的空白 px）。

**P4-01 按 run 实例成 run** — 来源：T5 步骤 4；D-X08
- 要点：
  - paint 按 (face, link, SynKind, copyText, RealizeClass, anchor) 合并 run，取代 P0-10 的过渡键。
  - 行内代码为 Rigid，`word-spacing:0`。
  - KernCtx 使用同一套 run。
- 验收：87K 文档的 DOM 节点数增长 ≤10%。
- golden：cite/*、notes/*、doc/structure、inline/fence-edge、pages/paged-doc 的 html。

**P4-02 段落级成形器** — 来源：T5 步骤 5；D-L03
- 要点：
  - 用字素簇流取代逐节点的状态机，覆盖：
    - autospace；
    - 禁则；
    - 跨样式、链接、代码、对象边界的断点；
    - 撇号与成对引号的判定；
    - 定义宽度；
    - 紧急扫描。
  - 软换行改由 TextRules 的 `joinsWithoutSpace` 判定，取代 P2-10 提取的谓词。
  - 删除旧的 emit 路径。
- golden：约 13 个有文字体系边界的用例，以及全部 hlist。

**P4-03 逐项源 span** — 来源：T5 步骤 6（依赖 P2-04 的偏移表）
- 要点：每个原子带 (srcStart, srcEnd)；每个 run 实例带 `data-s`，每行带 `data-e`。
- golden：大部分 html 的 `data-s` 变化。这是机械变化，用脚本检查单调性和切片是否正确。

**P4-04 TextProps v1；标点、空白、autospace 数据化** — 来源：T5 步骤 7
- 要点：
  - **前置（本步先做）：** 若还没有 Playwright 标点矩阵，先新建 `test/e2e/punct.spec.mjs`：
    - 覆盖各类全角标点及其组合（开括号、闭括号、句读、连接号、省略号）；
    - 在 4 个 dsf 下测量实际渲染出的间隙，与引擎预测比对，误差 ≤1px；
    - 先用当前代码录下基线，再改实现。
  - hyphens、overflowWrap、punct、autospace、whiteSpace 成为属性。
  - 标点空白用显式 px，取代 `tsr-sqL/R` 类（整合裁决：T5 胜出）。
  - DefinedAdvance 行取代对 U+2014/U+2026 的特判。
- golden：所有 CJK html（类改为内联 margin）；cjk/punct* 可能各移动 +1su。

**P4-05 UCD 字符类（RULES_VERSION 1）与 Unicode 控制符** — 来源：T5 步骤 8；D-X04
- 要点：
  - 默认包从 compat 切换到 und、en、zh-Hans。
  - 新增覆盖：
    - 谚文；
    - 〖〗｟｠・ー々；
    - 小假名；
    - 〜；
    - ZW/WJ/GL/NNBSP/SHY 控制符。
  - 字距资格按 CC 判定；网格宽度类取 EAW。
  - `tools/rules-diff` 只允许清单内的边界变化。
- golden：inline/quotes、style/kern-boundary，以及 rules-diff 列出的用例；更新 `test/golden/RULES`。

**P4-06 连字注册表、ExHyphen、hyphens/overflowWrap** — 来源：T5 步骤 9；D-X02、D-X05、D-X09
- 要点：
  - HyphenDict 按 BCP-47 组织；Disc 的 pre 取自 hyphenChar，并标 `data-syn='hyphen'`。
  - ExHyphen 开启，罚分 50。
  - noHyphen 改为属性；标题、题注、行内代码可紧急断行；URL 用 Chicago 规则。
  - 非 en 的词典走资源行。
- golden：
  - doc/hyphen、doc/url-break、cite/*；
  - 标题和题注中的长 token；
  - 含连字复合词的用例；
  - 长的行内代码。

**P4-07 attach 语义；脚注附着移出解析器** — 来源：T5 步骤 10 + T1 S14
- 要点：
  - 成形器把 `attach:'prev'` 应用到作用域的第一个原子，并把前一个字形尾部的 Blank（P4-04 起为显式 px）挪到标记之后。
  - 删除 `emit.cc:89-90` 和 `inline.cc:384-397`。
  - 用户可以使用 `attach()`。
- golden：notes/* 的 ast/js/tree/blocks/breaks/html：标记紧贴标点，并修复 `' . end'`。

**P4-08 原生项断行器与统一伸缩模型** — 来源：T5 步骤 11 + T6 S16；D-X01
- 要点：
  - 删除 fuseLegacy、capSu 和 LinebreakBlock 适配器，断行器直接读 T5 的 item。
  - 容量 = Σ权重 × juSu；重新校准 shrinkThreshold。
  - paint 按 PaintFit.ratio × 原始伸缩量实现 glue；layout 和 render 不再测试任何 `BF_*`。
  - 修订 App C。
  - 退役 blocks golden，改用 hlist。
- golden：两端对齐的用例（尤其是 CJK）的 breaks/layout/html 大面积变化。逐用例审阅，写入 REPORT，并按本阶段的审阅标准验收。

**P4 阶段结束：** 全部门禁、`--long` fuzz、性能门禁，以及语料审阅报告。

### P5 剩余审计条目与收尾

**P5-01 多字体数学链与宿主数学字体** — 来源：T8 S10；D-M06
- 要点：
  - 宿主的 `.tsmf` 作为声明输入，按哈希在进程范围内缓存。
  - `fuzz_tsmf` 加上安全评审补遗。
  - `math.fonts` 字体链：常量取自主字体，主次字体不一致时给警告。
  - 一个字体清单同时服务静态导出与 pack-dist。
  - 修订 v2 §9。
  - 参考墨迹默认关闭（D-M03）。
- golden：Euler 覆盖范围内的输入不变；新增多字体用例。

**P5-02 收尾**
- 要点：
  - 逐条核对 `TRACEABILITY.md`：369 条全部为 done 或 kept；26 个回归用例全部通过。
  - XFAIL 清空。
  - 架构 lint 的规则全部启用。
  - 同等地位用例覆盖每个注册表：element、counter、collector、ctor、region、fence、format、math family、locale、provider、behavior。
  - 双执行差分对全部用例成立。
  - 全部门禁，`--long` fuzz。
  - 终点性能门禁（§4.5）。
  - 完成 `docs/tsm-changes.md`。
  - 写 `REPORT.md`：完成情况、偏差、性能曲线、排版变化审阅、博客配合事项、后续建议（§9 的可选项）。

---

## 7. 高严重度缺陷 → 步骤 → 回归用例

| # | 缺陷 | 位置 | 修复步骤 | 回归用例（新） |
|---|---|---|---|---|
| 1 | 任意 JS 异常或 SyntaxError 都会让整篇失败 | `codegen.cc:214-229`，`worker.mjs:220-222` | P0-05 → P2-02 | `exec/contain-*.tsm`（P0-05 的 11 种输入） |
| 2 | `#if/#for`、`#let x=[..]` 生成非法 JS 且没有诊断 | `inline.cc:153-217`，`codegen.cc:67-94`，`linepass.cc:358-373`，`codegen.cc:215-219` | P0-05 → P2-12 | `exec/keyword-forms.tsm` |
| 3 | 嵌套的 `#let`/`#{}` 被静默丢弃 | `codegen.cc:198-200` | P0-05 → P2-12 | `exec/nested-stmt.tsm` |
| 4 | `contiguous()` 越界导致内容重复 | `inline.cc:68-72` | P0-04 → P1-06 | `inline/overrun-*.tsm` |
| 5 | 行内 `%--` 遮不住块标记 | `inline.cc:240-255` | P1-08 | `line/comment-own.tsm` |
| 6 | 未闭合的 `#let` 吞掉余下全文；未闭合 `#{` 的残缺 JS 使整篇失败 | `linepass.cc:364-366`，`linepass.cc:376-381`，`codegen.cc:220-223` | P0-04/P0-05 → P1-07 | `line/let-unclosed.tsm` |
| 7 | 31 个构造器名不能被绑定 | `codegen.cc:209-212` | P0-05 → P2-02 | `exec/let-ctor-name.tsm` |
| 8 | CRLF 改变语法 | `inline.cc:68-70`，`linepass.cc:148, 195, 214`，`inline.cc:260`，`source.h:24-28` | P0-04 → P1-06 | `line/crlf.tsm` |
| 9 | 括号匹配器不感知岛 | `inline.cc:114, 205, 332, 387-391, 477-486` | P1-06 | `inline/bracket-island.tsm` |
| 10 | 区域按竖线切分，并绕过块降级 | `inline.cc:454-508, 601-627`，`codegen.cc:95-107, 157-193`，`executor.mjs:88, 94-121` | P0-04 → P2-11 | `region/aside-pipe.tsm`、`region/hott-row.tsm` |
| 11 | span 不是出现级的 | `ops.cc:179-185`，`opbuf.mjs:96-103` | P2-04 | `splice/reuse-span.tsm` |
| 12 | 指数级实例化 | `model.cc:36-54` | P0-07 | `ops/dag-bomb.ops` |
| 13 | CSS 注入 | `typeset_html.cc:44-71`，`semantic_html.cc:66-80` | P0-06 | `style/inject.tsm` |
| 14 | 块位置的行内内容、行内图片被静默丢弃 | `emit.cc:170-172, 822-824` | P1-13，P2-11，P3-17 | `inline/image-inline.tsm`、`level/*.tsm` |
| 15 | 脚注内的引文序号错误 | `resolve.cc:234-241, 370-372` | P0-09 | `cite/in-note.tsm` |
| 16 | relayout 沿用旧宽度的 emit 结果 | `doc.h:392`，`emit.cc:721`，`worker.mjs:228-260` | P0-11 → P1-16 | e2e `resize-image` |
| 17 | emit 的 em 忽略 sizePx | `emit.cc:251` | P0-08 | `style/patch`（golden） |
| 18 | `BREAK_INF`（float）< `INF`（double） | `emit.h:53`，`break.cc:9, 48, 107` | P0-12 | `break/forbidden.tsm` |
| 19 | 无可行断点时整段塌成一行 | `break.cc:103-122` | P0-12 | P0-01 的双 URL 守护用例 |
| 20 | 复制时丢掉引文旁的正文 | `typeset_html.cc:454, 584-590` | P0-10 → P4-01 | e2e `copy-cite` |
| 21 | snap-kerning 写出两个 style 属性 | `typeset_html.cc:416-421` | P0-10 | `code/snap.tsm` |
| 22 | 语义页脚注 id 悬空 | `semantic_html.cc:205-212` | P0-09/P0-10 | 契约检查（锚点闭包） |
| 23 | 数学函数名、重音名遮蔽同名符号 | `math.cc:231-235, 437-439, 480-483` | P1-24 | `math/bare-names.tsm` |
| 24 | 作者给的 w 被提供方覆盖 | `doc.h:212-227` | P0-11 → P1-19 | `figure/w-only.tsm` |
| 25 | worker 没有按文档串行化 | `worker.mjs:228-260, 269-280` | P0-11 | e2e `two-docs` |
| 26 | DP 平局取决于哈希表的迭代顺序 | `break.cc:57-58, 89-94` | P0-12 | native 与 WASM 断点对比 |

---

## 8. 风险与对策

| 风险 | 对策 |
|---|---|
| LowerProgram 达不到性能门禁 | 走 MD-04 的后备方案（卫生的打印 JS），两种实现跑同一套 LoweringContract 用例 |
| 大面积 golden 抖动掩盖回归 | 结构与行为分开提交；机械变化用脚本核对；P4 用 rules-diff 加语料审阅 |
| 单步太大，长时间不绿 | 允许拆成多个子提交，每个子提交都必须绿（标明单提交的除外）；实在不行按 §4.7 处理 |
| P2 线格式波次中途失败 | 波次内每步各自为绿；MIN_COMPAT 只提升一次；`.ops` 随时可以重录 |
| 子系统间接口理解偏差 | 以 `design/INTEGRATION.md` 中的共享机制契约为准；偏差记入 PROGRESS |
| WASM 与 native 结果分歧 | `-ffp-contract=off`（P0-12 起）、平局全序；e2e 比对 tsrc 的输出 |
| 上下文压缩后丢失进度 | 用 PROGRESS.md 加 git log 续做（§4.6） |
| 博客与新引擎不兼容 | 只读语料回归（G6）；配合事项写进 REPORT，不改博客仓库 |
| 安全面扩大（provider、`#use`、RawHtml、新解码器） | P3-20 评审，引入步骤做补遗，每个解码器配 fuzz；provider 白名单 |
| 门禁耗时过长 | G8 每步每个目标只跑 10 秒，`--long` 只在阶段末跑；`gate.sh --quick` 只跑相关门禁 |

---

## 9. 非目标与保留特设

**有意保留**（来源：`design/INTEGRATION.md` §Keep special；TRACEABILITY 中的状态记为 `kept:`）：
- 渲染 kind 集合封闭；关键字形式封闭；
- CJK 优先的词法取舍；
- 片段不对带 JS 参数的 splice 求值；
- STYLE_PUSH/POP_TO 是唯一的作用域原语；
- MAKE_TEXT 是专用 op；
- 基础构造器封存；
- 引擎只解析两种小语言；
- resolve 之后不再运行 JS，也没有 resolve↔paginate 不动点；
- CSL、排序、参考文献格式留在用户态；
- 只有一个标签命名空间，靠形状保留别名；
- 数学核心固定；
- 代码网格采用贪心算法；
- 不允许用户写布局算法；
- 屏幕视为一张无限长的页；不做多栏、竖排、RTL；
- 绘制原语和白名单封闭；
- 语义后端在测量前遍历；
- mock 分类器是冻结的副本；
- 安全栏是 constexpr；
- 没有估计状态，也没有依赖图；
- front matter 由宿主处理；
- 表格行不拆分；列表标记不测量。

**可选扩展**（不属于审计问题，不在本次 goal 范围内，写进 REPORT 的后续建议）：
- T5 步骤 12：文档裁剪、hbox、ja/zh-Hant/ko 的完整包；
- 边注与页边 figure；
- 配置声明的行内语法行（已由 D-L06 否决）；
- MathML feed；
- 页码引用；
- PDF 后端；
- 增量前端（D-H04）；
- 删除旧的 `tsr_*` ABI。

---

## 附录 A：主题步骤 → 计划步骤

| 主题 | 映射 |
|---|---|
| T1 | S1→P0-02，S2→P1-05，S3→P1-06，S4→P1-07，S5→P1-08，S6→P1-09，S7→P2-06，S8→P2-12，S9→P2-11，S10→P3-33，S11→P2-13，S12→P3-35，S13→P2-10，S14→P4-07，S15→不采纳 |
| T2 | S1→P0-06，S2→P1-01，S3→P0-07，S4→P2-01，S5→P2-02（过渡 P0-05），S6→P2-03，S7→P2-04，S8→P2-11，S8b→P3-17，S9→P2-05，S10→P2-13，S11→P2-14，S12→P2-12 |
| T3 | S0→P0-09，S1→P1-10，S2→P2-07，S3→P2-09，S4→P3-03，S5→P3-04，S6→P3-13，S7→P3-31 |
| T4 | M0/M1→P0-08，M2→P1-02（校验子集 P0-06），M3→P1-03，M4→P1-04，M5→P2-08，M6→P3-01，M7→P3-02，M8→P3-18，M9→P3-30 |
| T5 | 1→P1-11，2→P1-12，3→P1-13，4→P4-01，5→P4-02，6→P4-03，7→P4-04，8→P4-05，9→P4-06，10→P4-07，11→P4-08，12→可选 |
| T6 | S0→P0-01，S1→P0-12，S2→P1-14，S3→P1-15，S4→P1-16，S5→P3-08，S6→P1-17，S7→P1-18，S8→P3-09，S9→P3-10，S10→P3-11，S11→P3-12，S12→P3-14，S13→P3-15，S14→P3-28，S15→P3-29（单行部分 P3-26），S16→P4-08 |
| T7 | S1→P0-01，S2/S6/S7→P0-10，S3→P1-18，S4→P3-05，S5→P3-06，S8→P3-07，S9→P3-04，S10→P3-16，S11→P3-19，S12→P3-23，S13→P3-27，S14→P3-36 |
| T8 | S0→P0-04，S1→P1-22，S2→P1-23，S3→P1-24，S4→P3-24（命名部分 P2-15），S5→P3-25，S6→P1-25，S7→P3-26，S8→P2-15，S9→P3-29，S10→P5-01 |
| T9 | M0/M1→P0-11，M1F→P0-03，M2/M3→P1-03，M4→P1-19，M5→P1-20，M6→P1-21，M7→P3-21，M8→P3-22，M9→P3-37，M10→P3-31，M11→P3-28，M12→P1-16 与 P3-32 |

## 附录 B：基线（2026-10-05，`main` @ `ecc3a89`）

- native golden：48 个用例 0 失败（`./engine/build/tsr_tests .`）。ASan/UBSan 调试构建同样为 48/0，没有 runtime error。
- 录制：all recordings current。
- 语料：199 通过，0 findings。
- e2e：264 通过（4 个 dsf 项目，6.1s）。
- 性能：见 §4.5。
- 工具链：
  - clang 21.1.8（`/usr/bin/c++` 即 clang）；
  - emsdk（`~/emsdk`，WASM 构建约 3 秒）；
  - Node 26.1（ICU 78.3 / Unicode 17.0）；
  - Python 3.14（UCD 16.0）；
  - Playwright 1.62；
  - libFuzzer 可用，可以访问 unicode.org。

## 附录 C：步骤依赖（直接前置）

所有步骤都隐含依赖 P0-00 和 P0-01。§4.7 跳过 blocked 步骤时，按本表的**传递闭包**判断。

| 步骤 | 依赖 |
|---|---|
| P0-02 | — |
| P0-03 | — |
| P0-04 | P0-02 |
| P0-05 | P0-04 |
| P0-06 | — |
| P0-07 | P0-06 |
| P0-08 | P0-06 |
| P0-09 | — |
| P0-10 | P0-06 |
| P0-11 | — |
| P0-12 | — |
| P1-01 | P0-06 |
| P1-02 | P0-06, P0-08 |
| P1-03 | P1-02, P0-11 |
| P1-04 | P1-03 |
| P1-05 | P0-02, P1-01 |
| P1-06 | P1-05, P0-04 |
| P1-07 | P1-06 |
| P1-08 | P1-07 |
| P1-09 | P1-08, P1-03 |
| P1-10 | P1-03, P0-07, P0-09 |
| P1-11 | — |
| P1-12 | P1-11, P0-12 |
| P1-13 | P1-12 |
| P1-14 | P0-12 |
| P1-15 | P1-14, P1-03 |
| P1-16 | P1-15 |
| P1-17 | P1-16, P1-13, P1-04 |
| P1-18 | P1-17, P1-10 |
| P1-19 | P1-04, P1-03 |
| P1-20 | P1-19, P1-13 |
| P1-21 | P1-19, P1-14, P1-09 |
| P1-22 | P1-11 |
| P1-23 | P1-22, P1-04 |
| P1-24 | P1-22 |
| P1-25 | P1-24, P1-20 |
| P2-01 | P1-01 |
| P2-02 | P2-01, P0-05, P1-08 |
| P2-03 | P2-02, P0-06 |
| P2-04 | P2-03, P1-08 |
| P2-05 | P2-03 |
| P2-06 | P2-05, P1-08 |
| P2-07 | P2-05, P1-10 |
| P2-08 | P2-05, P2-03, P1-11, P1-04, P1-02 |
| P2-09 | P2-06, P2-07 |
| P2-10 | P2-08, P1-11 |
| P2-11 | P2-10, P2-03, P1-07, P1-13 |
| P2-12 | P2-11, P2-02 |
| P2-13 | P2-11, P2-02 |
| P2-14 | P2-07, P2-02 |
| P2-15 | P2-05, P2-06, P2-02, P1-24 |
| P2-16 | P2-08 |
| P3-01 | P2-08, P2-03, P2-13, P2-14, P1-10, P1-18 |
| P3-02 | P3-01, P1-03 |
| P3-03 | P3-01, P2-16, P1-18 |
| P3-04 | P3-03, P1-10, P0-10 |
| P3-05 | P3-04, P1-18, P1-01, P0-11 |
| P3-06 | P3-05 |
| P3-07 | P3-05, P1-17 |
| P3-08 | P1-18, P1-15 |
| P3-09 | P3-08 |
| P3-10 | P1-18, P3-02 |
| P3-11 | P3-10, P2-08 |
| P3-12 | P1-18, P3-01, P3-06 |
| P3-13 | P3-12, P2-07 |
| P3-14 | P3-01, P3-10, P2-05 |
| P3-15 | P3-14, P1-13, P2-11 |
| P3-16 | P3-12, P1-18 |
| P3-17 | P3-16, P2-11 |
| P3-18 | P3-01 |
| P3-19 | P3-18 |
| P3-20 | P2-02, P2-13, P1-03, P1-19 |
| P3-21 | P3-20, P1-19 |
| P3-22 | P3-21, P1-21 |
| P3-23 | P3-03, P3-14, P3-20, P3-01 |
| P3-24 | P2-15, P1-24 |
| P3-25 | P3-24 |
| P3-26 | P3-25, P3-07, P3-16, P3-03 |
| P3-27 | P3-26, P3-23 |
| P3-28 | P3-21, P3-14, P1-19 |
| P3-29 | P3-26, P2-16, P3-03 |
| P3-30 | P3-01, P3-02, P2-05, P1-10 |
| P3-31 | P3-21, P2-12, P3-20 |
| P3-32 | P3-28, P3-02 |
| P3-33 | P1-13, P0-12, P2-03 |
| P3-34 | P3-03, P3-23, P1-07 |
| P3-35 | P2-03, P1-09, P1-03 |
| P3-36 | P3-21, P3-30, P3-05 |
| P3-37 | P3-36 |
| P4-01 | P2-05, P3-07, P0-10 |
| P4-02 | P4-01, P2-10 |
| P4-03 | P4-02, P2-04 |
| P4-04 | P4-03, P3-02, P3-18 |
| P4-05 | P4-04, P1-11 |
| P4-06 | P4-05, P3-01, P1-19 |
| P4-07 | P4-04, P2-08 |
| P4-08 | P4-07, P3-33 |
| P5-01 | P3-31, P1-23 |
| P5-02 | 全部 |
