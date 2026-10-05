# 进度（goal 运行维护）

> 续做方法：先读本文件，再 `git log --oneline remediation/audit-2026-10`，然后从第一个非 done 步骤继续（PLAN.md §4.6）。
> 状态：todo / doing / done / blocked。每步完成后同时更新 `TRACEABILITY.md` 的状态列。被阻塞时按 PLAN.md 附录 C 的依赖跳过（§4.7）。

## 当前位置

- 阶段：P1
- 下一步：P1-11
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
| P1-01 | 版本窗口与 ABI 握手 | done | grep:plan P1-01 | 2026-10-06 | 0（所有缓冲区仍为 v6，字节不变） | 写入器按所用词汇行的 since 定版本（≥MIN_COMPAT）；读取器接受 6..OPS_VERSION 窗口、拒绝新于缓冲区的 op/kind/attr；tsr2_abi() 握手 {opsWindow, schemaHash(FNV-1a 规范 JSON), programAbi/resVersion/syntaxVersion 占位 0, renderVersion 1}，worker 与 Node 渲染器核对；since 表生成到 ops.gen.mjs / kOpSince；CLAUDE.md 与 architecture §3 修订 |
| P1-02 | 属性注册表（props 行进入共享模式） | done | grep:plan P1-02 | 2026-10-06 | +1 用例（notes/sup-emphasis）；其余字节不变 | schema.json 新增 props（运行属性行）与 domains（正则→DFA）两节；生成 Styling/==/哈希/规范化/applyStyleArg/转储字段（props.gen.h）、typeset 的 runCss（style_css.gen.h）、executor 键表 STYLE_KEYS/STYLE_SUGAR、C++ matchDomain DFA（domains.gen.*）与 JS validDomain；删除 values.h（300 万随机串与旧实现 0 差异）；check-domains（JS↔C++ 一致）入 G4/CI；语义页 sup 内嵌 strong/em；docs/style-design.md |
| P1-03 | 设置文档 ABI、阶段模型、驱动循环、用例配置 | done | grep:plan P1-03 | 2026-10-06 | code/snap-sidecar layout+html（原 golden 错：代码行伸进边注栏）；+2 用例（code/nowrap-snap、ref/supplements-en） | 设置文档（schema settings/policy 行生成 Config、编解码、JS 默认值与旧选项映射；tsr2_set_config，旧 setter 为包装）；stages.def/products.def、validThrough、Resolve 独立；configure 的原地/REBUILD/REEXECUTE 规则；tsr2_doc_fork（克隆字符串/样式表、复制度量、重放 token/图片答案），worker relayout/paginate 用 fork；driver.h + ProviderSet，原生 token provider 移入 engine/src/code（查询嵌入构建）；profiles/golden.json + X.fixture.json 取代文件名约定；tsrc --profile/--fixture/--settings/--set，check-tsrc 928 个 golden 全部复现；每个用例 fork==fresh 差分；fuzz_settings；视图以 lint 检查（偏差）；docs/host-protocol-design.md |
| P1-04 | 字体面与根契约 | done | grep:plan P1-04 | 2026-10-06 | 103 个 html/paged 的根行（tools/golden-diff/root-line-only.mjs 核对只有根行变化） | FaceTable/faceOf/FaceKey；度量、vmet、resolveWidths、请求按 FaceId；族解析顺序含 mono×cjk（fonts.monoCjk）；CJK 斜体按直立测量；.tsr-doc 输出 lang、字体角色变量、基础字号；CSS 契约从变量绘制；shell chunkParas 解析根开标签（e2e 补丁测试）；数学文字经 faceOf |
| P1-05 | syntax.def 与 CallAST | done | grep:plan P1-05 | 2026-10-06 | 0（skeleton/ast/js/tree 全部字节不变） | syntax.def（CLASS/INLINE/BLOCK/KEYWORD/RESERVED/TOKEN_TAGS/SUGAR/NODE 行）+ tools/gen-syntax.mjs 生成 syntax.gen.{h,cc}、shared/syntax.gen.{mjs,json}、docs/syntax-table.md（入 gen-all/G9）；CallAST：AstKind {Doc,Text,Comment,Call,Splice,Stmt,Error} + SugarId，节点 32 字节（static_assert），载荷为紧随节点的旁路记录，kids 为 arena 切片；通用 dump 由模板生成；codegen/fragment 按 slot 分派（Para→mathblock 窥孔保留）；reservedSpliceHead、kTokenTags/TOKEN_TAGS 由表生成；tsr2_abi syntaxVersion=1，abi.mjs 核对；AST 字节 87K 116680→64664（−45%），unitAstBytes 守住；原生 parse 0.428→0.405ms；docs/syntax-design.md，PackCC/SourceMap 文档漂移更正 |
| P1-06 | SurfaceLexer | done | grep:plan P1-06 | 2026-10-06 | 0（现有用例全部字节不变）；+3 用例（inline/bracket-island、line/crlf-inline、line/trailing-blank） | LeafText：叶子各行以单个 \n 结构性连接（CRLF/行尾空白/容器前缀不进入词法器），所有扫描以叶子或体为界，偏移映射回原始 span；按 INLINE 行生成的 inlineOpener/kInlineOpenerByte 分派（纯文本成段追加，87K parse 0.43→0.19ms）；syntax/lexer.{h,cc}：lexCodeSpan（多反引号游程）、lexMath（只解码 \$）、lexComment、lexSplice、atomEnd、BracketMatcher（岛感知 + 普通回退，单次扫描记忆化，线性）；内容体/脚注/链接文字跨行；@[ 为 IdList；splitCells 用同一原子；SpliceP 存连接后的 JS 文本；删除 contiguous/plainGap/seekTo/scanSpliceHead；SYNTAX_VERSION 2；链接匹配方式偏差见偏差表 |
| P1-07 | BlockAutomaton | done | grep:plan P1-07 | 2026-10-06 | 容器 span：doc/structure、notes/cjk-glue、region/figure、doc/nested-base16/18、figure/float-in-list、exec/let-ctor-name、exec/nested-stmt-diag、line/crlf-inline、line/trailing-blank（skeleton/ast/js/tree/semantic/html/.ops）；行为：conform/appa-splices、conform/appb-lines、exec/contain-orphan-diag；+5 用例（line/tabs、line/interrupt-diag、line/fence-container-diag、line/blankblock-diag、region/resync-diag） | 容器协议 Prefix/Column/Explicit（tab 宽 4，span 随每个归属行扩展，空行结束引用）；fence/块注释/段内注释为 verbatim carry，容器退出即结束；fence 按内容列相对缩进去缩进，容器内 fence 传 ctx.lineOffsets；区域按名闭合，内层与随容器结束的区域报 region-unclosed，孤立闭合行为 region-orphan 错误块；段落打断逐条规则（空项、N≠1 不打断）；列表身份（标记类别、列）+ list-number 信息诊断；语句平衡到容器退出（结构连接、倍增窗口，线性），失败恢复到空行/块起始/容器退出；--%、}、; 后的行余部重新进入；SYNTAX_VERSION 3；偏差见偏差表 |
| P1-08 | 行所有权与内容体 | done | grep:plan P1-08 | 2026-10-06 | 0（现有用例全部字节不变）；+3 用例（line/own-body、line/own-hide、line/own-math） | 行所有权：段落行用 phase 2 原语扫描，未闭合构造一次性前瞻到结构边界（Leaf：代码 span/数学/splice JS/行内形式体，界=空行或容器退出；Container：行内注释/块形式体，界=容器退出），闭合则其间各行归段落、不起块，否则开符为字面（literalAt + RevertedWindow）；块形式体以 ']' 行（列 ≤ 开行缩进）闭合，记 SkelNode::bodies，']' 后续行与 '][' 续参；内容体（#f[、^[）以 Blocks 模式重入行扫描（linepassLines，公共缩进去除），单段落解包；模型 N3 拼接含块的 seq；多块脚注按块渲染；fuzz_linepass 180s、fuzz_inline 120s 无发现；SYNTAX_VERSION 4；偏差见偏差表 |
| P1-09 | 前端导出与工具链 | done | grep:plan P1-09 | 2026-10-06 | +327 个 tokens/outline/astjson golden（每个用例三份）；code/tsm-hl 的 tree/semantic/html（tsm 块改由引擎着色） | syntax/exports：syntaxTokens（14 标签）、outlineJson（标题/区域/fence/标签/诊断）、astJson（gen-syntax 生成 jsonAstNode）；C ABI tsr_syntax_tokens/tsr_outline/tsr_parse_json，tsrc --stage=tokens|outline|astjson（check-tsrc 1362 个 golden）；tsm 代码块在 Resolve 内由引擎着色，无 NEED_TOKENS 往返；grammar.js 与 TextMate 由 syntax-regex.js（读 syntax.gen.json）生成，tools/gen-grammars.mjs 入 gen-all 并 vendor parser.c，highlights.scm 只留一份；VS Code 扩展宿主内跑引擎 wasm（ABI 校验、UTF-8→UTF-16），token/大纲/折叠/补全来自引擎，tree-sitter 作冷启动回退，删除硬编码区域列表；一致性 (b) 逐字节 88.9%（允许类别表，≥85%），(e) fuzz_inline 覆盖导出（120s 无发现）；code-design §2、editor-design §5 修订 |
| P1-10 | 元素注册表、索引与分阶段解析器 | done | grep:plan P1-10 | 2026-10-06 | 结构性提交 0 变化（全部 tree/blocks/breaks/layout/html/semantic/diags 字节不变）；+111 个 index golden；+2 用例（ref/supplements-ja、ref/supplements-zh-hant）；另见 f93d611（golden 有误：非 figure group 的引用） | engine/src/elements（Registry：selector 成员关系于 instantiate 写入 ContentNode::cls，最具体者胜）；engine/src/semantic（numbering 计数自动机、terms 区域词包 en/zh-Hans/zh-Hant/ja 与回退链、index LOCATE/BIND、materialize 模板/引用/site/收集器/flow，输入不变、输出替换 tree.root）；engine/data/elements.json 内建 heading/table/figure/equation/footnote/term 与 toc/glossary/notes/bibliography 预设；resolve.cc 只剩阶段驱动；删除 applyLang，terms.* 改为逐词覆盖；tsrc --stage=index；替代 elements.json（figure 改名）输出除 index 名外相同（unitRegistry）；lint 基线 15→8；87K update 29.4ms；docs/semantics-design.md |
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
| P1-03 | 按阶段的设置视图先以 lint 机械检查（tools/lint-arch.mjs settings-view-<stage>：阶段源码只能读 affects 含该阶段的 Config 成员），而不是手写 struct 视图 | 手写视图需要改动所有阶段函数签名，P3-02 会再生成一次；lint 由 schema 的 affects 自动推出，达到"读未声明的行即报错"的同一保证 | P3-02 生成 struct 视图并删除该 lint 规则 |
| P1-03 | XFAIL 由 30 增至 31：新用例 code/nowrap-snap 属于已有的"代码行缺 line-spans"类（P3-07 修） | 计划要求新增此用例；该类缺陷已在清单中按用例逐条列出 | P3-07 修复后整类移除 |
| P0-12 | 审计 compression 阈值改为相对空格宽度（不严于旧的 −2.5px）；e2e 增加 EXPECTED_DIAGS（doc/url-overlong、region/hott-row 预期 overfull-line） | 收缩极限是空格宽度的 0.37，18px 等宽字体的合法收缩可达 −4px；内容宽于版心时诊断是正确输出 | P3-07 审计提示统一时并入 |
| P1-06 | 链接文字不用行内栈上的"弱 `[` 帧"，而是与内容体同一个括号计数器预先匹配：先按岛感知匹配，若岛吞掉了闭括号（或其后不是 `(url)`），回退到只认转义的普通匹配，此时体内的岛以该闭括号为界；内容体（`#f[`、`^[`）同样回退 | P0-04 的验收与 golden 要求 `[price $5](u) and $x$` 是链接、其后 `$x$` 是公式，而纯岛优先（含设计中的弱帧）会把 `$5](u) and $` 读成公式；弱帧还会让 `*a [b* c](u)` 的强调抢走链接（CommonMark 中链接优先），并改变今天的文本节点边界。预先匹配用一次扫描记下途经的所有括号，保持线性 | 无；P1-08 的 AtomTape 可直接替换匹配器内部的原子跳过 |
| P1-06 | `@[…]` 的 IdList 只在一行内匹配，`\,` 不作为转义（反斜杠只转义 `]` 等字符本身） | 解析器之后 resolver 仍按逗号切分一个目标字符串，转义逗号无法表示；行内匹配与今天一致，避免病态输入的二次扫描 | P2-06（统一的 ref 语法）改为结构化 id 列表 |
| P1-07 | golden 影响多于设计所列（4 个用例）：容器 span 还改动 doc/nested-base16/18、figure/float-in-list、exec/let-ctor-name、exec/nested-stmt-diag、line/crlf-inline、line/trailing-blank（P0-01…P1-06 新增的含列表/引用的用例）；行为改动 conform/appa-splices（`}` 后文本保留）、conform/appb-lines（`+` 与 `3.` 不再合并，`3.` 起始号生效）、exec/contain-orphan-diag（孤立闭合行为 region-orphan 错误块） | 设计的统计早于这些守护用例；三处行为改动正是本步要修的发现（trailing-text-after-block-closers-dropped、missed:5、region-error-recovery），逐个审阅 | 无 |
| P1-07 | 语句失败的恢复界保留 P0-04 的"块起始行"停止条件（不只第一个空行），并扩展到容器内 | line/let-unclosed-diag 的验收要求 `#let x = f(1` 下一行的标题保留；只按空行恢复会吞掉它 | P1-08 的试探性 carry 沿用同一恢复界 |
| P1-07 | 被迫闭合的区域只给 `region-unclosed` 诊断，不包成 Error 块；容器内多行语句的 JS 仍记原始 span，不拆成 JsText 片段 | 计划条目只要求诊断；容器内语句在 AST 中仍是 statement-nested-unsupported 错误，JS 文本无人使用 | P2-12 让容器内语句可执行时改为 JsText 片段 |
| P1-07 | 空行结束引用（`> a⏎⏎> b` 为两个引用）；XFAIL +1（line/fence-container-diag：代码行缺 line-spans 的既有类别） | 前缀容器的继续规则（与 CommonMark 一致）；新用例必须含容器内 fence | P3-07 修复代码行 span 后整类移除 |
| P1-08 | Phase 1 不存 AtomTape：段落行用与 phase 2 相同的原子/括号原语扫描，遇到未闭合的构造就一次性前瞻到其结构边界（加倍窗口），而不是"暂定提交、到界回退再重读"；RevertedWindows 照记，回退的开符记在 SkelNode::literalAt 供 phase 2 遵从，块形式内容体的闭合行记在 SkelNode::bodies | 先提交后回退在"每行一个永不闭合的开符"时会级联成 O(n³)（3000 行 >120s）；前瞻结果相同且每行只处理一次（同输入 0.9s，4KB 32ms）；存储的 tape 还须与 phase 2 的普通匹配回退保持一致，收益只在增量编辑（T9） | P3-21 等增量重排若需要 tape，从同一原语生成 |
| P1-08 | `@id[` 与 `#let x = [` 的内容体未在本步启用 | 设计把 `@id[…]` 门控在 T3 的 ref 构造器（P2-06），`#let x = [..]` 内容字面量属于 S8（P2-12）；本步只有 `#f[`、`^[` 两种内容体 | P2-06、P2-12 启用时复用 parseBody |
| P1-08 | 多块脚注体按块渲染（resolve.cc 的 notes 节）；模型增加 N3（含块的 seq 并入兄弟/替换段落） | Blocks 模式使 `^[…]`、`#f[…]` 可以产生多块值，不处理则被压成一行 | T2 的层级规范化（行内位置的块）在 P2 阶段 |
| P1-10 | 内建行以 JSON 嵌入（configure 时生成 semantic_data.gen.h，运行时解析一次并由 unitRegistry 校验），而非生成 gen/elements_defaults.h 的 C++ 表；模板语言只实现内建行用到的子集（text/term/slot/node/styled/when/each 及 paras 体放置），Query 只有 outline/table/flow 三种 | 文档声明（P2-07）同样走 JSON 运行时加载，一条路径；其余模板与查询形式随 P2-07/P3-03/P3-13 的新行加入 | P2-07、P3-13 |
| P1-10 | Resolve 的重跑类别仍为 Once：resolver 本身不再修改输入（输出树替换 tree.root，未变子树共享），但同阶段的 sidecar 抽取仍改写实例化树 | D-S13 要求的"不修改输入"已对 resolver 成立；把 Resolve 改为可重入需先把 sidecar 抽取移出（P2-13 起 sidecar 归 codeblock 构造器） | P2-13 后改 stages.def |
| P1-10 | 旧 resolver 的一处错误（引用非 figure 的带标签 group 显示"图 "）先在旧代码上单独修正并更新 golden（f93d611），再提交结构性重写 | 守则：golden 有误的修正单独提交；结构性提交保持字节不变 | 无 |
| P0-07 | D-I03 的节点预算下限从 1M 改为 256K：预算 = max(262144, 64 × 原始节点数)；深度上限 256 不变 | 1M 个 ContentNode 约 90MB，达不到 P0-07 的"峰值内存 < 64MB"验收；64× 原始节点数的项对正常文档仍然宽裕 | P1-03 把它做成 HostOnly 设置时，默认值用 256K |

## 阻塞记录（§4.7）

| 步骤 | 现象 | 已尝试 | 保存位置（stash / wip 分支） | 绕行 |
|---|---|---|---|---|
