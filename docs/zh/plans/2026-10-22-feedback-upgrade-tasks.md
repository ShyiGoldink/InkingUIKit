# 任务书：外部反馈（0.1.0-test）升级任务拆分

> 状态：**已完成**——第一批（T-01~T-06，0.1.1）、第二批（T-07~T-12，0.2.0）与第三批（T-13~T-14，0.3.0）全部落地（本文件按 §1 的规则逐条跟踪并压缩）  
> 日期：2026-10-22  
> 来源：外部使用者对 `inking 0.1.0-test` 的 12 条反馈。逐条核实结论见 `.workbuddy/memory/2026-10-21.md`  
> 目标版本：`0.1.1`（第一批，静默失效清零）/ `0.2.0`（第二批，能力补齐）/ `0.3.0`（第三批，字体与巡检）  
> 涉及层：`widget/`（主）、`text/`、`panel/`、`button/`、`render/`、`detail/InkLog`、`tools/inkgen`、`cmake/`、`CXXCSS.md`

---

## 0. 结论（TL;DR）

反馈的 12 条里 **9 条成立、2 条部分成立、1 条不成立**（4.4 的接口其实在 `docs/zh/UsageReport.md:72-82`，缺的是 `API.md` 入口）。成立的条目拆成 **14 个任务、3 个批次**：

| 编号 | 任务 | 批次 | 对应反馈 | 落点 | 代价 |
| - | - | - | - | - | - |
| T-01 | 内容驱动组件在无布局容器时自测量 | 0.1.1 | 2.1（P0） | `widget/InkingWidget`、`text/InkingStaticLabel` | 中 |
| T-02 | 尺寸为零的统一护栏 | 0.1.1 | 2.1 | `widget/`、`text/`、`panel/`、`button/` | 低 |
| T-03 | 面板全透明兜底改品红并告警 | 0.1.1 | 3.1 | `panel/PanelData`、`button/ButtonData`、`inkgen` | 低 |
| T-04 | 字体打开失败告警 | 0.1.1 | 3.3(b) | `render/InkingText` | 低 |
| T-05 | 日志：级别开关 / 去重 / 分级计数 | 0.1.1 | 3.4 | `CMakeLists.txt`、`detail/InkLog`、`window/InkingWindow` | 中 |
| T-06 | 文档补三处（配方 / 前置校验 / 观测口入口） | 0.1.1 | 1.2、4.3、4.4 | `CXXCSS.md`、`README`、`docs/zh/API.md` | 低 |
| T-07 | 抽公共绘制件 + 面板 stroke/shadow | 0.2.0 | 3.2 | `button/ButtonPaint`、`panel/`、`widget/InkingWidget`、`scene/` | 高 |
| T-08 | `inkgen --dump-fields` + 防漂移测试 | 0.2.0 | 4.3 | `tools/inkgen`、`CXXCSS.md` | 低 |
| T-09 | `inkgen --dir`（解非 ASCII 路径） | 0.2.0 | 4.2 | `tools/inkgen/main.cpp`、`cmake/inking-inkgen.cmake` | 低 |
| T-10 | `$extends` 单层浅合并 | 0.2.0 | 4.1 | `tools/inkgen/Cxxcss.cpp` | 中 |
| T-11 | 公开 `stateChanged` 事件 + 两档差异表 | 0.2.0 | 3.5 | `button/InkingButtonLike.h`、`CXXCSS.md` | 低 |
| T-12 | 输入框光标 clamp + 水平滚动 | 0.2.0 | 1.2（残留） | `text/InkingTextInput` | 中 |
| T-13 | 字体回退链 `fontPaths` | 0.3.0 | 3.3(a) | `dataStruct/InkingTextLayout.h`、`render/InkingText` | 中 |
| T-14 | 静默失效清单化 + 逐条自检 | 0.3.0 | 全部（元任务） | `AGENTS.md` §6、`examples/demo/selftest.cpp` | 中 |

