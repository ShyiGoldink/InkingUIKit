# 任务书：SpriteGroup 与 AnimateGroup 分家（自身规划）

> 状态：**待执行**（本文件按任务逐条跟踪，完成规则见 §1）
> 日期：2026-11-04
> 来源：2026-11-04 设计讨论（帧事件地基 / 命名对调 / 多块叠加动画组）。核实结论见 §2
> 目标版本：`0.9.0`（全部五批；`docs/zh/CHANGELOG.md:1` 那一节仍是「未发布」）
> 涉及层：`image/`（帧机与两档精灵）· `render/`（合批，只读）· `docs/zh/` · `examples/demo/`

---

## 0. 结论（TL;DR）

两条设计判断要落成两件事：**「一个角色的多个动作」该叫 AnimateGroup**，**「一个 sprite 由多块拼成」才该叫 SpriteGroup**。分家之前先拆帧事件的 reentrancy——后面所有「过渡」都靠它。

| 编号 | 任务 | 批次 | 对应来源 | 落点 | 代价 |
| - | - | - | - | - | - |
| T-01 | 让帧事件派发可在回调里换配置 | 0.9.0-a | 讨论 §过渡 | `src/image/InkingSprite.cpp` | 中 |
| T-02 | 把 `InkingSpriteGroup` 改名为 `InkingAnimateGroup` | 0.9.0-b | 讨论 §命名 | `include/image/` + 12 处引用 | 中 |
| T-03 | 让帧机支持「每帧一组矩形」 | 0.9.0-c | 讨论 §一帧多块 | `include/image/SpriteLike.h` | 中 |
| T-04 | 新增块叠加组的数据面 | 0.9.0-d | 讨论 §数据模型 | 新 `include/image/SpriteGroupData.h` | 中 |
| T-05 | 静态档 `InkingSpriteGroup` | 0.9.0-d | 讨论 §静态档 | 新 `include/image/InkingSpriteGroup.h` | 高 |
| T-06 | 动态档 `InkingDynamicSpriteGroup` | 0.9.0-e | 讨论 §双档 | 新 `include/image/InkingDynamicSpriteGroup.h` | 中 |
| T-07 | 合批断言与 demo | 0.9.0-e | 讨论 §一次 drawcall | `examples/demo/` | 低 |
| T-08 | 文档、CHANGELOG、版本号收口 | 0.9.0-e | 惯例 | `docs/zh/` + `include/inking.h` | 低 |

两条不变量贯穿全部任务：

1. **单块必须退化成 `size() == 1`。** 任何泛化只要让现有三档（`InkingSprite` / `InkingDynamicSprite` / `InkingAnimateGroup`）产生行为差异，就是做错了。
2. **能落在数据层的不落在绘制层。** 格号越界、重名、空表一律在 `Validate()` 拦；绘制层不做兜底分支。

---

## 1. 任务维护规则（本文件的写法约定）

### 1.1 每个任务必须写三段

| 段 | 要求 |
| - | - |
| **准备怎么改** | 决策与选型：做什么、走哪条路、为什么；**放弃的候选要写出来** |
| **大致改动逻辑** | 代码级顺序：改哪个文件、加哪个函数、谁调谁、哪一步不能反。不贴大段代码 |
| **与改动前对比** | 表格；**至少一行「改动后仍不变」的边界**，否则是过度承诺 |

另附 **落点**、**验收**（可断言）、**风险与回退**（至少各一条）。

### 1.2 完成即压缩（核心规则）

任务完成后，把该任务整段压缩成一行：

```
- T-01 <标题>-已完成（<YYYY-MM-DD>，<一句可验证的结论>）
```

删除「准备怎么改 / 大致改动逻辑 / 与改动前对比 / 验收 / 风险」五个细目，并把 §8.2 进度表的状态改为 `已完成`。**两处必须同改。**

### 1.3 四种状态

