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

### T-04 新增块叠加组的数据面

- **状态**：未开始
- **落点**：新文件 `include/image/SpriteGroupData.h` + `src/image/SpriteGroupData.cpp`

**准备怎么改**

数据面要表达的是「一张图集 + 多个动画 + 每帧一组格号」。三条路：

| 方案 | 做法 | 代价 | 结论 |
| - | - | - | - |
| A 自己声明网格字段 | `source` + `frameWidth/Height/columns/rows/margin/spacing` | 六个字段与 `SpriteSheet` 重复 | **推荐** |
| B 复用 `SpriteSheet` | 组里嵌一个 `SpriteSheet` | 它带 `frameRects` / `count` / `firstFrame`，全是「按帧」的概念，语义半对 | 退路 |
| C 复用 `InkingAtlasData` | 现有图集描述 | 它的帧是矩形表（**宽高可以不等**），跟「等格」正好相反 | 不做 |

选 A。**等大做成结构保证**，不做运行期校验：只给网格，不给矩形表，于是「不等大」根本配不出来。这条要写进文件头注释——它跟 `InkingAtlasData` 的取舍相反，两个概念并存时必须靠注释分清。

结构（字段名即接口）：

```cpp
struct InkingSpriteGroupAnimation {
    std::string name;
    std::vector<std::vector<int>> frames;   // 每帧 = 格号列表；顺序 = 叠加顺序
    double fps = 12.0;
    bool loop = true;
    std::vector<InkingSpriteData::SpriteEvent> events;
};

struct InkingSpriteGroupData {
    std::string source;                     // 图集路径（纹理缓存键）
    int frameWidth = 0, frameHeight = 0;
    int columns = 0, rows = 0;
    int margin = 0, spacing = 0;
    std::vector<InkingSpriteGroupAnimation> animations;
    static constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);
    std::size_t FindIndex(const std::string&) const noexcept;
    int ResolveCount() const noexcept;      // columns × rows
    InkingShapeBounds CellRect(int cell) const noexcept;
    int SheetWidth() const noexcept;
    int SheetHeight() const noexcept;
    void Normalize();
    std::string Validate() const;
};
```

几条已经定死的语义：

- **格号允许重复**（同一格可以在同一帧出现两次，业务自己负责）。不查重——查重会把「故意叠两层做发光」这种用法挡掉。
- **每帧块数允许不同**（不强制槽位固定）。配置省一半；代价是顶点数每帧可变，而反正每帧都要 `BuildShapeMesh` K 次。
- **格号越界归 `Validate()`**，不夹取。「改到哪一格」猜不出来。
- **`Normalize()` 只补 `count`-类默认值，不做夹取**——这个类型没有可夹的字段。

**大致改动逻辑**

1. 建 `include/image/SpriteGroupData.h`：四个类型（上面的两个结构 + 模块名常量）。
2. `CellRect(cell)`：`column = cell % columns`、`row = cell / columns`，返回 `{margin + column*(frameWidth+spacing), margin + row*(frameHeight+spacing), frameWidth, frameHeight}`。越界返回全零矩形（与 `SpriteSheet::CellRect` 同一套判据）。
3. `Validate()` 九条，顺序即报告顺序：`source` 空 / 网格字段 `<= 0` / 动画表空 / 动作名为空 / 重名 / `fps <= 0` / `frames` 空 / 格号越界（要报「哪个动画的第几帧第几块」）/ 事件帧越界。
4. `SheetWidth()` / `SheetHeight()`：与 `SpriteSheet` 同公式，供构造期与真实图尺寸比对。
5. `FindIndex` 线性查，找不到返回 `kNotFound`（**不是 0**）。

**与改动前对比**