设计取向有两条不变：**能落在 Widget 层的不落在组件层**（T-01、T-02）；**先修行为、再加护栏**（T-01 在 T-02 之前）——否则护栏会被新行为绕过，出现"日志说没问题、画面还是空"的第三种错。

---

## 1. 任务维护规则（本文件的写法约定）

### 1.1 每个任务必须写三段

| 段 | 要求 |
| - | - |
| **准备怎么改** | 决策与选型：做什么、走哪条路、为什么选它、放弃了哪些候选。不做无理由的选择 |
| **大致改动逻辑** | 代码级顺序：改哪个文件、加哪个函数、步骤之间的依赖。不贴大段代码，只写"谁调谁、先谁后" |
| **与改动前对比** | 行为 / 接口 / 成本的对照表。**必须写"改动后仍然不变的边界"**，否则读者会以为它全好了 |

另附 **落点**、**验收**、**风险与回退**。验收必须是可断言项（自检函数或探针输出），不能是"看起来对"。

### 1.2 完成即压缩（核心规则）

任务完成后，**把该任务整段压缩成一行**，只保留编号、标题、完成标记、日期与一句关键结论：

```
- T-01 内容驱动组件在无布局容器时自测量-已完成（2026-10-2X，标签 200×0 变 200×27.5，文档 §3.8 同步改口径）
```

压缩时**删除**该任务的「准备怎么改 / 大致改动逻辑 / 与改动前对比 / 验收 / 风险」五个细目，并在 §6 的进度表里把状态改为 `已完成`。

细目不留在本文件里——它已经在 git 历史与该任务的 commit message 中，需要回看就 `git log --grep=T-01`。本文件只承担两件事：**没做完的写清楚，做完的只留一行**。

### 1.3 四种状态

| 情况 | 写法 |
| - | - |
| 完成 | `- T-01 xxx-已完成（日期，一句结论）` |
| 放弃 | `- T-01 xxx-已放弃（原因；替代方案）` |
| 拆分 | `- T-01 xxx-已拆分为 T-01a / T-01b`，原任务行改为这两行，各自走完整三段 |
| 阻塞 | `状态：阻塞（原因）`，保留全部细目，不压缩 |

### 1.4 完成的定义（DoD）

四条全中才算完成，缺一条保持 `未开始` / `进行中`：

1. 编译通过（三级验证的 ② 号：`g++ -std=c++20 -I include -c`）；
2. 该任务的验收断言在自检里跑绿（不是临时探针一次性通过）；
3. 涉及文档同步改（`CXXCSS.md` / `API.md` 的对应段落）；
4. 「与改动前对比」表里的每一行都能对上实测——对不上的行，要么改代码，要么改表并在结论里说明。

---

## 2. 基线事实（三条硬证据）

这三条是整份任务书的前提，均已实测，不再讨论：

1. **标签 200×0 / 测量 76×27.5**。探针链接发布包实测：`InkingStaticLabel` 构造后 `GetHeight() == 0`，而 `MeasureLayout()` 报 27.5。`PaintText` 头一行 `width <= 0 || height <= 0` 直接返回（`src/render/InkingText.cpp:869`）。
2. **面板全透明兜底成 `0xFF000000`**，且零日志（`include/panel/PanelData.h:168`）。
3. **日志那条 ×528 的消息本来就是 DEBUG**（`src/button/InkingStaticButton.cpp:95`）；噪声的根子是 `CMakeLists.txt:96` 把 `INK_ISDEBUG=1` 写成 **PUBLIC**，消费方无法关掉。

P0 的根因链（比反馈判断更深一层）：`ArrangeSubtree` 只在容器 `LayoutSpec::Valid()` 时问 `MeasureLayout`（`src/layout/InkingLayoutBinding.cpp:29`），而 `RelayoutParent()` 找不到"声明了布局的祖先"就**静默返回**（`src/widget/InkingWidget.cpp:547`）。所以这是"内容驱动尺寸 + 无布局容器"这一整类组合失效，标签只是第一个撞上的。

---

## 3. 第一批：0.1.1（静默失效清零）-已完成