| 情况 | 写法 |
| - | - |
| 完成 | `- T-01 <标题>-已完成（日期，结论）` |
| 放弃 | `- T-01 <标题>-已放弃（原因；替代方案）` |
| 拆分 | `- T-01 <标题>-已拆分为 T-01a / T-01b`，各自走完整三段 |
| 阻塞 | `状态：阻塞（原因）`，保留全部细目，不压缩 |

### 1.4 完成的定义（DoD）

1. 编译通过（0 warning 为佳）；
2. 验收断言在 `examples/demo/selftest.cpp` 里跑绿（不是临时探针）；
3. 涉及文档同步改；
4. 「与改动前对比」每行都能对上实测。

---

## 2. 基线事实（核实结论）

| 条目 | 判定 | 证据 |
| - | - | - |
| 现有 `InkingSpriteGroup` 是「一个角色多个动作、一个节点换配置」 | 成立（**T-02 已改名为 `InkingAnimateGroup`**） | `include/image/InkingAnimateGroup.h:41-121`；`vector<pair<string, InkingSpriteData>>` + `SwitchTo` |
| 合批按**纹理**分，同纹理连续 N 笔 = 一次 `SDL_RenderGeometry` | 成立 | `src/render/InkingBatch.cpp:174-191`；`textureDraws` 计数 `:92` |
| 一批顶点上限 16384 | 成立 | `src/render/InkingBatch.cpp:19` |
| `contain` 适配绕开批次（要裁剪） | 成立 | `src/render/InkingDraw.cpp:681-692` |
| 图像面**没有**顶点缓存槽（`VertexCacheSlot` 只服务形状填充） | 成立 | `AppendTexturedGeometry` 不碰 `CurrentNode()`，`src/render/InkingBatch.cpp:174-191` |
| 帧事件在**渲染之前**发，回调里切配置视觉零延迟 | 成立 | `advanceFrame` 由 `onAnimationTick` 调、`TickFrame` 在 `Render` 前（`src/scene/InkingScene.cpp:279-326`） |
| `SetSpriteData` 会把相位重对到落点帧，不会在下一帧跳走 | 成立 | `src/image/InkingSprite.cpp:197-209`（`_offsetReady = false` → `ensureTimeBase()`） |
| 帧事件回调里切配置是 UB（遍历 `_sprite.events` 中被替换） | 成立（**T-01 已修**：快照派发 + 切换延后） | `src/image/InkingSprite.cpp:558-587` 边遍历边回调；`:183-205` 整个替换 `_sprite` |
| `resolveFrame` 是文件内纯函数，外部不可用 | 成立 | `src/image/InkingSprite.cpp:36-80`（匿名命名空间） |
| 当前版本号 | 成立 | `include/inking.h:20` = `"0.8.5"`；`docs/zh/CHANGELOG.md:1` = 0.9.0 未发布 |
| `InkingSpriteGroup` 的引用面 | 成立（**T-02 已按此清单全部改完**；实测 14 个文件，比这里多一份 `plans/2026-11-03-uikit-completeness-audit.md`） | **15 个文件**命中（排除 `dist/`、`build*/`）；其中 **13 个要改**：`include/image/InkingSpriteGroup.h`、`src/image/InkingSpriteGroup.cpp`、`include/widget/InkingWidget.h`、`include/image/InkingDynamicSprite.h`、`examples/demo/{main,selftest}.cpp`、`docs/zh/{CHANGELOG,TaskGuide-Dynamic,TaskGuide-G,api/InkingComponents}.md`、`docs/zh/plans/` 两份、`README.md`。另 2 份 `.workbuddy/memory/` 是历史记录，不动 |

**本轮不做技术验证的部分**：多块叠加的一次提交由 T-07 用 `BatchStats::textureDraws` 直接断言，不预设结论。

---

## 3. 第一批：0.9.0-a（帧事件地基）