| 场景 | 用 `InkingSpriteData` | 用 `InkingSpriteGroupData` |
| - | - | - |
| 一张图一个动作、帧宽高不等 | 行（`frameRects`） | **不行**（等格是硬约束） |
| 一张图多个动作、每帧一块 | 行 | 行（每帧一个格号） |
| 一张图多个动作、每帧多块叠加 | 不行 | 行 |
| 序列帧（多张图） | 行 | **不行**（只支持一张图集） |
| 现有三档的配置与校验 | `InkingSpriteData::Validate()` 九条 | **不变**（新类型独立，不碰它们） |

**验收**

`examples/demo/selftest.cpp` 新增 `CheckSpriteGroupDataValidate()`：九条判据各造一份非法配置，断言 `Validate()` 返回的说明里含关键子串（与现有 `CheckSpriteDataValidate` :4658 同一套写法）；合法配置断言返回空串；`CellRect(0)` / `CellRect(最后格)` / `CellRect(越界)` 三组坐标断言 `margin` / `spacing` 生效与越界返回全零。

**风险与回退**

- **风险**：与 `InkingAtlasData` 语义相反（一个要求等格，一个允许不等），配置层可能混用。缓解：文件头注释写明分工，且 `Validate()` 在 `source` 指向的图尺寸不符时报错（T-05 做）。
- **回退**：新文件、无既有调用者，删掉即可。

---

### T-05 静态档 `InkingSpriteGroup`

- **状态**：未开始
- **落点**：新文件 `include/image/InkingSpriteGroup.h` + `src/image/InkingSpriteGroup.cpp`；`include/image/SpriteLike.h` 与 `src/image/InkingSprite.cpp` 加两个 protected 入口

**准备怎么改**

三条路：

| 方案 | 做法 | 代价 | 结论 |
| - | - | - | - |
| A 派生 `InkingSprite` | 白拿帧机/帧事件/状态机 | 它构造要一份 `InkingSpriteData`，得造一份「假配置」让基类校验过关再覆盖帧表；脏 | 退路 |
| B 派生 `InkingStaticImage` + `InkingSpriteLike` | 与 `InkingSprite` 平级，自己走「注入帧表 + 时间参数」 | 复刻 `onAnimationTick` 转调与 `hooksFor`（约 30 行接线） | **推荐** |
| C 组合一个 `InkingSprite` 成员 | 零继承 | `InkingSpriteLike` 构造是 protected，组合不合法 | 不做 |

选 B。A 的「假配置」会在构造期多跑一次 `loadFrameBytes` + 一次会失败的 `validateSheetGeometry`，而且那份假配置必须刚好让 `Validate()` 通过——这是典型的「为了绕过校验而编数据」，`AGENTS.md` 第 30 条最忌。

B 需要一个「不经过 `InkingSpriteData` 校验就能装帧表」的入口，这就是 T-03 之外要补的两个 protected 方法：

```cpp
/// 用「每帧一组矩形」整体替换帧表。现有三档从不调。
void replaceFrameTable(std::vector<std::vector<InkingShapeBounds>> perFrame);

/// 只装时间参数（fps / loop / startFrame / events），不校验素材、不建帧表。
void setTiming(double fps, bool loop, std::size_t startFrame,
               double startOffsetSeconds,
               std::vector<InkingSpriteData::SpriteEvent> events);
```

`replaceFrameTable` 把每帧造成一个 `LoadedFrame{data=nullptr, size=0, name=图集名, sourceRects=perFrame[i]}`，`_mode = FrameMode::Sheet`；`setTiming` 只填 `_sprite` 的那几个字段，不动 `_frames`。

**硬约束**：`onRender` 里 K 次 `fillShapeTextured`，同一帧**只产生一次** `SDL_RenderGeometry`。

**大致改动逻辑**