六条全部落地（2026-10-22），验收断言已进 `examples/demo/selftest.cpp` 并全绿
（`== 失败项：0 ==`，新增 14 条断言）。细目见 `git log --grep=T-01` ~ `T-06`；
逐条结论如下（按 §1.2 压缩，只留一行）：

- T-01 内容驱动组件在无布局容器时自测量-已完成（2026-10-22，`RelayoutParent` 找不到布局祖先时改走 `RelayoutSelfIfContentSized`，只补零的那一维；标签 200×0 → 200×27.5 级，`CXXCSS.md` §3.8 同步改口径）
- T-02 尺寸为零的统一护栏-已完成（2026-10-22，`WarnIfZeroSize(module)` 落在 Widget 层，标签/面板/两档按钮四处共用一套文案；纯容器仍刻意不查）
- T-03 面板全透明兜底改品红并告警-已完成（2026-10-22，兜底 `0xFF000000` → `0xFFFF00FF` + WARN，`PanelData` 与 `ButtonData` 两条规矩一起改）
- T-04 字体打开失败告警-已完成（2026-10-22，`AcquireFont` 失败分支 WARN 带路径/字号/`SDL_GetError`，`ResolveFontPath` 候选全落空另报一条；`PaintPlaceholder` 行为不变）
- T-05 日志：级别开关 / 去重 / 分级计数-已完成（2026-10-22，`INK_ISDEBUG` 改 `option(INKING_DEBUG_LOG)` 默认 OFF 但**仍为 PUBLIC**（公开头有 `inline constexpr` 分支，改 PRIVATE 即 ODR 违例）；DEBUG 走独立折叠槽；WARN/ERROR 不再折叠；新增 `CountOf` / `ResetStats`）
- T-06 文档补三处-已完成（2026-10-22，`CXXCSS.md` §3.8.1 输入框配方 + 新增 `CXXCSS/Panel/inputBox.json`；`README` 加"路线 0：先 validate"；`API.md` / `api/InkingScene.md` / `api/InkingWidget.md` 补观测口入口与"哪些 setter 会自动重排"表）

两条设计取向已按原计划执行：能落在 Widget 层的不落在组件层（T-01、T-02 都落在
`InkingWidget`）；先修行为、再加护栏（T-01 先于 T-02）。

**与任务书的偏离**（三条，都已经在代码注释里写清理由）：

1. **T-05 的 `INK_ISDEBUG` 没有改成 PRIVATE**。任务书原话是"PUBLIC 只保留 `INK_ISLOG`，
   `INK_ISDEBUG` 移入 option、PRIVATE 打开"。做不了：`include/detail/InkLog.h` 里
   `kDefaultLogLevel` / `kDebugEnabled` 是 `inline constexpr`，库与消费者取值不同就是
   ODR 违例；改成 PRIVATE 之后库打出调试流、调用方的宏却是空的。正确解法是
   **默认 OFF、仍 PUBLIC**——同样达到"发布包不再吃调试流"的目标。
2. **T-05 的"去重"没有做成"在原行追加 ×N"**。原地回写要重写文件（或预留定长占位），
   代价与收益不成比例。实际做法是**DEBUG 独立折叠槽**：正文 1 行 + 结算时 1 行
   `FOLD ×N`（带 first~last）。这对"重复 528 次"的实测噪声有效，而原来的共用槽对它
   完全无效（被任何别的记录打断）。
3. **T-02 的护栏查四个组件而不是三个**：动态按钮也有同一段文案（`InkingDynamicButton.cpp:71`），
   一起收了，否则"同一个错误在两档按钮上措辞不同"。

**已知不做的边界**（写清楚免得后人以为是漏）：

- 自测量只在**没有布局祖先**时发生；布局容器内仍由布局给尺寸。
- 空文本标签不报 0 尺寸（它本来就不占地方）。
- 第二批（T-07 ~ T-12）见 §4、第三批（T-13 / T-14）见 §5，均已同日完成。

## 4. 第二批：0.2.0（能力补齐）-已完成