- T-01 让帧事件派发可在回调里换配置-已完成（2026-11-04，快照派发 + 切换延后。**改前实测**：在 `one@1` 回调里换配置后事件流变成 `one@1,swapTwo@2`——`three@3` 丢了、新配置的 `swapTwo@2` 被提前扫进来，且照发一次报旧帧号 3 的 `frameChanged`；**改后**：`one@1,three@3`、`frameChanged` 不发、切换落在第 0 帧、换完照常推进。`CheckSpriteEvents` 原五段断言不动 + 新增 ⑥ 段六条全绿，`selftest` 末行失败项 0；`nm` 差分只多 `InkingSpriteLike::SetSpriteDataNow` / `applyPendingSpriteData` 两个符号。**一处与本节原验收不同**：`GetFrameAdvanceCount()` 增量实测是 **1** 不是 2——一次 `TickFrame(0.25)` 只落一次帧（0→3 是一跳），切换本身不计，原写的"= 2"与它自己括号里的语义对不上）

---

## 4. 第二批：0.9.0-b（命名归位）

- T-02 把 `InkingSpriteGroup` 改名为 `InkingAnimateGroup`-已完成（2026-11-04，`git mv` 两个文件 + 9 个文件符号替换：类名 / `InkingAnimateGroupData` / `InkingAnimateGroupModuleName`，`InkingWidget.h` 的 friend 与全部 include 同步（改错就编不过，这是本题自带的护栏）；`CheckSpriteGroup` → `CheckAnimateGroup` 含 `main()` 调用点。构建 0 warning、`selftest` 末行失败项 0、`nm` 里旧名 **0 命中** / 新名 18 个；4 份历史文档（`CHANGELOG` 0.6.6 节 / `TaskGuide-G` / `plans/2026-10-25` / `plans/2026-11-03` 审计）按 §4 硬约束**只加注不改写**，`docs/zh/api/InkingComponents.md` 与 `TaskGuide-Dynamic.md` 是当前文档，类名直接改）

---

## 5. 第三批：0.9.0-c（帧机泛化）

- T-03 让帧机支持「每帧一组矩形」-已完成（2026-11-04，`LoadedFrame::sourceRect` → `sourceRects`（矩形表，空 = 不设源矩形）+ `Hooks::setSourceRect` → `setSourceRects(指针, 个数)`；`Hooks` 布局变 = ABI 破坏，仓内两处 `hooksFor` 同步、无外部派生类。**单块退化判据**：现有三档恒 1 个元素、值与改动前逐字节相同——六条精灵自检**一条断言不改**就过（`CheckSpriteSheet` 本身就是离屏逐像素比对，够硬，没另加重复断言）。`applyFrame` 的「切不出矩形就不动源矩形」从判一块扩到判每一块（单块时与原判据等价）。单块档收到多块 = 数据层漏校验：画第一块 + 一条 WARN，不整帧丢。构建 0 warning、`selftest` 末行失败项 0、`ctest` 8 条全绿；`nm` 差分 **+2 / −1**（模板实例与 COMDAT 存在性，无公开符号消失），已按验收记进 `CHANGELOG` 的 0.9.0 节）

---

## 6. 第四批：0.9.0-d（块叠加组：数据面与静态档）

- T-04 新增块叠加组的数据面-已完成（2026-11-04，新文件 `include/image/SpriteGroupData.h` + `src/image/SpriteGroupData.cpp`：`InkingSpriteGroupData` + `InkingSpriteGroupAnimation` + `InkingSpriteGroupModuleName`（**等格是结构保证**——只给网格不给矩形表，"同帧两块大小不同"配不出来；与 `InkingAtlasData` 的取舍相反、分工写进文件头）。`CellRect` 是"格号 → 像素矩形"的唯一算术点（行优先、越界给全零），`SheetWidth/Height` 与 `SpriteSheet` 同公式；`Validate()` 九条按序报（格号越界会报"哪个动画的第几帧第几块"）；`Normalize()` 是**幂等空操作**（这个类型没有能修正的字段，猜 `fps=12` 就是静默兜底）。自检 `CheckSpriteGroupDataValidate()`：合法基线（含幂等、格号重复合法、`249×167` 的 margin/spacing 验算）+ 九条非法各断关键子串 + `FindIndex` 的 `kNotFound` + `CellRect` 的 0/5/6/23/越界/负号六组坐标，全绿。构建 0 warning、`selftest` 末行失败项 0、`ctest` 8 条全绿；`CHANGELOG` 与 `api/InkingComponents.md` 各加一节数据面）

