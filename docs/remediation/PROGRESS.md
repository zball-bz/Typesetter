# 进度（goal 运行维护）

> 续做方法：先读本文件，再 `git log --oneline remediation/audit-2026-10`，然后从第一个非 done 步骤继续（PLAN.md §4.6）。
> 状态：todo / doing / done / blocked。每步完成后同时更新 `TRACEABILITY.md` 的状态列。被阻塞时按 PLAN.md 附录 C 的依赖跳过（§4.7）。

## 当前位置

- 阶段：P1
- 下一步：P1-01
- 分支：`remediation/audit-2026-10`

## 步骤表

| 步骤 | 标题 | 状态 | 提交 | 日期 | golden 变化 | 备注 |
|---|---|---|---|---|---|---|
| P0-00 | 准备：分支、计划文档、基线、环境脚本 | done | grep:plan P0-00 | 2026-10-05 | 0 | gate.sh/env.sh/bench.sh；bench-edit --runs/--json/relayout；Debug -fno-sanitize-recover |
| P0-01 | 契约检查与守护用例 | done | grep:plan P0-01 | 2026-10-05 | +11 用例（仅新增） | contract.h 五项检查；XFAIL 27 项；tsrc --snap/--base/--page-height（默认 base 16）；review-corpus 340 篇；e2e AUDIT_XFAIL |
| P0-02 | 编译器防护（去掉 default 分支，AST dump 补 Note） | done | grep:plan P0-02 | 2026-10-05 | notes/*.ast ×3（原 golden 错）+2 用例 | -Werror=switch-enum（linepass/inline/fragment/codegen）；lint-arch 15 项基线；App A/B 一致性用例 |
| P0-03 | Fuzz 基础设施 | done | grep:plan P0-03 | 2026-10-05 | 0 | fuzz_opreader/linepass/inline；三处发现已修（strtab 指针溢出、负 bits、缺 bib 条目）并入 test/fuzz 回放；各 3 分钟无发现 |
| P0-04 | 前端越界修复（过渡） | done | grep:plan P0-04 | 2026-10-06 | +7 用例（仅新增） | contiguous/注释扫描限于叶子；CRLF；splitCells 跳过数学/代码；未闭合语句在空行或块起点恢复；CRLF 单测 |
| P0-05 | 执行容错（过渡） | done | grep:plan P0-05 | 2026-10-06 | 全部 js.txt；line/*-unclosed-diag 出现 error 块；+15 用例 | 模块：外层用户名 + 内层参数 __ 别名；仅直接执行用户 JS 的单元加帧；__region/__fence 内部容错；SyntaxError 标记二分；ingest 扫描 script-error 诊断；87K compile+execute +1.6% |
| P0-06 | 模式抽取、读取器校验、样式值校验 | done | grep:plan P0-06 | 2026-10-06 | +1 用例（style/inject-diag） | schema.json+lock；gen-schema/gen-all（ops.def、schema.gen.*、ops.gen.mjs、schema-table.md）；解码时按域校验；颜色/字体/lang 校验关闭 CSS 注入；类型化访问器；-Werror=switch；G9 启用 |
| P0-07 | 实例化加固（显式栈 + InstLimits） | done | grep:plan P0-07 | 2026-10-06 | 0 | 显式栈 copy + InstLimits（下限 256K，见偏差）；normalize.cc（N1/N2，按 schema level）；删除 isInlineKind；炸弹单测 25.8ms，峰值 35MB |
| P0-08 | 样式卫生与统一 em | done | grep:plan P0-08 | 2026-10-06 | style/patch blocks+breaks | emPx 统一（emit/measure/CSS 同一公式）；MetricStore 键 <<32；Styling 浮点规范化；JS popTo 钳制。$.style.push(number) 的弃用诊断推迟到 P2-01（届时删除该入口） |
| P0-09 | 语义正确性修复 | done | grep:plan P0-09 | 2026-10-06 | notes tree（SUP）、notes/eqref semantic、+7 用例 | 引文按文档序（脚注体在标记处）；分组引用逐键；ref-shadowed/ref-unnumbered；所有带标签节点注册；保留形状；重复标签丢弃；compose 取代绝对样式；#notes() 重复不放置；第二个参考文献克隆无锚；designated init；diags golden 阶段 |
| P0-10 | 渲染正确性修复（HtmlWriter/AnchorNamer、run 键、列表锚点） | done | grep:plan P0-10 | 2026-10-06 | cite/* ×5 + doc/refs-diag html（原 golden 错）；+1 用例（inline/hyphen-link） | html_writer.h：编译期属性白名单、单一 style、流式写出（87K 原生渲染 2.09→1.97ms）；BF_REF 过渡 run 键；连字符在链接内；render-attr 诊断；e2e 引文旁复制、snap 单 style；fuzz 发现（参考文献嵌套收集器无限递归）单独修复 |
| P0-11 | 宿主卫生（worker 串行化、fork 重排、引擎卫生） | done | grep:plan P0-11 | 2026-10-06 | figure/pull-diag diags span（原 golden 错）；+3 用例（figure/w-only、figure/reemit-diag、cite/outside-root-diag） | worker 按 docId 信箱（顺序执行、同类合并、每次 await 后查 generation）；dims RPC 带 rid；图片尺寸并行、按页面 baseUrl 解析、文件头嗅探（含 EXIF 方向）；字体成功才算已加载、迟到清缓存、失败 30s 重试；tokens 语法加载失败重试、literate 偏移用原文；loadResource 限定 rootDir/baseDir；DiagSink 来源切片；setWidth 重新 emit；KP 备忘录校验键 + LRU；provider 边界检查；w-only 按比例。relayout 新基准 6.1/23.0/57.3ms |
| P0-12 | 断行语义包 | done | grep:plan P0-12 | 2026-10-06 | 34 段断点（29 用例，审阅见 REPORT.md）；64 个 breaks.txt 代价数值 | 提交 1：items.h 适配器、-ffp-contract=off、WASM/native 断点一致门禁（95/95）。提交 2：TeX 丢弃、Forbidden、Disc、Forced 段末（末行 fil + 收缩）、代价上限 1e4、i32 千分位罚分、总序平局、最终一遍救援 + overfull-line + data-overfull、布局收缩极限；断点下标取下一行首块；审计 compression 阈值按空格宽度；AUDIT_XFAIL 清空 |
| P1-01 | 版本窗口与 ABI 握手 | todo | | | | |
| P1-02 | 属性注册表（props 行进入共享模式） | todo | | | | |
| P1-03 | 设置文档 ABI、阶段模型、驱动循环、用例配置 | todo | | | | |
| P1-04 | 字体面与根契约 | todo | | | | |
| P1-05 | syntax.def 与 CallAST | todo | | | | |
| P1-06 | SurfaceLexer | todo | | | | |
| P1-07 | BlockAutomaton | todo | | | | |
| P1-08 | 行所有权与内容体 | todo | | | | |
| P1-09 | 前端导出与工具链 | todo | | | | |
| P1-10 | 元素注册表、索引与分阶段解析器 | todo | | | | |
| P1-11 | TextRules 兼容表与单一分类器 | todo | | | | |
| P1-12 | HList 与 run 实例 | todo | | | | |
| P1-13 | InlineObject 注册表与扁平化表 | todo | | | | |
| P1-14 | KP 正式化与校验缓存 | todo | | | | |
| P1-15 | 断行移入布局（ExclusionMap） | todo | | | | |
| P1-16 | 与宽度无关的 emit（SizeSpec） | todo | | | | |
| P1-17 | 统一行物化（materializeLines） | todo | | | | |
| P1-18 | 盒树、布局器注册表、Fragment、DisplayList | todo | | | | |
| P1-19 | 资源表（ResourceTable） | todo | | | | |
| P1-20 | 按段延迟（per-pid deferral） | todo | | | | |
| P1-21 | Session 内容键缓存 | todo | | | | |
| P1-22 | MathDict | todo | | | | |
| P1-23 | MathFont 运行时对象 | todo | | | | |
| P1-24 | MathRow 注册表与 Call IR | todo | | | | |
| P1-25 | 数学惰性布局 | todo | | | | |
| P2-01 | Node 值与 DIAG 通道 | todo | | | | |
| P2-02 | LowerProgram 与帧 | todo | | | | |
| P2-03 | 构造器 ABI 与注册表 | todo | | | | |
| P2-04 | 出现级 span 与偏移表传输 | todo | | | | |
| P2-05 | 通用属性、EXT、DECL、field | todo | | | | |
| P2-06 | 统一参数/区域头/标签/引用语法 | todo | | | | |
| P2-07 | 语义声明、事件与公开语义构造器 | todo | | | | |
| P2-08 | 样式线格式变更（唯一一次 MIN_COMPAT 提升） | todo | | | | |
| P2-09 | 结构化引用 | todo | | | | |
| P2-10 | 软换行上线（行内文本中的 U+000A） | todo | | | | |
| P2-11 | 区域无损溯源与层级范式 | todo | | | | |
| P2-12 | 任意位置语句、关键字形式、内容字面量 | todo | | | | |
| P2-13 | 片段程序与默认 fence 中的 sidecar | todo | | | | |
| P2-14 | 参考文献就地生成 | todo | | | | |
| P2-15 | math/mathsrc 节点、洞与数学声明 | todo | | | | |
| P2-16 | 其余 schema 变更（tcell 块体、equations、tag 槽、fill） | todo | | | | |
| P3-01 | 级联、规则、默认样式表、NodeProps | todo | | | | |
| P3-02 | 全局开关变为作用域属性 | todo | | | | |
| P3-03 | slot、site、冻结标题克隆、计数器标记 | todo | | | | |
| P3-04 | 身份与 DOM 拼写解耦（AnchorId） | todo | | | | |
| P3-05 | RenderResult 与提交路径 | todo | | | | |
| P3-06 | shell 核心与 Behavior 注册表 | todo | | | | |
| P3-07 | 分隔符与复制契约 | todo | | | | |
| P3-08 | ParShape 与侧向排除区 | todo | | | | |
| P3-09 | LineEnds 取代对齐标志 | todo | | | | |
| P3-10 | 表格布局器 | todo | | | | |
| P3-11 | 网格布局器；代码块 + sidecar 两轨表 | todo | | | | |
| P3-12 | VList 与分页阶段 | todo | | | | |
| P3-13 | 新集合、flow 与计数器 | todo | | | | |
| P3-14 | 作者面特征（traits）与表格扩展 | todo | | | | |
| P3-15 | 通用放置与独立布局（InlineBlock/子图） | todo | | | | |
| P3-16 | 几何权威 | todo | | | | |
| P3-17 | 块入行内的拆分策略 | todo | | | | |
| P3-18 | 类名渲染与主题拆分 | todo | | | | |
| P3-19 | 基线权威 | todo | | | | |
| P3-20 | 安全评审检查点 | todo | | | | |
| P3-21 | ResourceHost、定位器、引用清单、静态导出 | todo | | | | |
| P3-22 | 代码高亮清单与引擎侧 overlay | todo | | | | |
| P3-23 | PresentationMap（元素行的 html 段） | todo | | | | |
| P3-24 | SymbolInfo 身份与数据驱动的数学族 | todo | | | | |
| P3-25 | 运算符原子与单一 mlist→item 转换 | todo | | | | |
| P3-26 | 数学采用通用协议；公式编号由布局测量 | todo | | | | |
| P3-27 | 语义页数学盒与无障碍 | todo | | | | |
| P3-28 | 宿主测量的替换盒（boxInfo） | todo | | | | |
| P3-29 | 数学网格、equations 与显示行 | todo | | | | |
| P3-30 | LocalePack 与文档语言 | todo | | | | |
| P3-31 | 跨文档标签、项目驱动、#use | todo | | | | |
| P3-32 | boxInfo 在布局阶段消费；宽度依赖的编译期证明 | todo | | | | |
| P3-33 | 正文防护、自动链接、转义、硬换行 | todo | | | | |
| P3-34 | 描述列表（/ term: desc） | todo | | | | |
| P3-35 | 打印器、转换器套件、front matter、语料重转 | todo | | | | |
| P3-36 | 导出包 | todo | | | | |
| P3-37 | ABI 收尾与文档修订 | todo | | | | |
| P4-01 | 按 run 实例成 run | todo | | | | |
| P4-02 | 段落级成形器 | todo | | | | |
| P4-03 | 逐项源 span | todo | | | | |
| P4-04 | TextProps v1；标点/空白/autospace 数据化 | todo | | | | |
| P4-05 | UCD 字符类（RULES_VERSION 1）与 Unicode 控制符 | todo | | | | |
| P4-06 | 连字注册表、ExHyphen、hyphens/overflowWrap | todo | | | | |
| P4-07 | attach 语义；脚注附着移出解析器 | todo | | | | |
| P4-08 | 原生项断行器与统一伸缩模型 | todo | | | | |
| P5-01 | 多字体数学链与宿主数学字体 | todo | | | | |
| P5-02 | 收尾：残留检查、文档、最终报告 | todo | | | | |

## 门禁记录（每阶段结束）

| 阶段 | G1 | G2 | G3 | G4 | G5 | G6 | G7 xfail 数 | G8 --long | G9 | G10 | 日期 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 基线 | ✓ 48/0 | ✓ 48/0 | ✓ | ✓ | ✓ 264 | ✓ 199/0 | — | — | — | — | 2026-10-05 |
| P0-01 后 | ✓ 59/0 | ✓ | ✓ | ✓ | ✓ 304 | ✓ 199/0 + 340 篇 | 27 | — | — | — | 2026-10-05 |
| P0 结束 | ✓ 95/0 | ✓ | ✓ | ✓ + WASM 断点一致 95/95 | ✓ 480 | ✓ 199/0 + 340 篇 | 30（e2e AUDIT_XFAIL 0） | ✓ 30 分钟（linepass/inline/opreader 各 600s，无新发现；期间修复 1 处：参考文献嵌套收集器无限递归） | ✓ | ✓ | 2026-10-06 |

## 性能曲线（update 模式中位数，3 次取最小；单位 ms）

| 时点 | 7.8K | 35K | 87K | 87K compile/execute/ingest/engine/render | 冷启动 7.8K/35K/87K | relayout | 备注 |
|---|---|---|---|---|---|---|---|
| 基线（审计时） | 3.8 | 11.9 | 28.1 | 1.3 / 4.0 / 0.3 / 6.5 / 6.9 | 83 / 110 / 151 | — | 2026-10-05，单次测量 |
| 基线（P0-00 重录，门禁用） | 3.60 | 12.00 | 28.90 | 1.4 / 4.0 / 0.3 / 6.5 / 7.2 | 47.9 / 71.3 / 111.7 | 5.50 / 22.20 / 58.00 | `tools/bench.sh`，3 次取最小；阶段计时分辨率 0.1ms（worker 中浏览器计时器粗化） |
| P0-10 后 | 3.50 | 10.90 | 27.90 | 1.4 / 4.0 / 0.3 / 6.5 / 6.2 | 47.8 / 70.3 / 108.0 | 5.80 / 21.80 / 56.70 | HtmlWriter 流式写出：87K 原生渲染 2.09→1.97ms |
| P0-11 后（relayout 新基准） | 3.70 | 11.60 | 27.80 | 1.3 / 4.0 / 0.3 / 6.8 / 6.2 | 50.8 / 71.2 / 112.8 | 6.10 / 23.00 / 57.30 | relayout 现在重新 emit（缺陷 #16），此行作为之后 relayout 门禁的基准 |
| P0 结束 | 3.50 | 11.20 | 28.00 | 1.3 / 4.1 / 0.3 / 6.9 / 6.2 | 46.9 / 73.8 / 108.3 | 6.10 / 22.90 / 57.00 | 阶段门禁：update 均优于基线；relayout 按计划以 P0-11 记录为新基准（重新 emit），不劣于该基准 |

## 偏差记录（MD-11）

| 步骤 | 偏差 | 原因 | 影响的后续步骤 |
|---|---|---|---|
| P0-12 | golden 影响大于计划所列（计划 11 用例 12 段；实际 29 用例 34 段） | 计划的测量早于 P0-01…P0-10 新增的守护用例（B、C 类，同一模式）；计划外的已有用例（figure/block、figure/pull-diag pid 2、notes/cjk-glue、math/inline、math/parse-diag）是"末行收缩"与 Disc 语义的直接结果，逐段审阅为改进或中性，记入 REPORT.md | 无；P1-14（活跃表）须保持这些断点 |
| P0-12 | 审计 compression 阈值改为相对空格宽度（不严于旧的 −2.5px）；e2e 增加 EXPECTED_DIAGS（doc/url-overlong、region/hott-row 预期 overfull-line） | 收缩极限是空格宽度的 0.37，18px 等宽字体的合法收缩可达 −4px；内容宽于版心时诊断是正确输出 | P3-07 审计提示统一时并入 |
| P0-07 | D-I03 的节点预算下限从 1M 改为 256K：预算 = max(262144, 64 × 原始节点数)；深度上限 256 不变 | 1M 个 ContentNode 约 90MB，达不到 P0-07 的"峰值内存 < 64MB"验收；64× 原始节点数的项对正常文档仍然宽裕 | P1-03 把它做成 HostOnly 设置时，默认值用 256K |

## 阻塞记录（§4.7）

| 步骤 | 现象 | 已尝试 | 保存位置（stash / wip 分支） | 绕行 |
|---|---|---|---|---|