- T-07 抽公共绘制件 + 面板 stroke/shadow-已完成（2026-10-22，`detail::PaintSurface` 三笔（阴影→填充→描边）落在 `ButtonPaint.h`，按钮/图片/面板共用一份；描边走**装饰遍** `onRenderDecoration`（按"子树结束"落笔）⇒ 压得住子孙、不越界压上层兄弟；面板 `stroke`/`shadow` 进两态外观并随过渡插值；`GetDrawItemCount` 与命中表条目数不变；自检新增 12 条断言全绿）

**T-07 与任务书的三条偏离**（都已在代码注释里写理由）：

1. **阴影没有进装饰遍**（任务书写"装饰遍只画描边与阴影"）。阴影是"形状之下的偏移副本"——
   放装饰遍会画在**所有填充与子节点之上**，等于给内容盖一层半透明块。实际分层：
   阴影留在填充那一趟（`onRender` 里先画），只有描边进装饰遍。
2. **装饰遍不是"全树第二趟"，而是"子树结束时落笔"**（任务书方案 A 写的是两趟全树遍历）。
   两趟全树会让**所有**描边都压在所有填充之上——包括层级更高的兄弟子树，那是把"局部层级"
   又改回全局。实现改成待落栈：绘制列表是 DFS 先序（每棵子树连续一段），遍历时发现当前
   节点已不在装饰者的子树里就落笔。自检里专门有一条锁这个（兄弟盖住边框时是兄弟的颜色）。
3. **面板透明白兜底加了豁免**：有可见描边或阴影时不兜底成品红（"透明底 + 一圈描边"是正当
   配置）。`PanelData::Normalize` 与 `ButtonData::Normalize` 一起改，两条规矩保持一致。

- T-08 `inkgen --dump-fields` + 防漂移测试-已完成（2026-10-22，白名单集中到 `tools/inkgen/Fields.h`（数组顺序原样，报错文案由它拼）；`--dump-fields` 输出「组件×段×字段」三层表（154 行），`CXXCSS.md` §9 锚点块由它生成；ctest `field_table_in_sync` 逐字比对（删一行必红，已实测）。**一处文案变化**：`transition` 那条从"duration 与 easing"改成由数组拼接的"duration、easing"——为了消掉第二份定义）

- T-09 `inkgen --dir`（解非 ASCII 路径）-已完成（2026-10-22，`--dir <CXXCSS 根>` 扫组件目录 + 根下 `config.json`、跳过 `*.example.json`、打印扫描清单；与显式文件列表**互斥**；`--validate` 同支持；两个 `ink_add_generated` 都加了 `DIR` 分支（仓库自己的 `inkgen_compile_check` 已改走 `--dir`）。**根因比任务书深两层**：MinGW 下 `fs::path(窄串)` 用 "C" locale 的 codecvt，遇到中文直接抛 `Illegal byte sequence`（`--dir` 都会崩）；且 `path.string()` 返回 UTF-8 而 `ifstream(窄串)` 走 ANSI。新增 `tools/inkgen/PathUtil.h`（`ToPath`/`ToNarrow`/`ToUtf8`）把三个方向钉死，路径全链路按宽字符走。实测：中文目录下 configure+build 通过、产物头 413 行与 ASCII 路径一致且是合法 UTF-8）

- T-10 `$extends` 单层浅合并-已完成（2026-10-22，`ApplyExtends` 在 `LoadConfigInto` 里**先合并再校验**；只合并顶层键、不做深合并、不支持链式；自引用/父不存在/链式三类报错都是退出码 1；`JsonValue` 加 `file` 让**父文件里的错指到父文件的行**（实测 `badBase.json:6`）；生成物注释标"$extends 继承自 X.json"；`CXXCSS.md` §4.6 新增。等价性实测：同一份配置「extends 写法」与「手写展开」生成的产物逐字节相同）