---

- T-05 静态档 `InkingSpriteGroup`-已完成（2026-11-04，按方案 B：`InkingStaticImage` + `InkingSpriteLike` **与 `InkingSprite` 平级**——派生 `InkingSprite` 要在构造期编一份假配置骗过 `InkingSpriteData::Validate`，那就是"为了绕过校验而编数据"。`InkingSpriteLike` 补四个 protected 入口：`replaceFrameTable(每帧一组矩形, keepPhase)`（落帧语义与 `SetSpriteData` 逐条一致）、`setTiming`、`setAutoplay`（构造期设播放标志**不动时间基准**，否则第一次 tick 甩飞）、`setConfigError`（让 `IsValid`/`GetConfigError` 对组有意义）。数据面补 `imageBytes`/`imageSize`（生成期烘图集那条路，缺了它生成器烘的图集用不上）。`onRender` 每块一次 `PaintImage`、同纹理分批 ⇒ **2 块一帧 1 次 `SDL_RenderGeometry`**（不调 `PaintButtonAppearance`，那会把描边按 K 份画）。自检 `CheckSpriteGroup()` 全绿：结构 5 条 / `SwitchTo` 7 条（成功、`(新,旧)`、切当前是空操作、不存在 false + 恰好 1 条 ERROR、切失败状态不变）/ 非法配置 3 条（构造期拦、不装帧表、1 条 ERROR）/ **合批 3 条**（2 块与 1 块 `textureDraws`+`geometrySubmissions` 增量都是 1）/ 像素 3 条（两次读回 400/400 点全不同、上层覆盖顺序对）。**一处与验收 ④ 不同**：任务是"2 块各自落在预期像素上"，但 K 块同框叠图、BMP 不透明，读回只看得到最上层——改判「2 块 vs 1 块两次读回不同」，实现若忽略多块则两次一致、判据照样红。构建 0 warning、`selftest` 末行失败项 0、`ctest` 8 条全绿）

---

## 7. 第五批：0.9.0-e（双档与收口）

- T-06 动态档 `InkingDynamicSpriteGroup`-已完成（2026-11-04，方案 A 先复制：`InkingDynamicWidget` + `InkingImageLike` + `InkingSpriteLike`，与静态档**逐字同源**，差别只有"几何能不能改"与"进不进命中表"。不抽 `InkingSpriteGroupLike`——第二档刚出现就抽、抽出什么全凭猜；判据是自检那条「两档同场景跑 100 帧，帧号 / 换帧 / 跳过逐一相等」。`Resize` 后**不重算格矩形**（等格与组件框无关）。自检 `CheckDynamicSpriteGroup()` 全绿：`Resize(120,90)` 后 `HitTest` 新旧点一假一真、尺寸真改、帧数不变（4 条）；两档 100 帧后 `帧号=1 换帧=100 跳过=0` 逐一相等 + `GetOwnedChildCount()` 都为 0（3 条）；`SwitchTo` 与非法配置与静态档同一个出口（3 条）。**唯一重复点是 `onRender` 那十几行**，先接受。构建 0 warning、`selftest` 末行失败项 0、`ctest` 8 条全绿）

---

### T-07 合批断言与 demo

- **状态**：未开始
- **落点**：`examples/demo/selftest.cpp` 的 `CheckSpriteBatching`（:7798）附近新增一段；`examples/demo/main.cpp` 加一页示例

**准备怎么改**

「一帧 K 块只有一次 drawcall」是这轮的核心收益，必须变成**可断言的数**，不能只写在设计里（`AGENTS.md` §4 第 4 条）。

| 方案 | 做法 | 代价 | 结论 |
| - | - | - | - |
| A 扩 `CheckSpriteBatching` | 在同一条自检里加「多块组」一段 | 复用现成的开关与读回工具 | **推荐** |
| B 另写一条自检 | 独立函数 | 重复 `SetImageBatchingEnabled` 的开关与还原 | 退路 |

