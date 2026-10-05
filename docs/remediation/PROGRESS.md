# 进度（goal 运行维护）

> 续做方法：先读本文件，再 `git log --oneline remediation/audit-2026-10`，然后从第一个非 done 步骤继续（PLAN.md §4.6）。
> 状态：todo / doing / done / blocked。每步完成后同时更新 `TRACEABILITY.md` 的状态列。被阻塞时按 PLAN.md 附录 C 的依赖跳过（§4.7）。

## 当前位置

- 阶段：P1
- 下一步：P1-18
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
| P1-11 | TextRules 兼容表与单一分类器 | done | grep:plan P1-11 | 2026-10-06 | 0（全部 golden 字节不变） | vendor UCD 17.0.0 五个文件（engine/rules/ucd/17.0.0，ucdc --fetch 可重取）；engine/rules/classes.def（CC 类）+ locale/compat.def（RULES_VERSION 0：五个宽区间、clreq 标点、歧义类、列、kern 截断、App C 常量）→ tools/ucdc.mjs → engine/gen/textrules.h（类区间表 + UCD 列 GCB/ExtPict/EAW，约 3.4KB，入 gen-all/G9）；shape/textrules.h 唯一分类 API，emit/inline/layout/typeset_html/support 五处分类器全部改走它；mock.h 冻结 mockIsWide；unitTextRules 对全部码位钉住旧分类器的字面副本；tools/rules-diff.mjs（码位类/列差异与语料边界对比，供 P4）；docs/shaping-design.md |
| P1-12 | HList 与 run 实例 | done | grep:plan P1-12 | 2026-10-06 | 结构性提交 0 变化（全部现有 golden 字节不变）；+111 个 hlist golden（每个排版用例一份） | engine/src/shape/hlist.{h,cc}：HItem 24B（IK Box/Glue/Penalty/Disc，GC Word/InterChar/Autospace/Blank/ObjectSpace，IA_* 属性，run，aux，w，x，cold）、ColdRec（源 span、rawPx、blank、迁移用 capSu、anchor）、AdvanceSpec 32B（Measured/Defined/Fixed/MeasuredMinusBlanks/KernCtx/Object）、DiscRec、RunRec（face/link/SynKind/copyText/RealizeClass/anchor）；TeX 合法性写在头文件，lintHList（每边界至多一个断点、开标点后与闭标点前无断点、run 连续且 BlankBearing/Pinned/Object 单盒、LetterSpaced 盒后有 InterChar）在每个 golden 上运行；emit 以旧逐节点逻辑直接产出 HItem（InlineSink 接口，块遍历共享），finish() 按今天的断行结构写成 TeX 形式并加 InterChar 胶，run 随项生成；resolveWidths 读 AdvanceSpec；fuseLegacy 为规定的降级表（模板化：生产只保留断行器读的 BreakBlock 五字段，完整 LinebreakBlock 只供 blocks dump 与校验）；emit/legacy.cc 原样保留旧行内发射器作为 CI 预言机（仅原生链接），黄金运行器与 tsrc --fuse-check 逐字段比较——111 个用例及真实/typst/博客语料共 650 篇全部相等；layout 四个行循环与 renderLineBox 改读 item 区间与 run 实例（blockStart 映射断点，块区间只用于 dump）；tsrc --stage=hlist（数值罚分、类、span，修复 dump-hides-finite-penalties）；性能：同时把词宽表改为按字符串下标的槽表（哈希查找占 WASM 引擎约五分之一）、px 格式化改为精确整数实现（render −2ms），87K update 29.5ms（同机交替测 HEAD 29.6–30.0）；docs/shaping-design.md §5 |
| P1-13 | InlineObject 注册表与扁平化表 | done | grep:plan P1-13 | 2026-10-06 | 17 个含公式用例的 hlist golden（增加对象记录：object 盒带种类/部件/上下伸、对象表）；+4 用例（inline/object-image、inline/object-raw、inline/hardbreak、inline/object-unsupported-diag），其余全部字节不变 | 扁平化表 = schema 每个 kind 的 inline 列（text/container/code/object/break/error/skip/unsupported），gen-schema 强制每行都有并生成 KindInfo::inl，emit 行内遍历按它分派（封闭，无 default 递归）；shape/objects.{h,cc} 对象注册表（math/image/raw/error，边界类 firstCC/lastCC），HList 增加 parts（每部件 w/asc/desc）；数学成为对象：emit 时度量齐全即展开，否则留一个占位部件并标记 hasDeferred，resolveWidths 对该列表单独排版并拼接（不再整篇重 emit；显示公式保持至 P3-26），黄金流程中 Id_(A)、f(x) "if" x > 0 走拼接路径且 fuseCheck 相等；行内 image/raw 成为对象（声明或拉取的尺寸，基线上的盒；无尺寸/不安全为 1em 虚线占位并给 image-src），修复行内图片被静默丢弃；不支持的 kind 成为 error 对象（⚠ kind）并给 shape-unsupported，黄金运行器对非 unsupported 用例出现该诊断即失败（语料扫描为零）；hardbreak → Penalty(-INF)，经 fuseLegacy 成为块罚分 -BREAK_INF，适配器映射为 Penalty(Forced)，断行器对强制断点前的行用 fil、layout 视为段末（不两端对齐、复制为换行）；layout 三处高度副本已在 P1-12 合一，改走 objectPart 垫片；渲染按对象种类分派（公式盒、img、tsr-iraw、错误文本），语义渲染器同样绘出行内 image/raw；record-fixtures 支持 X.tree.json（无表层语法词汇的原始 ops 用例），contract 检查把 span.tsr-iraw 视为可信内容；docs/shaping-design.md §6 |
| P1-14 | KP 正式化与校验缓存 | done | grep:plan P1-14 | 2026-10-06 | 0（全部 golden 字节不变，115 个用例） | break.cc 改为 TeX 活动表：节点的行一旦 Overfull 即失活，Forced 断点使之前所有节点失活，只在 parshape 前缀内按行数分开保存节点（之后每个断点一个，按全序取优），去掉 ±5 窗口、±1 行数剪枝与重试阶梯；救援并入末遍（所有活动节点在某断点都 Overfull 时按全序取最优者在此断开）；BreakParams{cost, tolerance, emergencyStretch}，默认只跑末遍（与今天一致），容差遍与应急伸展遍按设计实现并有单测；缓存键为条目字节 + 块数 + 行宽 + 参数的 128 位哈希（MurmurHash3 x64_128 的块步骤），命中时以条目数校验，LRU 预算按结果字计；i64 su 前缀和与 -ffp-contract=off 已在 P0-12；语料对比（真实文档/typst/博客 539 篇 × 300/640px）：133 个单元的断点变化，106 个代价更低，其余 27 个（24 篇）代价更高者全部是旧窗口搜索把内容挤进多条 Overfull 救援行（救援行不计代价），新结果 Overfull 行严格更少，无一例更差；性能：未缓存 KP 在 HoTT + 40 章 pbr-zh × 两种宽度上 50.0 → 38.0ms；bench 87K update 29.1ms、relayout 56.7ms，均在预算内，无需有界活动模式 |
| P1-15 | 断行移入布局（ExclusionMap） | done | grep:plan P1-15 | 2026-10-06 | 0（全部 golden 字节不变） | layoutDoc 自己断行（breakLinesCached，overfull-line 诊断随之移入 layout，顺序不变）：段落、浮动题注（按图宽）、表格格（同一 colW 公式）、sidecar 行（按 sidebarW）；浮动追踪器改为 layout.cc 中的 ExclusionMap，在 layout 自己的游标处计算，与游标共用一个 gapBefore()，按今天的前缀 ParShape 规则（以 baseLeading 计遮挡、同侧堆叠取最宽、异侧与非文本单元清除）逐位复现；删除 Doc::typeset 的逐 kind 循环、FlowUnit 的五个重放字段（narrow/narrowK/narrowLeft/floatShiftSu/floatClearSu）与断点三字段（FlowUnit 与 TableCell），断点结果记录在 LayoutResult::breaks（dumpBreaks 读它）；stages.def 删除 Break（并入 Layout），schema 设置行 affects 中的 Break 改为 Layout（8 行），products breaks→Layout，lint-arch 把 break/ 归入 Layout；figure-design 的 "under-clears" 更正为 "over-clears" 并改写追踪器说明 |
| P1-16 | 与宽度无关的 emit（SizeSpec） | done | grep:plan P1-16 | 2026-10-06 | 14 个图片用例的 blocks 与 hlist 单元头（改为打印尺寸 spec：intrinsic=WxHpx、scale、placeholder）；inline/object-unsupported-diag 的 diags 次序（image-src 先于 shape-unsupported）；其余全部字节不变 | emit 不再读宽度：图片记录 ImageSize（固有 px、scale、placeholder），代码块只记 sidecar 标志，layout 用同一公式解析（resolveImageSize，sidecar 列宽 code.sidecarFrac）；image-src 诊断移到图片请求扫描（每文档一次，不再随 emit 重复）；host.width 的 affects 改为 Layout+Paint，code.sidecarFrac 改为 Layout；setWidth 只使 Layout 失效；worker 的 relayout 以 host.width 补丁原位重进 Layout（不再 fork；得到 REBUILD 时才回退到 fork），并带回阶段计时；黄金运行器新增"原位宽度补丁 == 新建"检查（全部用例）；验收：87K relayout 的引擎+渲染（node，同一构建）fork 路径 10.1ms → 原位 4.3ms；浏览器端到端 relayout 中位数 57.3（P0-11 记录）→ 55.2ms，主要耗时是整页 DOM 替换（worker 内 engine 2.9 + render 5.0ms） |
| P1-17 | 统一行物化（materializeLines） | done | grep:plan P1-17 | 2026-10-06 | 12 个 breaks.txt 增加格/题注/sidecar 流记录（cell=）；20 个 layout 与 24 个 html、2 个 paged：折行的标题、题注与 error 块获得 join（仅 join 变化，逐行分类核对）；浮动题注行获得源 span（所在段的 data-s 随之变为段相对）并移除 5 条 XFAIL；figure/float 的 2 行紧题注按断行器的假设收缩；+2 用例（region/table-term、code/sidecar-hyphen） | layout.cc 一个 materializeLines（LinePolicy：Join FromBreak|Never，Align Justify|Ragged|Center|Cell）取代段落、表格格、浮动题注、sidecar 行四个循环与 P1-13 的高度垫片：丢弃后的条目区间、自然宽与伸展、行高取 vmet 与对象部件、连字符取自 Disc（修复 sidecar 断字丢连字符）、join 由断点处被丢弃的 run 决定（不再由对齐决定：段末与硬换行才是真正的行界）、表格格与 sidecar 用 Join::Never、非两端对齐的紧行一律收缩到断行器假设的宽度（Overfull 设在收缩极限）；TableCell 保留锚点（格内行内 term 的 id 落在格的首行，修复悬空的 @gizmo），typeset 渲染器输出行锚点，语义渲染器为行内带标签 group 输出 span id；dumpBreaks 覆盖每个流；docs/document-model.md §6.3/§9 更新 |
| P1-18 | 盒树、布局器注册表、Fragment、DisplayList | done | grep:plan P1-18 | 2026-10-06 | pages/paged-eq-ids.paged（golden 有误：同页锚点被共享状态丢弃，现 tsr-h-1.1、tsr-eq-a/b 等 id 出现）；其余全部现有 golden 字节不变；+236 个 blocktree/vlist golden（每个排版用例各一份）；+1 用例（region/anchor-kinds，含 dl 与 paged） | engine/src/boxtree：BoxTree 阶段（stages.def，Once）在 resolve 之后建 LayoutBlock 树（前序扁平数组，子树 [i,end)），布局器按内容模型选（Paragraph/Stack/Replaced+Painter/Grid/Table），TraitTable 手写复现今天的默认（根间距 1pg、列表 1/3pg 整除、标题/题注/公式不两端对齐、标题与块图 keep-with-next），角色是数据（role 表按 interned 名查：figure→题注与浮动、sidecar-lines→第二轨），列表/引用缩进与标记、首行缩进决定都在建树时；锚点无状态：带标签块的标签落在其子树第一个叶子（代码块、分隔线、raw 也算；仅标记的空列表项除外），更深的标签占用时外层作为第二个 id（空 span）——每个带锚块恰好一个带锚 fragment；emit 只塑形叶子：Flow（HList 流）+ cells（表格格/浮动题注行/sidecar 行）+ 类型化载荷 LeafData（Rule/Grid/Table/Raw/Image/MathData），FlowUnit 的并集字段与 kind 开关删除；layout：DocLayout 的布局器注册表 kLayouters[LayouterId]，Stack 递归以最深公共栈的间距（取代 tightAbove/gapBefore），叶子布局器产出 Fragment（kind Line/Rule/CodeRow/Raw/Math/Image 取代 special 0–5，y 一律为顶，规则线 dump/paint 取中线，新增 baseline），VEntry 垂直列表；layout/paginate.cc：分页切割器从序列化器移出（PageResult），keep-with-next 由特性决定；paint/：DisplayList（DLRoot/DLBlock/DLNode/DLRun：run 在 run 实例边界形成、行内载荷、所有打印的 px 值），typeset 写出器改为无状态 DL 遍历（不读 Config、FlowUnit、内容树；runCss 只取 basePx），删除 lastAnchored，paged 与流式输出同一组 id；model.h 拆出 model/style.h，lint layout-includes-model 改为按 include 传递闭包检查 layout/break/paint/typeset_html，role-string-compare 覆盖 boxtree/paint，lint 基线 8→4；products：blocktree、vlist、dl；layout.h:20 与 emit.h:67 的旧注释随结构替换；docs/layout-design.md、docs/render-design.md 新建，document-model §8/§9.1/§9.4、architecture、figure-design、host-protocol 更新 |
| P1-19 | 资源表（ResourceTable） | done | grep:plan P1-19 | 2026-10-06 | code/tsm-hl.tree（树不再被 token 改写：代码体保持纯文本）；其余全部 golden 字节不变 | engine/src/resource：resources.def（textWidth/fontVmet/fontFace/codeTokens/boxInfo，RES_VERSION 1）+ tools/gen-res.mjs 生成两端列表（resources.gen.h、resources.gen.mjs，入 gen-all/G9）；线格式 TSRQ/TSRA（codec.cc 与 runtime/src/shared/rescodec.mjs，列式、批次号、字符串块、MetricKey 表）；tsr2_requests(doc, kinds)/tsr2_provide 一批进一批出（不再每词跨边界、不再 JSON 与重新 intern）；完整 MetricKey（字体栈、faceDigest、px、字重、斜体、features、lang、dppx；新增 host.dppx，code.fontFeatures 影响 Measure）：表存原始 px，Measure 量化（ceil + ε）；ResourceTable 按内容键去重（同语言同代码体只问一次），应答逐行校验：错批整包拒绝（provider-invalid，需求保持待定），重复/越界/非有限/负值行无效，缺行 provider-missing，失败行 measure-failed——各自只降级被消费的量（宽度取码位 em 上界×1.2、vmet 1em/0.3em、token 退为纯代码、图片占位 + image-load）；token 应答不再改写内容树：emit 与语义产物共用 tokenLines 折叠（删除 foldTokens），图片尺寸只在表中（作者单独给的 w/h 保持，另一边按比例，缺陷 #24 的结构性修复）；旧 JSON 请求与 tsr_provide_* 保留为同一张表上的垫片（未匹配的应答报 provider-invalid）；tsr2_abi.resVersion 与 abi.mjs 核对；worker.mjs、render.mjs、原生 driver（answerRound 走同一线格式）切换；边界不变量：重复 compile/ingest 报 doc-reuse，未收敛时 render 报 render-precondition，空请求即判停滞（不再空转 64 轮）；fuzz_resanswer（D-H08）；单测 unitResources（编解码往返、错批、缺行降级、作者参数不被改写） |
| P1-20 | 按段延迟 | done | grep:plan P1-20 | 2026-10-06 | 0（全部 golden 字节不变） | Emit 按顶层块运行（EmitPass：同一遍共享塑形缓冲，top(t) 逐块）：代码块/图片的应答未到时该块等待（Resolve 时按 pid 记录其需求），显示公式缺正文字体度量时该块延迟——这次尝试及其 (Emit, pid) 诊断片丢弃、所缺词随下一批请求，到达后只重新 emit 该块；其余块照常 emit，其宽度需求与延迟块的需求同一轮请求（不再有整篇屏障与整篇重新 emit）；删除 Doc::mathTextMissing 与 MathTextCtx（改为 measure.h 的 MeasureNeeds：pass 记录所缺，由调用方延迟）；DiagSink 增加 pid 与 beginPid/sortPids（重试的块替换自己的诊断片，片按块序保持）；单测（段落先 emit、图片等待、同一批含 boxInfo 与 textWidth）；87K update 29.9→27.9ms（引擎 9.5→8.4：显示公式所在文档不再整篇 emit 两次） |
| P1-21 | Session 内容键缓存 | done | grep:plan P1-21 | 2026-10-06 | 0（全部 golden 字节不变） | engine/src/resource/session.{h,cc}：宿主持有的内容键应答缓存（宽度按会话内 metric key id + 文本、vmet、代码 token；宽度两代近似 LRU，预算字节）与 memo 槽；tsr2_session_new(json)/free（有文档挂接时拒绝）、tsr2_doc_attach，fork 共享源会话，未挂接的文档用私有会话；需求查找顺序：文档自身 → 引擎内 answerer（注册表：codeTokens.tsm，可在建会话时关闭）→ Session（拷入）→ 宿主；provide 对 Content 种类且 store 位为 1 的行写穿；MetricStore 缺项时经 MetricBacking 查会话；KP memo 从进程全局静态表移入会话 memo 槽，键为完整输入字节（命中时逐字节比较，不再只校验条目数）；新增 host.loadedFaces（宿主已加载的字体，进入 faceDigest：字体落地即换键，不复用回退字体量出的宽度）；JS：worker 一个会话、每个文档挂接，canvas 测量器去掉跨文档缓存（同一轮内按字体+文本去重），token 缓存删除（会话缓存），policy.tokenCacheEntries/measureCacheWords 改为 sessionBudgetBytes；render.mjs 每进程一个会话；黄金运行器对每个用例再在一个跨用例的热会话上重放并要求 html/diags/breaks 字节一致；单测（第二个文档只请求图片尺寸、KP 命中不增长、answerer 可关闭）；87K update 27.9→27.4ms |
| P1-22 | MathDict | done | grep:plan P1-22 | 2026-10-06 | 0（mathbox/blocks/html 全部字节不变） | engine/data/math/symbols.tsv（由原生成头文件的 322 行播种，删去 11 条不可达行：括号、prime、o+ o- o. :'；列：name cp class flags negation alias_of tex default_for_cp class_source）；固定版本的 UCD UnicodeData.txt（17.0.0）与 MathML Core 运算符字典（w3c/mathml-core@b07c0b3，转为 TSV）入库；tools/mathdict.py（纯 python3，不用 unicodedata）生成 engine/gen/math_dict.h（按名排序的符号表、码位默认类索引、运算符键 trie、UCD 否定表）、engine/src/math/atom.h 与 runtime/src/shared/math-vocab.gen.mjs，入 gen-all/G9；构建门禁：版本固定、重名、码位多行无唯一默认行、字母与运算符字符混合的键（!word 行除外）、超过 4 字符的运算符键、与 MathML Core 推出的类不同且未标 override（54 行 override）；math/dict.h（MathDict：byName、matchOp 走 trie 的最大吞吐、classOfCp、negate）；词法器改用 trie 与码位索引（保留 !word 与 _|_ 分支）；mathc.py 不再产出 OpEntry/类别枚举、不再加入词典码位，RANGES 加 0x02B0–0x02FF，不再编译水平变体链与 asmItalic（无人读取），单测守住 kGlyphs ⊇ 旧集合（engine/data/math/glyph-cps.baseline.txt，2023 个）；死数据清理：kFlagStretchy、bar 的无用码位、allowFraction/scriptArg 参数、layoutBigOp 不可达的 textOp 分支；单测：每个键都词法回到自身；文档漂移修正（math-design §3/§5/§7/§8/§13/§14、architecture §1/§5、document-model 的 fonts.math、§10→§14 引用） |
| P1-23 | MathFont 运行时对象 | done | grep:plan P1-23 | 2026-10-06 | 0（全部 golden 字节不变） | engine/src/math/font.{h,cc}：MathFont（upem、hhea、常量、字形记录、竖直变体链、装配件、family、内容哈希；constant/su/glyph/chain）与进程级 MathFontRegistry（Euler 嵌入为 id 0，零拷贝包装生成产物）；排版器通过 MathFont 读取全部常量与字形（删除 mathfont.h 的全局访问器）；MathBox::textFont 改为字体 id（kTextFont = 文本字体），typeset 写出器从注册表取 hhea 固定字形 span 的行盒；MathPolicy 收纳今天的字面量（short_fall 整数有理 1/10、装配重复上限 64、未覆盖字形的 600/700 替身）；math-coverage 诊断的码位改为十六进制；mathc.py 一次运行同时产出 woff2 子集（字形记录集合 + U+0020，默认特性，130KB）与内容哈希，写入头文件与字体清单 runtime/src/shared/mathfont.gen.mjs；shell 把数学字体作为 role=math 的声明字体经 ensureFontFaces 安装，删除第二条 @font-face 路径与 STIX/serif 回退，.tsr-mg 加 font-kerning:none；export-static 与 pack-dist 按清单复制字体；e2e 新增数学绘制审计（数学字体已加载、每个字形 span 用它且被覆盖、字距关闭、U+0020 覆盖、行内公式基线与正文基线差 < 0.5px）；math-design §3/§8/§11 更新 |
| P1-24 | MathRow 注册表与 Call IR | done | grep:plan P1-24 | 2026-10-06 | math/parse-diag 的 blocks/hlist/breaks/layout/mathbox/html/diags（整式文本盒 → 局部布局 + Error 叶子，诊断改为子 span）；binom 字节不变；+21 个 mathir golden（每个含公式的用例） | engine/src/math/ir.{h,cc}：封闭节点集 MNode{Sym Num Text Run Attach Frac Group BigOp Call Param Error}（带源字节区间）；行注册表：C++ 原语 frac/stack/radical/lr/accent/rule + engine/data/math/stdlib.tsv 模板行（sqrt root abs norm floor ceil binom overline underline bar 与各重音，mathdict.py 生成 math_rows.h），模板用文档将来使用的同一模板语言书写、绑定时展开；checkRow 单一校验（单测覆盖全部行）；布局只按原语分派（删除 isCallName 与 layoutCall 的名字 if 链、layoutBinom 改为 stack 原语 + lr 模板）；调用只在紧邻 name( 时绑定，否则用行的 bare（dot → ⋅ Bin、hat → ˆ），再查词典，再按隐式名字规则（sqrt x、abs 成为名字）；not 仍为 ¬（缺陷 #23：裸 dot/hat/bar/abs 不再拖垮整式）；元数来自槽位：缺参为空 Error 叶子、多参为调用后一个 Error 叶子，均为 math-arity 警告；错误局部化：无操作数的运算符、多余的右括号等成为 Error 叶子（在 , ) ; 关系符或结尾处重新同步，以文本字体排出，测量同名字），其余照常布局；每式至多 8 条诊断、子 span；math-coverage 每个码位一次；prime 与 ^ 合并（f'^2）；解析深度上限 256；symbols.tsv 删除重音行（重音现为函数行）；tsrc --stage=mathir（树与诊断）；单测：全部行通过校验、bare/名字/相邻绑定、元数、错误叶子与子 span 映射、嵌套上限；math-design §10.1 |
| P1-25 | 数学惰性布局 | done | grep:plan P1-25 | 2026-10-06 | 0（现有 golden 字节不变；+1 用例 math/coverage-diag）；另见下一提交（公式随其 run 的链接与颜色绘制：inline/bracket-island html） | emit 只"准备"公式（解析、报告解析诊断，留下待定对象：行内公式一个占位部件，显示公式无盒的 MathData），不再读度量；Measure 阶段经 resolveWidths 的通用待定对象钩子（对象表的 finalizer，循环不点名数学）在所需文本宽度就绪后排版、拼接部件、只在成功的那一遍报告排版诊断；显示公式同样在 Measure 定型——P1-20 的"显示公式缺度量则推迟整块"随之删除（EmitPass 不再接收度量，emit 之上不再有数学测量通道）；mathbox 产物改在 Measure 阶段；未被数学字体覆盖的码位改为测量过的文本叶子（每码位警告一次），不再画 600/700 替身框；补回 varrho、varsigma、::= 三行（字体不覆盖时作为文本叶子）；layoutMathFormula/Segments 可关闭解析诊断（定型时不重复）；math-design §14、shaping-design 更新 |
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
| P1-12 后 | 3.50 | 12.00 | 29.50 | 1.8 / 4.1 / 0.4 / 8.9 / 4.1 | 51.9 / 74.3 / 114.9 | 6.10 / 22.50 / 58.60 | HList 使引擎 +0.8ms（87K），compile +0.4ms（上一文档更多分配的释放）；词宽槽表与整数 px 格式化抵消（render 6.1→4.1）。同机交替 A/B：P1-11 后 HEAD 29.6–30.0。P1 阶段门禁（P0 结束 ×1.05+0.3ms：3.98 / 12.06 / 29.70）当前满足，余量很小，P1-14 起须保持 |
| P1-14 后 | 3.70 | 12.00 | 29.10 | 1.3 / 4.0 / 0.4 / 9.4 / 4.2 | 54.3 / 78.9 / 112.1 | 6.00 / 22.80 / 56.70 | 【性能】步：活动表 KP（无窗口）在缓存未命中时更快（HoTT+pbr-zh 未缓存 50.0→38.0ms）；relayout 87K 58.6→56.7；update 在 P1 阶段门限内（35K 12.00 ≤ 12.06，余量很小） |
| P1-16 后 | 3.50 | 12.10 | 29.70 | 1.8 / 4.1 / 0.4 / 9.4 / 4.2 | 53.3 / 77.4 / 116.7 | 5.10 / 21.40 / 55.20 | relayout 原位重进 Layout（引擎+渲染 87K：fork 10.1 → 原位 4.3ms）；update 87K 29.70 贴着 P1 门限 29.70，下一步须留意 |
| P1-18 后 | 3.60 | 12.40 | 30.10 | 1.7 / 4.1 / 0.3 / 9.2 / 4.3 | 52.7 / 78.6 / 115.5 | 5.00 / 21.50 / 56.70 | paint 层（DisplayList）多一遍；87K update 30.1–30.3（同机两次复测），高于 P1 门限 29.70 约 0.4ms、35K 12.40 高于 12.06；P1 结束前的性能步须收回（候选：DLRun 瘦身与按块复用、JS 字符串编组、compile 析构） |
| P1-19 后 | 3.90 | 12.30 | 29.90 | 1.5 / 4.1 / 0.3 / 9.5 / 4.2 | 55.6 / 81.3 / 117.0 | 5.20 / 21.30 / 55.00 | 二进制批量拉取（每轮一次跨边界）；87K update 29.9（仍高于 P1 门限 29.70 约 0.2ms），7.8K 3.90 贴近 3.98；P1 结束前的性能步须收回 |
| P1-20 后 | 3.50 | 11.60 | 27.90 | 1.3 / 4.0 / 0.3 / 8.4 / 4.2 | 55.4 / 80.6 / 116.4 | 5.30 / 21.90 / 57.50 | 按段延迟：不再整篇重新 emit；回到 P1 门限以内（29.70 / 12.06 / 3.98） |
| P1-21 后 | 3.30 | 11.20 | 27.40 | 1.2 / 4.2 / 0.3 / 9.0 / 4.2 | 55.5 / 83.1 / 115.9 | 5.30 / 21.80 / 55.80 | Session：新文档只请求未答过的项（宽度不再每次跨边界），引擎内多了会话查找 |

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
| P1-12 | hlist golden 为 111 个而非计划的 49 个 | 计划写于用例增加之前；每个排版用例一份 | 无 |
| P1-12 | 发射形式按今天的断行结构写成 TeX 形式（可断的盒后紧跟 Penalty，胶自身的非零罚分在其前；如 CJK 字后的 autospace 前有 pen 0），而非设计中整形器的规范形式（"Direct 且有胶 ⇒ 不加 Penalty"） | 发射逻辑仍是旧逐节点逻辑；这样 fuseLegacy 无歧义且与旧块逐字段相同；lint 是性质检查，全部通过 | P4-02 整形器改为规范形式，hlist golden 随之变化 |
| P1-12 | 表示上的落地差异：AdvanceSpec 增加 tri（KernCtx 测量串）并排成 32B；Disc 的未断宽度即 HItem.w，DiscRec 以 spec 指向接缝 KernCtx（无 nobrW 字段）；InterChar 胶无 spec、与其字共用 ColdRec；CJK 盒的 x 存其间隙权重（迁移用，与 capSu 同在 P4-08 删除）；ColdRec 增加 anchor（DOM run 在 P4-01 前保持今天的锚点规则，锚点不切分 run）；InlineObject 只有数学分段（P1-13 建注册表）；SynKind 先只有 Content/Ref/Indent；行内代码暂为 Plain（Rigid 与 word-spacing:0 在 P4-01） | 保持全部现有 golden 字节不变，同时让 HItem 保持 24B、每个 CJK 字不多一条 spec/cold | P1-13、P4-01、P4-08 |
| P1-12 | 生产路径只保留断行器读取的 BreakBlock（宽、断行宽、容量、罚分、类别位），完整 LinebreakBlock 只在 blocks dump 与 fuseCheck 中由同一降级模板生成 | 性能（WASM 引擎）；同一模板保证两种投影一致，fuseCheck 另行逐块比较 | P1-14（缓存键改为 DP 输入）、P4-08 删除 |
| P1-12 | 步骤外的两项性能修改：MetricStore 词宽表改为按 StrRef 下标的槽表；HtmlWriter 的 px 格式化改为精确的整数实现（与 printf "%.3f" 相同，unitFmtPx 对约 40 万值钉住） | 不带 HList 开销的成本进入 P1 阶段门禁：HEAD 已贴近 29.7ms 的 87K 门限；两项均输出不变，单独提交 | 无 |
| P1-12 | lint 在 typst 语料中发现 2 篇（layout-inline-cjk--cjk-punctuation-adjustment-1/-2：`！ ？` 中间有键入空格）存在闭标点前的断点 | 旧行为：空格前的盒可断（UAX #14 LB13"即使隔着空格"未实现）；结构性步骤不改变断行；用例层面 lint 全部通过 | P4-02（Section.pair 带 spacesBetween）修正，届时把这两篇纳入 lint 检查 |
| P1-13 | hardbreak 按计划（复核 R4）映射为 Penalty(Forced) 而非设计的"Penalty(0) + 警告"；另外断行器对以 Forced 罚分结束的行使用 fil，layout 把该行视为段末 | TeX `\hfil\break` / CommonMark 硬换行的语义：断点前的行左齐，不被拉伸；现有用例没有 Forced 罚分，结果不变 | P1-14 正式化 KP 时保留此规则；P4-08 物品原生断行器 |
| P1-13 | para 的扁平化行为 container（其内容流入行内）而非 unsupported | resolver 物化的行内 term 是 group{role:term}[para[…]]，typst 语料 model-terms 三篇依赖它；块级进入行内的拆分由 P2-11（N4–N6 诊断）与 P3-17（拆分）负责 | P2-11、P3-17 |
| P1-13 | 对象的边界类只记录、暂不参与边界判断：对象沿用公式规则（其后可断、其后紧接闭标点则禁断），只有公式产生 CJK autospace；行内图片忽略 scale（只用 w/h） | 边界类由整形器的成对规则读取（P4-02）；scale 依赖版心宽度，宽度无关的 emit 在 P1-16 | P1-16、P4-02 |
| P1-13 | 语义渲染器（静态导出）也改为绘出行内 image/raw（对象行），其余 kind 保持原有递归 | 同一缺陷的另一条输出路径（博客静态页走它）；块级进入行内的处理留给 P2-11/P3-17 | P2-11、P3-17 |
| P1-13 | hardbreak 与行内 raw 的用例用 X.tree.json 构造原始 ops（record-fixtures 用运行时 OpBuf 编码，G3 照常校验） | 两者都没有表层语法（hardbreak 语法保留未开放；raw 只在区域处理器中可得） | 语法开放后可改为普通用例 |
| P1-15 | ExclusionMap 先以今天的追踪器算术实现（剩余遮挡高度、遮挡宽度、侧别），而不是设计中的几何矩形 + available()/clearY() 接口 | 计划要求本步"先复现今天的前缀 ParShape"、golden 不变；几何矩形与保守行带需要真实行高，属于 ParShape 步骤 | ParShape/保守行带落地的步骤（T6，P1-17 之后） |
| P1-16 | golden 变化多于计划（计划 5 个 blocks）：14 个图片用例的 blocks 与 hlist（两者共用单元头，hlist 在 P1-12 新增），外加 P1-13 新用例的 diags 次序 | 计划之后新增了带图片的用例（ref/supplements-*、figure/*、doc/wrap-heading-caption）；image-src 改由图片请求扫描报告后排在 emit 诊断之前 | 无 |
| P1-16 | image-src 诊断放在图片请求扫描（Resolve 阶段末，与 NEED_IMAGES 同一遍）而非 ingest 扫描 | resolver 物化的内容也可能含图片；同一遍里报一次，re-emit 不再重复 | 无 |
| P1-16 | "relayout 明显更快"以引擎侧计时验收：87K 的 fork 路径 10.1ms → 原位 4.3ms；浏览器端到端只从 57.3 降到 55.2ms | 端到端耗时主要是宿主替换整页 DOM（每行都随宽度变化），不在引擎内 | 宿主侧的增量 DOM 替换不在本计划范围 |
| P1-17 | golden 变化多于计划：breaks 12 个（计划 5 个；之后新增的表格/浮动用例同样增加流记录）；join 的变化不只题注，还有折行的标题与 error 块（同一规则，审计 break-layout-pages/missed:2 正指此类）；浮动题注行获得源 span 后 5 条 line-spans XFAIL 转为通过并移除；figure/float 两行紧题注收缩 | 统一行物化后各流遵循同一规则；题注过去比断行器假设的更宽而溢出浮动框 | 无 |
| P1-17 | XFAIL 增加 1 条：新用例 code/sidecar-hyphen 的 html line-spans（代码行缺 span，属已有的 P3-07 类） | 计划要求修复 sidecar 断字丢连字符，需要回归用例；代码行的 span 由 P3-07 统一补齐 | P3-07 |
| P1-17 | 语义渲染器同时为行内带标签 group 输出 span id | 新回归用例 region/table-term 的语义输出同样出现悬空锚点（同一缺陷的另一输出） | 无 |
| P1-18 | pages/paged-eq-ids 的 golden 更正与结构性提交同在一个提交 | 无状态写出器本身就是修复：不存在能复现旧共享状态缺陷的中间结构；提交说明单列此项 | 无 |
| P1-18 | XFAIL 增加 2 条：新用例 region/anchor-kinds 的 html/paged line-spans（代码行缺 span，属已有的 P3-07 类） | 修复"代码块不接受容器锚点"需要回归用例，该用例必须含代码块 | P3-07 |
| P1-18 | 锚点资格：分隔线与 raw 也承载容器标签（T7 C1-1 曾定为今天的规则：从不落在分隔线或 raw，另立 S9b） | finding emitter/anchor-opt-in-per-kind 要求代码块与分隔线接受容器锚点；一个规则覆盖所有叶子更简单；现有 golden 无一变化（语料中无此情形） | 无 |
| P1-18 | 同一叶子上的外层标签作为第二个 id（行首空 span，data-syn="anchor"）输出 | "每个带锚块恰好一个带锚 fragment"；一个元素只能有一个 id；现有用例与 539 篇语料中均未出现（仅 region/anchor-kinds 覆盖） | 无 |
| P1-18 | paged 的 keep-with-next 由叶子特性决定（任何标题叶子的最后一个带），不再只看顶层节点是否 heading | 去掉分页器对内容树的读取；嵌套标题同样应与下文同页；现有 paged golden 不变 | P3-12 |
| P1-18 | baseline 只在 Fragment/DL 中传达，HTML 尚不固定行高 | 计划要求 golden 字节不变；在 HTML 中固定基线属 T7 S11（render-runtime/host-line-height-leak） | P3 |
| P1-18 | dl golden 仅按需（fixture products 含 "dl"，当前 1 个用例）；blocktree/vlist 每个排版用例各一份 | T7 将 dl 定为调试产物；全量 dl golden 只会复制 html golden 的信息 | 无 |
| P1-19 | 计划写"golden：无"，实际 code/tsm-hl.tree 变化 | tsm 的 token 在 Resolve 时由引擎内答复，过去折叠进树；按设计应答不再改写树，tree dump 随之保持纯文本代码体（html/semantic 不变） | 无 |
| P1-19 | gen-res 生成两端的列描述表，编解码器是读表的通用实现（C++ 一份、JS 一份），而不是逐种生成编码函数 | 同一来源、同样的一致性保证，生成代码最少；G9 检查生成表新鲜度，G4/G5 与 fuzz_resanswer 覆盖两端 | 无 |
| P1-19 | codeTokens 应答只有 runs（start,end,tag 三元组，沿用现有 tag 表），没有设计中的 canonLang/classes 列 | class 词表属 T4/T7 的类通道，尚不存在；现有 tag 表即 syntax.def TOKEN_TAGS | P3（类通道） |
| P1-19 | fontFace 种类已入表但从不请求，faceDigest 恒为 0；MetricKey 的 features 只对代码 run 取 code.fontFeatures | fonts.declared 设置尚不存在；按语言的代码 features 不是样式属性 | T4/T9 后续步骤 |
| P1-19 | token 应答校验改为整行失败（任一 token 非法即纯代码 + provider-invalid），P0-11 起是逐个丢弃 | 设计 T9 A1 的规则；垫片 tsr_provide_tokens 仍先滤掉越界 tag（不越界写） | 无 |
| P1-19 | tsr2_requests 增加种类掩码参数 | 语义导出（render.mjs）只能回答 token：只请求能答的种类，避免其余行被判缺失 | 无 |
| P1-19 | measure-failed / provider-missing 诊断按批次与种类聚合，不按 pid | 应答时尚无 pid 归属（按段延迟是 P1-20） | P1-20 |
| P1-19 | 新增设置 host.dppx（HostOnly，affects Measure） | 完整 MetricKey 需要 dppx；默认 1，宿主可设 | 无 |
| P1-19 | 停滞由驱动循环判定（无可请求而未完成），而非新增 Doc::Status::Stalled | 停滞只能在宿主一侧观察（是否还能回答）；worker 立即报错而不是空转 64 轮 | P3-37（tsr2_typeset 状态码） |
| P1-19 | w-only 新用例即 P0-11 的守护用例 figure/w-only（另加单测验证树中作者参数不被改写） | 用例已存在；本步给出结构性修复 | 无 |
| P1-20 | MathTextCtx 的"删除"是改为通用的 MeasureNeeds（store + 记录所缺），不是取消排版时读度量 | 显示公式的盒结构依赖正文字体宽度（T8 拆分结构与宽度在 P3-26）；本步去掉的是侧信道与整篇重新 emit：所缺只延迟所在块 | P3-26 |
| P1-21 | 字体状态以 host.loadedFaces（宿主已加载字体列表）进入 faceDigest，而不是设计中的 fontFace 资源应答 | fonts.declared 与 fontFace 拉取尚未实现；用宿主已知的加载状态同样使键随字体落地而变，无需失效通道 | T4/T9 声明字体 |
| P1-21 | 宽度缓存用两代近似 LRU（当前代过半预算即换代），token 表按条数上限清空 | 实现简单且有界；淘汰只代价一次重新请求 | 无 |
| P1-22 | symbols.tsv 的 tex 列暂为空（-）；不生成设计中的 trie 以外的别名行（AA..ZZ 仍是普通行）；math-vocab.gen.mjs 生成但 tools/convert 尚未改用 | tex 名只供转换器，转换器的词表统一是 P3-24 | P3-24 |
| P1-22 | 删除了 11 条不可达词典行，并且不再编译水平变体链/asmItalic（S1 只说"字节中性"） | finding math/dead-data-and-params 的最后一步就是本步，需整条修复；所有 golden 不变，字形记录集合仍为旧集合的超集 | 无 |
| P1-24 | 未实现设计中的 irOf 记忆化与 declEpoch（解析仍随每次布局进行） | 计划本步要点未列入；按段延迟（P1-20）已去掉整篇重复 emit，惰性布局（P1-25）再统一 | P1-25 |
| P1-24 | Error 叶子以文本字体排出（经拉取测量，同名字）；Sym 槽只接受一个符号记号，暂无点号名（arrow.r 等） | 设计如此；点号名属 P3-24 的词汇身份步骤 | P3-24 |
| P1-25 | 计划写"golden：无"，但公式随其 run 的链接/颜色绘制改变了 inline/bracket-island 的 html（链接里的公式过去不在链接内） | 该用例正是 math/math-leaves-bypass-style 描述的缺陷；更正放在单独提交 | 无 |
| P1-25 | measureStyle 保持今天的测量元组（正文字体按样式字号），paintStyle 只取颜色与链接（容器上着色，文字叶子继承） | 设计 S6 如此；字体族/语言进入数学文字叶子的测量属 T4 字体角色 | P2/P3 |
| P0-07 | D-I03 的节点预算下限从 1M 改为 256K：预算 = max(262144, 64 × 原始节点数)；深度上限 256 不变 | 1M 个 ContentNode 约 90MB，达不到 P0-07 的"峰值内存 < 64MB"验收；64× 原始节点数的项对正常文档仍然宽裕 | P1-03 把它做成 HostOnly 设置时，默认值用 256K |

## 阻塞记录（§4.7）

| 步骤 | 现象 | 已尝试 | 保存位置（stash / wip 分支） | 绕行 |
|---|---|---|---|---|
