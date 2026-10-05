# 进度（goal 运行维护）

> 续做方法：先读本文件，再 `git log --oneline remediation/audit-2026-10`，然后从第一个非 done 步骤继续（PLAN.md §4.6）。
> 状态：todo / doing / done / blocked。每步完成后同时更新 `TRACEABILITY.md` 的状态列。被阻塞时按 PLAN.md 附录 C 的依赖跳过（§4.7）。

## 当前位置

- 阶段：P0（未开始）
- 下一步：P0-00
- 分支：`remediation/audit-2026-10`（尚未创建）

## 步骤表

| 步骤 | 标题 | 状态 | 提交 | 日期 | golden 变化 | 备注 |
|---|---|---|---|---|---|---|
| P0-00 | 准备：分支、计划文档、基线、环境脚本 | todo | | | | |
| P0-01 | 契约检查与守护用例 | todo | | | | |
| P0-02 | 编译器防护（去掉 default 分支，AST dump 补 Note） | todo | | | | |
| P0-03 | Fuzz 基础设施 | todo | | | | |
| P0-04 | 前端越界修复（过渡） | todo | | | | |
| P0-05 | 执行容错（过渡） | todo | | | | |
| P0-06 | 模式抽取、读取器校验、样式值校验 | todo | | | | |
| P0-07 | 实例化加固（显式栈 + InstLimits） | todo | | | | |
| P0-08 | 样式卫生与统一 em | todo | | | | |
| P0-09 | 语义正确性修复 | todo | | | | |
| P0-10 | 渲染正确性修复（HtmlWriter/AnchorNamer、run 键、列表锚点） | todo | | | | |
| P0-11 | 宿主卫生（worker 串行化、fork 重排、引擎卫生） | todo | | | | |
| P0-12 | 断行语义包 | todo | | | | |
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

## 性能曲线（update 模式中位数，3 次取最小；单位 ms）

| 时点 | 7.8K | 35K | 87K | 87K compile/execute/ingest/engine/render | 冷启动 7.8K/35K/87K | relayout | 备注 |
|---|---|---|---|---|---|---|---|
| 基线（审计时） | 3.8 | 11.9 | 28.1 | 1.3 / 4.0 / 0.3 / 6.5 / 6.9 | 83 / 110 / 151 | — | 2026-10-05，单次测量；P0-00 改进 bench 后重录 |

## 偏差记录（MD-11）

| 步骤 | 偏差 | 原因 | 影响的后续步骤 |
|---|---|---|---|

## 阻塞记录（§4.7）

| 步骤 | 现象 | 已尝试 | 保存位置（stash / wip 分支） | 绕行 |
|---|---|---|---|---|