1. `SpriteLike.h` + `InkingSprite.cpp`：加 `replaceFrameTable` / `setTiming` 两个 protected 入口（T-03 的地基之上）。
2. `SpriteGroupData.h` 已就绪（T-04）。`InkingSpriteGroup` 构造：`_group = data;` → `_group.Normalize()` → `_groupError = _group.Validate()` → 取 `initialAnimation`（空 = 第 0 个，名字不存在则退回第 0 个 + 1 条 ERROR）。
3. 构造基类：`InkingStaticImage` 吃一份 `InkingImageData`（`normal.source = _group.source`、`runtimeAsset = true`、形状默认 `Rect`）；`InkingSpriteLike` 吃 host / `static_cast<InkingImageLike&>(*this)` / `hooksFor(*this)`。
4. 构造体：`setTiming(...)` + `replaceFrameTable(把当前动画的格号表转成矩形表)` → 落第 0 帧。**顺序不能反**：`setTiming` 要在 `replaceFrameTable` 之前，因为后者末尾会 `applyFrame` 并读 `_sprite.fps`。
5. `hooksFor` 里 `setSourceRects` 绑成「把这一组矩形存进 `_frameRects`（成员 `vector<InkingShapeBounds>`）并 `MarkRepaintDirty()`」——**不走** `InkingImageLike::SetRenderSourceRect`（那个只存一个）。
6. `onAnimationTick(delta)` 转调 `InkingSpriteLike::advanceFrame(delta)`（与 `InkingSprite.h:197` 同一行）。
7. `onRender(pixelX, pixelY)`：按 `GetShape()` / 真实图尺寸构造 `InkingImageFitSpec`，对 `_frameRects` 每块调一次 `inking_detail::fillShapeTextured`，`tintColor` 取 `GetAppearance().normal.color` 的 alpha。（不调 `PaintButtonAppearance`——那会把描边/阴影按 K 份画一遍。）
8. `SwitchTo(name, keepPhase)`：`FindIndex` 失败 → 返回 false + 1 条 ERROR；成功 → `setTiming` + `replaceFrameTable` + `ensureTimeBase` 语义（复用 `SetSpriteData` 里那套「换配后不跳帧」的顺序，见 `InkingSprite.cpp:197-209`）。
9. 图集几何校验：构造期用 `inking_detail::ProbeImageSize` 量一次图，与 `SheetWidth()/SheetHeight()` 比对，不符记 ERROR（与 `validateSheetGeometry` 同一条判据，不另写一份）。

**与改动前对比**

| 场景 | 改动前（`InkingSprite` 图集） | 改动后（`InkingSpriteGroup`） |
| - | - | - |
| 一帧一块 | 支持 | 支持（`frames[i]` 里一个格号） |
| 一帧 K 块 | 不支持 | 支持，**一次** `SDL_RenderGeometry` |
| 切动画 | 换 `SpriteData`（可换文件、换尺寸） | 换格号表（**同一张图集**，尺寸恒等） |
| 命中 | 一个矩形 | **不变**（一个矩形；透明区照样算命中） |
| 序列帧 | 支持 | **不支持**（只吃一张图集） |

**验收**

`CheckSpriteGroup()`（新）：
1. 构造一份 2 动画 × 3 帧 × 2 块 的配置，断言 `GetAnimationNames().size() == 2`、`GetCurrentAnimation() == "idle"`、`GetFrameCount() == 3`。
2. `SwitchTo("run")` 后 `GetFrameCount()` 变、`GetCurrentAnimation()` 变、`animationChanged` 发一次（新, 旧）；`SwitchTo("nope")` → false + `InkLog::CountOf(Error)` 恰好 +1。
3. 合批断言：`BatchStatsSnapshot().textureDraws` 在一帧内增量 = **1**（同图集 2 块）；`geometrySubmissions` 增量 = 1。
4. 几何断言：离屏读回，第 0 帧的 2 块各自落在同一矩形内的预期像素上（与 `CheckSpriteSheet` 的读回段同一写法）。

**风险与回退**