**大致改动逻辑**

1. 在 `CheckSpriteBatching` 末尾加一段：建一个 2 块 × 1 帧的组，`BatchStatsSnapshot()` 取前后差值，断言 `textureDraws == 1`、`geometrySubmissions == 1`。
2. 同一个组改成 1 块再测一次，断言 `textureDraws` 仍是 1（对照组：**不是**「块多所以提交多」）。
3. `examples/demo/main.cpp`：加一个用 `walk.png` 的块叠加示例（两个格号叠一帧），菜单项切换动画。素材沿用现有 `walk.png`，**不引入第三方素材**（`tools/THIRD-PARTY-NOTICES.md` 的规矩）。
4. 哪一步不能反：开关必须在 `CheckSpriteBatching` 末尾**还原**（那里已有 `SetImageBatchingEnabled(false)` 的对照段，照抄收尾）。

**与改动前对比**

| 场景 | 改动前 | 改动后 |
| - | - | - |
| 同图集 2 块一帧 | 不存在（一帧一块） | `textureDraws` = 1 |
| 同图集 1 块一帧 | `textureDraws` = 1 | **不变**（对照组） |
| 两块来自不同图集 | 不存在 | 2（**不承诺合批**，跨纹理本来就刷批） |

**验收**：`CheckSpriteBatching` 的新段落打印前后差值并 `Check`；`selftest` 末行「失败项：0」。

**风险与回退**：风险是「一次提交」被别的绘制插进来打断（描边、文字、contain 图都会刷批）。缓解：断言的组里不配描边、不加标签。回退：删掉该段。

---

### T-08 文档、CHANGELOG、版本号收口

- **状态**：未开始
- **落点**：`docs/zh/CHANGELOG.md`、`docs/zh/api/InkingComponents.md`、`docs/zh/API.md`（§3 文档地图加一行指向本任务书）、`README.md`、全部文档页脚、`include/inking.h:20`

**准备怎么改**

版本号现状是 `0.8.5`，而 `CHANGELOG.md:1` 已经有「0.9.0（未发布）」一节。本轮全部进 0.9.0 那节，**不另开 0.9.4**——0.9.1～0.9.3 没有记录，版本跳过去就是空账（`AGENTS.md` §4 第 3 条）。

| 方案 | 做法 | 代价 | 结论 |
| - | - | - | - |
| A 并入 0.9.0 | 在既有「未发布」节里加四个批次小节 | 一节变长 | **推荐** |
| B 另开 0.9.4 | 版本号直接跳 | 0.9.1～0.9.3 无记录 | 不做 |

**大致改动逻辑**

1. `CHANGELOG.md` 的 0.9.0 节内加四小节（帧事件 / 命名 / 帧机 / 新组件），每节写「变更 / 破坏性 / 已知限制」三类里命中的。
2. 破坏性单列：`InkingSpriteGroup` 改名与符号全换、`InkingSpriteLike` 的 `Hooks` 签名变。
3. `InkingComponents.md`：旧节标题改 `InkingAnimateGroup`，新增 `InkingSpriteGroup` / `InkingDynamicSpriteGroup` 两节（照 T-05/T-06 的公开面写）。
4. `API.md` 的头文件索引加两个新文件。
5. `include/inking.h:20` 改 `"0.9.0"`；全部文档页脚一起升（`AGENTS.md` §2 的版本五处一致）。
6. 哪一步不能反：**先等 T-01～T-07 全绿再动版本号**，否则中途变红的构建会被记进「已发布」的历史。

**与改动前对比**

| 场景 | 改动前 | 改动后 |
| - | - | - |
| `InkingVersion` | `"0.8.5"` | `"0.9.0"` |
| 自检启动行 | 打印 `版本 0.8.5-release` | `版本 0.9.0-release` |
| 库的公开面 | 无块叠加组 | 多两个组件、多一个数据面 |
| 既有三档行为 | — | **不变**（T-01～T-03 的验收已逐条钉住） |