- T-11 公开 `stateChanged` 事件 + 两档差异表-已完成（2026-10-22，`InkingButtonLike::stateChanged`（`InkingEvent<ButtonState,ButtonState>`）在 `OnStateChanged` 里与 protected 钩子同一位置 Emit ⇒ **两档自动都有**、状态没变不重复发；`CXXCSS.md` §4.3 补五点差异表（采样时机 / 命中 / 遮挡 / 几何写入口 / 过渡推进）与"回调同步触发、别在回调里改同一个按钮"的约束；自检 `button_state_event` 两档各 1 条断言绿）

- T-12 输入框光标 clamp + 水平滚动-已完成（2026-10-22，`_scrollX` 参与 `BoxForOffset`（减）/`OffsetAtPoint`（加）**成对抵消**，并把光标矩形夹进 `[0, width]`；`EnsureCaretVisible` 只在跨边界时动（End/输入/点击/拖选/换文本/取焦点六处触发）；滚动只影响绘制 ⇒ **只标重绘脏**（代码注释写明"漏了就是滚动量变了画面不动"）；基类加 `TextScrollX()` 虚钩子，标签绘制用它、排版与命中不受影响；自检 `input_caret_clamped` + `input_scroll_mapping` 等 7 条断言绿。滚出本体的那部分靠面板 `clipChildren` 裁——文档 §3.8.1 写明两条要一起用）

---

## 5. 第三批：0.3.0（字体与巡检）-已完成

- T-13 字体回退链 `fontPaths`-已完成（2026-10-22，`TextStyle::fontPaths` / `TextRequest::fontPaths` / `ButtonTextData::paths` 三段贯通；链 = `fontPath` + `fontPaths` 去重、逐条静默试开，**解析结论按链缓存**（键不含字号）⇒ 排版与绘制两趟合用一条 WARN；字体 / 字形图集 / 整段纹理三种缓存键一律换成**实际命中的那条路径**；`text.path` 认字符串数组（首元素进主字段、其余进链尾，非字符串与空串按项报错）；自检 `font_fallback_chain` 8 条断言绿）

- T-14 静默失效清单化 + 逐条自检-已完成（2026-10-22，清单落在 `examples/demo/selftest.cpp` 的 `kSilentFailures`（8 条）与 `docs/AGENTS.md` 新增 §7 的锚点块，两处靠 `NoteSilentFailure("名字")` 登记对齐；补出三条"既有但没断言"的：`theme_token_missing`（退回兜底色 + miss +1，未装表时不计 miss）、`scroll_range_stale`（内容 180 / 视口 100 ⇒ 范围 = 内容 − 视口，`SetScroll` 按它夹住）、`text_negative_width`（可排宽度为负不换行；负配置夹 0 后按内容补）；元自检 `silent_failure_list_synced` 对账"清单里每条都真的跑过且恰好一次"，ctest `silent_failure_list_in_sync` 对账"文档表 ↔ 源码登记"）

**0.3.0 门槛实测**：字体缺失场景（两条候选全打不开）整条链路（排版 + 绘制）合计
**1 条 WARN**；`silent_failure_list_synced` 9 条断言绿；人为删掉 `NoteSilentFailure("input_caret_clamped")`
之后运行期自检红 2 条、ctest 同时红（两条通道都咬得住）。

**T-13 与任务书的两条偏离**（都写进了代码注释与 `CXXCSS.md` §3.8）：

1. **整条链全打不开时，不退回机器默认字体**（任务书原话是"全失败才走机器默认"）。
   两条理由：① 静默换一份字体 = "看起来正常、其实不是你要的字体"，比方块难发现得多，
   与本轮"静默失效清零"的主题正好相反；② 那样一来 T-04 的断言（"打不开产生 1 条 WARN"）
   在机器默认可用的机器上会变成 0 条 —— 测试与实现互相打架，说明这条路本来就不该走。
   现在的行为：**占位方块 + 1 条汇总 WARN（列出链上每一条路径）**。
2. **"中文用雅黑、数字用等宽"这条对比行不成立**，改表不改代码：回退链是**文件级**的
   （整块文字只用一个字体文件），同屏混排需要"按字形覆盖范围逐个选字体"，而 SDL_ttf
   只有 `TTF_OpenFont(path)`，没有族名 / 覆盖范围查询。链解决的是"文件不一定在哪台机器上"。