- **风险一**：`onRender` 里 K 次 `BuildShapeMesh`（图像面没有顶点缓存槽）。K 小（≤8）时可忽略；K 大时要另开任务做顶点缓存。
- **风险二**：`hooksFor` 是第三份接线（`InkingSprite` / `InkingDynamicSprite` / 本类）。三份接线会分家——缓解：三者的 `hooksFor` 只绑自己的成员，逻辑全在 `InkingSpriteLike`，且 T-03 已把签名统一。
- **回退**：新文件、无既有消费者；出问题就删掉本次新增的 `InkingSpriteGroup`，T-02 的改名可保留（它本身是纯重命名）。

---

## 7. 第五批：0.9.0-e（双档与收口）

### T-06 动态档 `InkingDynamicSpriteGroup`

- **状态**：未开始
- **落点**：新文件 `include/image/InkingDynamicSpriteGroup.h` + `src/image/InkingDynamicSpriteGroup.cpp`

**准备怎么改**

双档的理由与 Button / Image / Sprite 一致：静态档几何定死、进命中表；动态档几何可改、不进表（记「洞」）。形状照 `InkingDynamicSprite`（`InkingDynamicWidget` + `InkingImageLike` + `InkingSpriteLike`）。

| 方案 | 做法 | 代价 | 结论 |
| - | - | - | - |
| A 复制静态档、换基类 | 与 `InkingDynamicSprite` 对 `InkingSprite` 的同一套差 | ~200 行重复 | **推荐**（本轮） |
| B 抽 `InkingSpriteGroupLike` | 两档共用一个骨架 | 第二档还不存在就先抽，抽出什么全凭猜 | 退路（第三档出现时） |
| C 只做静态档 | 动态需求用父节点走位 | 精灵自己走位做不到 | 不做（已定双档都要） |

选 A。**先复制、再抽取**是 `AGENTS.md` 里 G-03 → G-04 走过的路，判据是「两档逐字相同 + 计数相等」，不是「看起来像」。

**大致改动逻辑**

1. 复制 T-05 的类，基类换成 `InkingDynamicWidget`（+ `InkingImageLike` + `InkingSpriteLike`）。
2. 四个几何写入口（`Resize` / `ChangeSelfAnchor` / `ChangeTraceAnchor` / `ChangeOffset`）与 `InkingDynamicSprite` 同形；其中 `Resize` 之后**不需要**重算格矩形（等格，矩形与组件尺寸无关）。
3. `HitTest` 自己判（与 `InkingDynamicSprite` 同一条路），`OwnsSubtreeHit()` 保持 false。
4. 绘制复用 T-05 的 `onRender` 逻辑——**这是唯一的重复点，先接受**。

**与改动前对比**

| 场景 | 静态档 | 动态档 |
| - | - | - |
| 几何写入口 | 无（编译期 `static_assert` 钉死） | 四个都有 |
| 命中 | 进命中表 | **不进表**，记洞；`Resize` 后洞跟着刷 |
| 每帧推进 | `onAnimationTick` | `onAnimationTick`（**同一条路**） |
| 一帧 K 块一次提交 | 1 次 | **不变**（同一份绘制逻辑） |

**验收**

`CheckDynamicSpriteGroup()`：
1. `Resize(120, 90)` 后命中跟着变（`HitTest` 新旧点一真一假）。
2. 两个组（静态 / 动态）同配置同场景，`TickFrame(1.0/12.0)` × 100 之后 `GetFrameIndex()` 恒等、`GetFrameAdvanceCount()` 相等——**这是「两档帧机同源」的判据**（G-04 的判据复用）。
3. `GetOwnedChildCount() == 0`（节点数恒 1）。

**风险与回退**

- **风险**：复制期两端会分家（G-03 的教训）。缓解：第 2 条验收就是分家的探测器；真分家了再抽 Like。
- **回退**：新文件，删掉即回退；静态档不受影响。

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
| T-04 | 新增块叠加组的数据面 | 0.9.0-d | 未开始 |
| T-05 | 静态档 `InkingSpriteGroup` | 0.9.0-d | 未开始 |
| T-06 | 动态档 `InkingDynamicSpriteGroup` | 0.9.0-e | 未开始 |
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