**验收**：构建 0 warning；`selftest` 末行「失败项：0」；`ctest` 全绿；`grep -rn "0.8.5"` 只命中 `dist/` 与历史 CHANGELOG。

**风险与回退**：风险是「改了版本号但文档没同步」——`ctest` 里的元自检（`silent_failure_list_in_sync` 一类）能抓一部分。回退：版本号单独一个提交，revert 不影响代码。

---

## 8. 批次门槛与进度

### 8.1 门槛

| 批次 | 发布前必须满足 |
| - | - |
| 0.9.0-a | `CheckSpriteEvents` 六段全绿（含新增 ⑥）；`nm` 差分只出现在 `InkingSpriteLike` 内部符号上 |
| 0.9.0-b | 构建 0 warning；`selftest` 失败项 0；`nm` 里旧名零命中；`grep` 复核清单为空 |
| 0.9.0-c | 五条既有精灵自检**一条断言不改**就过；`textureDraws` 行为不变（`CheckSpriteBatching` 原段） |
| 0.9.0-d | `CheckSpriteGroupDataValidate` + `CheckSpriteGroup` 全绿；`textureDraws == 1` 断言跑在自检里 |
| 0.9.0-e | `CheckDynamicSpriteGroup` 全绿（含两档帧号恒等）；demo 有块叠加示例；版本五处一致；`ctest` 全绿 |

### 8.2 进度表

| 编号 | 任务 | 批次 | 状态 |
| - | - | - | - |
| T-01 | 让帧事件派发可在回调里换配置 | 0.9.0-a | 已完成 |
| T-02 | 把 `InkingSpriteGroup` 改名为 `InkingAnimateGroup` | 0.9.0-b | 已完成 |
| T-03 | 让帧机支持「每帧一组矩形」 | 0.9.0-c | 已完成 |
| T-04 | 新增块叠加组的数据面 | 0.9.0-d | 已完成 |
| T-05 | 静态档 `InkingSpriteGroup` | 0.9.0-d | 已完成 |
| T-06 | 动态档 `InkingDynamicSpriteGroup` | 0.9.0-e | 已完成 |
| T-07 | 合批断言与 demo | 0.9.0-e | 未开始 |
| T-08 | 文档、CHANGELOG、版本号收口 | 0.9.0-e | 未开始 |

完成任务后，本节状态改 `已完成`，§3～§7 对应段落按 **§1.2** 压缩成一行——两处必须同时改。

---

## 9. 明确不做（避免反复讨论）

| 不做的事 | 原因 |
| - | - |
| 「在上一个动画的第 n 帧切换」作为接口参数 | 帧事件 + `(previous, current]` 区间语义已经等价实现：配 `frame: n`，画面就是「第 0..n−1 帧旧的，第 n 帧起新的」。再开一个参数就是同一件事的第二个名字 |
| 每槽独立相位（身体 12fps、眼睛 6fps） | 「省图片」同步模型已经拿到（格号复用）；独立相位省的是配置，代价是「一帧」失去唯一含义。它属于分层时间轴，是另一个数量级 |
| 切片间过渡（淡入淡出、位置插值） | 等尺寸同原点，切动画只换 UV，不产生位置突变；「丝滑」问题在结构上不存在 |
| 每块独立 tint / 闪白 | `fillShapeTextured` 的 `tintColor` 只取 alpha（`include/render/InkingDraw.h:156`）；要整组变色得改绘制层，另开任务 |
| 每槽独立命中（点眼睛） | 等格同原点的 K 块共用一个矩形，分开命中没有语义 |
| 多块叠加进 `InkingSpriteData` | 那会把「等格」这条硬约束扩散进现有三档的配置；新数据面已经分离 |
| pending 切换排队（多个待定） | UI 语义是「这一帧算一句话」，后设覆盖先设；队列会让「切了没反应」变成常态 |
| 帧事件回调里把组件自身析构 | 这是既有问题（`Emit` 快照只挡得住容器），修它要动生命周期，不属于本轮 |