**已知边界**（写清楚免得后人以为是漏）：

- 链上"前面没命中、后面命中"**不报警**（那正是回退链的用途）；只有整条链全失败才报；
- 链解析结论按**链**缓存（不含字号），所以运行期新装了一份字体不会被重新发现——
  字体是构建期资源，不是运行期资源；
- `theme_token_missing` / `scroll_range_stale` / `text_negative_width` 三条只补断言、
  **不改行为**（它们本来就按文档在做）。

---

## 6. 批次门槛与进度

### 6.1 门槛

| 批次 | 发布前必须满足 |
| - | - |
| 0.1.1 | T-01~T-06 的验收断言全绿；用反馈者的 60 节点工程复跑（`--validate` + 构建 + 运行），`Log.html` 中 WARN 数量 ≤ 5 且每条都能解释 |
| 0.2.0 | `panel_stroke_over_children` 真渲染通过且 `GetDrawItemCount()` 不变；中文路径构建通过；`field_table_in_sync` 绿 |
| 0.3.0 | 字体缺失场景只产生 1 条 WARN；`silent_failure_list_synced` 绿 |

### 6.2 进度表

| 编号 | 任务 | 批次 | 状态 |
| - | - | - | - |
| T-01 | 内容驱动组件在无布局容器时自测量 | 0.1.1 | 已完成（2026-10-22） |
| T-02 | 尺寸为零的统一护栏 | 0.1.1 | 已完成（2026-10-22） |
| T-03 | 面板全透明兜底改品红并告警 | 0.1.1 | 已完成（2026-10-22） |
| T-04 | 字体打开失败告警 | 0.1.1 | 已完成（2026-10-22） |
| T-05 | 日志：级别开关 / 去重 / 分级计数 | 0.1.1 | 已完成（2026-10-22） |
| T-06 | 文档补三处 | 0.1.1 | 已完成（2026-10-22） |
| T-07 | 抽公共绘制件 + 面板 stroke/shadow | 0.2.0 | 已完成（2026-10-22） |
| T-08 | `inkgen --dump-fields` + 防漂移测试 | 0.2.0 | 已完成（2026-10-22） |
| T-09 | `inkgen --dir` | 0.2.0 | 已完成（2026-10-22） |
| T-10 | `$extends` 单层浅合并 | 0.2.0 | 已完成（2026-10-22） |
| T-11 | 公开 `stateChanged` 事件 | 0.2.0 | 已完成（2026-10-22） |
| T-12 | 输入框光标 clamp + 水平滚动 | 0.2.0 | 已完成（2026-10-22） |
| T-13 | 字体回退链 `fontPaths` | 0.3.0 | 已完成（2026-10-22） |
| T-14 | 静默失效清单化 | 0.3.0 | 已完成（2026-10-22） |

任务完成后，本节状态改 `已完成`，§3~§5 中的对应段落按 **§1.2** 压缩为一行——两处必须同时改，否则会出现"进度表说完成、正文还是三段细目"的不一致。

---

## 7. 明确不做（避免反复讨论）

| 不做的事 | 原因 |
| - | - |
| 面板专用"装饰层"（可相对节点定位的描边系统） | 会造出第二套定位语义，与锚点 + 布局并行；T-07 的装饰遍已覆盖需求 |
| `SurfaceSpec` 抽象基类 | 两个使用者还不足以支撑抽象；先抽**函数**（`PaintSurface`），等第三个使用者出现再谈类型 |
| 字体**族名**解析 | SDL_ttf 无对应 API；自行枚举系统字体属另一项工程，且破坏"只依赖 SDL"的约束 |
| CXXCSS 选择器 / 级联 / 继承 | `$extends` 单层已覆盖反馈的真实痛点；完整级联等于重写一套 CSS 求值器 |
| 让动态档进命中表 | 动态档的前提就是几何每帧变、无法提前登记（`docs/InputDesign.md` §7）；改这个是架构翻案，不是修 bug |
