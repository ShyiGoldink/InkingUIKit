# 任务书 G 系列：几何分档（静态 / 动态两档补全）

> ## 本文对应版本 0.9.x（开发中；代码 `kVersion` 仍是 0.8.5，发布时一并改）
>
> - **系列语义**：`G` = **Geometry**。这一系列只做一件事——把"几何会不会在构造后自己变"这条判据落到 Image / Sprite / Label / Panel 四个组件上。
> - **编号来源**：`docs/zh/TaskGuide.md` §1.1 的系列表已登记 `G` 一行。`G-00`…`G-08` 只增不改；废止的编号保留并注明"未落地"。
> - **上游依据**：`docs/zh/TaskGuide-Dynamic.md` 的附录 A / B / C（`MD-01`~`MD-03` 的执行结果）。本文所有"现状 / 兼容 / 为什么这么做"都能在那三份附录里找到带行号的证据，本文不重复举证。
> - **本系列不改名字**：`InkingStaticImage` / `InkingStaticLabel` / `InkingStaticPanel` / `InkingSprite` 四个旧名一个都不动（附录 C.4）。
> - **⚠ 0.9.0 起 `InkingSpriteGroup` 改名为 `InkingAnimateGroup`**：那个名字让给了"一个 sprite 由多块拼成"的新组件（见 `plans/2026-11-04-sprite-group-and-animate-group.md`）。**本文里出现的 `InkingSpriteGroup` / `CheckSpriteGroup` 都是当时那个类**，按历史记录读，一个字都不改。
> - **优先级**：本系列是**能力补齐**，不是 1.0 前的主线。四个组件都**必须**有动态档——只有 Button 一个有，组件能力是残缺的；但排期上让位于纯静态 UI 的完善，不抢主线资源。批次之间允许拉长，不要求连续做完。

---

## 0 不覆盖什么

**不做**：`Canvas`、`Video`、`Scene`、`Window` —— 它们的几何在设计期就定了，没有运行期改的诉求。

**本轮不动**：`InkingSpriteGroup`（挂在 `InkingSprite` 这个静态档上，零改名方案下它**一行都不用改**）。它要不要双档（子项坐标跟随、组内几何传播、成组动画）需要单独设计，另开任务书。

**不造 `PanelLike`**：`InkingDynamicPanel` 是独立实现，理由见 `TaskGuide-Dynamic.md` 附录 A.2（面板的绘制早已在 `src/panel/PanelPaint.cpp` 共用，能抽的只剩 11 行胶水；两档真正的差异在状态机、滚动、容器命中三组，而现存的动态样本 `InkingFixedPopup` 这三组一组都没有）。

## 1 需求核对（先回答"为什么要"，再谈"怎么做"）

`TaskGuide-Dynamic.md` 附录 A.3 把"要动态档"拆成三种诉求，并指出**用错了两者都难受**：

| 诉求 | 正确解 | 用错的后果 |
|---|---|---|
| 偶发拖拽 / 缩放 | A13 外部显示变换（`SetDisplayTransform`） | —— |
| **每帧走位**的实体 | **动态档** | 用 A13 能"看见它动"，但**占位不变**、且每帧仍是整表重建（见 §2.1；动态档的叶子在 `T-19` 之后不再付这笔） |
| **运行期改尺寸 / 占位** | **动态档** | 静态档那两条写尺寸的通道（`ApplyLayoutRect` / `ApplySelfSize`）业务代码调不到 |

> **一句必须说准的话**：动态档买的是**占位**与**命中语义**，不是性能。
> 写这句话时（2026-11-02 立项）A13 与四个几何入口**同价**——都调 `MarkStructureChanged()`，都是每帧一次整表重建
> （证据见 `TaskGuide-Dynamic.md` 附录 A.3 的更正段）。同日 `T-19` 落地后，几何入口这一半分了出去：
> **叶子移动只刷洞**。但这条要归功于 `T-19`，而且**静态档走 A13 仍然照付全价**——所以
"用动态档就能省重建"依旧是错的推论（它省的是"因为它是叶子"，不是因为它是动态档）。

四个组件的归属与确认：

| 组件 | 诉求 | 谁确认 |
|---|---|---|
| Image | 每帧走位（实体层贴图） | **Shyi，2026-11-02**："四个都补" |
| Sprite | 每帧走位 + 换动作时改尺寸 | 同上 |
| Label | 运行期改尺寸 / 占位（不靠布局容器） | 同上 |
| Panel | 运行期改尺寸 / 占位（吸附、按内容撑高） | 同上 |

> 这一节存在的意义：任何后续任务如果答不出自己属于哪一行，就不该存在。`G-08`（生成器）是唯一的例外——它服务的是上面四行的**外部可达性**，本身不新增能力。

## 2 现状（一张表，来自附录 A.1）

| 组件 | 绘制位置 | 几何依赖 | 输入通道 |
|---|---|---|---|
| `InkingStaticImage` | `onRender` 薄包装 → `detail::PaintButtonAppearance`（与 Button 同一份） | 只读；拖 / 缩走 A13，不动 `_width/_height` | **推** |
| `InkingSprite` | **没有自己的 `onRender`**；换帧只在内容层（`applyFrame`） | 同 Image；`SetSpriteData` 刻意不动尺寸 | 继承 Image 的推 |
| `InkingStaticLabel` | `onRender` 按当前 `_width/_height` 排版 | 只读；写尺寸的两条通道都不归业务代码 | **无**（默认穿透） |
| `InkingStaticPanel` | 两趟：填充/阴影 → 描边（装饰遍） | 只读；滚动每帧改**子树变换** | **推** |

四条对实现有直接约束的结论：

1. **绘制基本不用写**：Image 走 `PaintButtonAppearance`，Panel 走 `PaintPanelAppearance` / `PaintSurfaceShadow` / `PaintSurfaceStroke`。新组件的 `onRender` 都是 3–10 行的接线。
2. **工作量在输入**：动态档收不到 `PushPointer*`，只有 `onPointerStateChanged` 那一次通知（`src/scene/InkingScene.cpp:1321, 1443`，仅 `IsDynamic()` 且指针真变）。每组件 30–56 行自判，形状照 `PointerProbe` 那套。
3. **`detail::PointerProbe` 可以直接用**：已有两个消费者（`InkingDynamicButton`、`InkingFixedPopup`），保持 `detail/` 不升 `util/`（附录 B.3），本系列不再讨论它的归属。
4. **不需要登 CMake**：`CMakeLists.txt:82` 是 `GLOB_RECURSE`，新增 `src/**.cpp` 自动进库。

### 2.1 已知成本：每帧移动 = 每帧一次整表重建（**叶子已由 `T-19` 收拾**，本系列不解决剩余那部分）

链条（全部核对过）：

```
SetDisplayTransform（A13，静态版拖动走这条）
  → MarkStructureChanged()                 src/widget/InkingWidget.cpp（SetDisplayTransform）
  → NotifySceneStructureChanged()          _contentDirty = _orderDirty = true
                                           src/scene/InkingScene.cpp
  → 下一帧 sortDrawList()                   clear + 全量 DFS + stable_sort
  → rebuildHitTable()                      每个动态节点现算包络 → AddHole

ChangeOffset / Resize / 改锚点（动态档的四个几何入口，T-19 起分两路）
  → MarkGeometryMoved()                    子树里有静态节点？
      ├ 有 → 上面那条（照旧全量重建）
      └ 没有（叶子）→ MarkDirty + MarkRepaintDirty + NotifyDynamicMoved
                      → 下一帧只跑 RefreshHoles()（列表 / 排序 / 静态条目一个字节不动）
```

三条结论，写在这里免得每个任务各猜一遍：

1. **A13 与几何入口原本同价，`T-19` 把几何那条分了一半出去**。所以仍然**别**拿
   "动态档省重烘"当"用动态档代替 A13"的理由：动态档买的是**占位**与**命中语义**。
   它确实省掉了"每帧整表重建"，但那要归功于 `T-19`（叶子只刷洞），与"是不是动态档"
   无关——**一个每帧走 A13 的静态图仍然照付 798 µs**。
2. **顶点缓存不复用**：顶点缓存槽（`InkingWidget.h:942-949`）是给"静止帧"用的，每帧移动的节点每次都要重算轮廓。
3. **动态容器的包络更贵**：`GetHitEnvelope()` 对动态容器是整棵子树并集（`src/panel/InkingFixedPopup.cpp:229-232` 的 `PanelHitEnvelope`），而重烘每帧都要问一次。子节点多的动态面板要把这条列入验收的观测项（至少记一个计数，别让它悄悄变 O(N²)）。

**本系列不做优化**：优化属于场景架构（增量重建 / 移动节点专用通道），要单独立项，且必须先有一个"几百个移动实体"的基准。把它塞进 G 系列会让 9 个任务各自背上一个架构问题。**叶子那条切口已立项为 `TaskGuide.md` 的 `T-19`，并于 2026-11-02 落地**（见 §2.1.1）——所以从 G-01 起，**动态档的叶子移动不再付下面那笔账**（静态版走 A13，仍然照付）。
#### 2.1.1 但"重烘整表"不是必须的——有具体切口（**`T-19` 已落地**，不在本系列）

"动态节点不进表"这句话容易让人顺推出"那它动的时候不用重烘"。**半对**，要分成两件事看：

| 东西 | 叶子动态节点移动时要不要重建 | 依据 |
|---|---|---|
| **静态条目** | **不要**。静态条目的包络是"节点自身绝对位置 + 自身包络"，兄弟或无关节点的移动影响不到它 | `InkingScene.cpp:1147-1151`：静态用 `item.absX`，动态用 `GetAbsX()` |
| **绘制列表 / 排序** | **不要**。`IsAbove` 只比树序与兄弟内 z，**不看坐标**；`Resize` / `ChangeOffset` / 改锚点都不动 z | `sortDrawList` 的稳定排序比较器 |
| **动态洞** | **必须**。`QueryHit` 是 `if (!inHole) return staticResult;`——点不在洞里就**根本不问动态层** | `InkingScene.cpp:1234-1236` |
| 静态**子孙**的快照 | 容器移动时**必须**（`item.absX` 是建表时冻住的） | `:689-690`、`:1766-1771` |

所以正确的切法是一条三行规则：

- **叶子**（无静态子孙）移动 → 只刷**洞**，不动列表、不动静态条目；
- **容器**有静态子孙 → 照旧全量重建；
- z / 父子 / 可见性变 → 照旧全量重建。

需要的机制（`T-19` 已按这条落地，2026-11-02）：① `InkingWidget::HasStaticInSubtree()`——**递归早退、不缓存**（缓存要在三条增删路径上维护，漏一处就是"移动中的组件点不到"）；② 场景的"只刷洞"脏通道 `_holesDirty` + `RefreshHoles()`，在 `sortDrawList()` 没有别的脏时顺带刷；③ 四个几何入口统一走 `MarkGeometryMoved()` 分流。

代价与风险：洞的刷新**实测**是"扫一遍列表挑动态节点 + 算包络"（`O(列表长度)`，每项 2 ns），比整表重建便宜**两个数量级**（4000 项：8.4 µs vs 798 µs）；但"叶子/容器"的判据一旦写错，症状是**移动中的组件点不到**（洞过期）或**点到空气**（洞没跟着走）。所以它必须配一条自检：移动一个叶子后，在新位置 `QueryHit` 必须命中它、在旧位置必须不命中。

#### 2.1.2 实测：这是"阶跃"，不是"渐进"（探针 `build/probe_dynamic_cost.cpp`）

探针只量 `RebuildHitTable()` 一帧的账（`sortDrawList` + 重烘），400 帧取均值，不含渲染。每行是独立场景（N 个静态 `InkingStaticWidget` 平铺 + K 个动态节点每帧改 offset）：

| 静态数 | 动态数 | 全静态·没人动 | 每帧都动 | 每静态节点每帧 |
|---|---|---|---|---|
| 200 | 0 | **0.00 µs** | — | — |
| 200 | 1 | 0.00 µs | **45 µs** | 0.22 µs |
| 200 | 10 | 0.00 µs | 39 µs | 0.20 µs |
| 1000 | 0 | **0.00 µs** | — | — |
| 1000 | 1 | 0.00 µs | **178 µs** | 0.18 µs |
| 1000 | 10 | 0.00 µs | 190 µs | 0.19 µs |
| 4000 | 0 | **0.00 µs** | — | — |
| 4000 | 1 | 0.00 µs | **798 µs** | 0.20 µs |
| 4000 | 10 | 0.00 µs | 807 µs | 0.20 µs |

三条读数：

1. **没人动就是真的 0**。`sortDrawList()` 开头 `if (!_contentDirty && !_orderDirty) return;`，全静态且无变化时每帧一个 bool 都不多付。
2. **一个组件动起来，代价是"整个界面的规模"，不是"那个组件"**。200 / 1000 / 4000 个静态节点分别 45 / 178 / 798 µs，**线性于静态总数**。
3. **第 1 个动态节点吃掉全部增量，第 2~10 个几乎免费**（1 个 vs 10 个只差 0~7%）。因为列表与静态条目是给全场景重建的，多几个动态只多几份包络。

换成帧预算：1000 个静态节点 + 1 个每帧移动的组件 ≈ 178 µs ≈ 60fps 预算（16.7 ms）的 **1.1%**；4000 个 ≈ 4.8%。单看不致命，但它**乘在每一帧上**，而且不随"只有一个小东西在动"而减少。

对 PvzAdventure 的含义：实体层若与大量静态 UI 同场，UI 越大、每帧越贵。**"拆场景"不是解法**——一次只有一个 active scene（`SceneLibrary::GetActiveScene()`，`src/window/InkingWindow.cpp:953-957`），拆了也只有一个在跑。真正的解法就是 §2.1.1 那条"叶子只刷洞"。

> **上表是 T-19 之前的基线，别拿它当现状。** 2026-11-02 落地 `T-19` 之后重跑同一个探针，
> "每帧都动"那一列变成 200 / 1000 / 4000 项分别 **0.30 / 1.67 / 8.4 µs**（约 95~150 倍），
> 因为叶子移动只刷洞、不再重建整表。**仍然**有静态版那一档照付全价：走 A13 拖动的
> `InkingStaticImage` 每帧还是 798 µs（那是"外部变换无界 ⇒ 包络算不出来"的必然代价）。
> 读数留下的意义：它解释了**为什么**要做 T-19，以及"一个组件动、整场重建"曾经有多贵。

**给 G 系列的一条纪律**：任何任务都**不要**用"帧时间"做验收判据——它会完全被这条现有成本主导，量出来的是场景规模，不是你改的东西。要量性能就量计数（包络调用次数、重建次数）。


## 3 兼容（零改名，一句话）

四个旧名全部保留、公开面不变，调用方与代码生成器**零改动**。对照表见 `TaskGuide-Dynamic.md` 附录 C.4。本系列新增的只有四个类：`InkingDynamicImage` / `InkingDynamicSprite` / `InkingDynamicLabel` / `InkingDynamicPanel`。

## 4 执行纪律（四条，来自附录 B.4 / C.5 / 静默失效清单）

1. **每个组件相邻两笔**：第 1 笔**复制**（新增动态版，直接照抄静态版的数据与绘制调用，静态版一个字不动）；第 2 笔**抽取**（把共用部分抽成 `XxxLike`，两类都继承，**纯重构、行为零变化**）。提交说明里第 2 笔必须写"纯重构"。
   - 为什么不是先抽：仓库自己的 `InkingButtonLike` 是"先有第二份复制品、再抽出来"的产物（`include/button/InkingButtonLike.h:13-16`）。先抽 = 拿没验证的边界去猜。
2. **`XxxLike` 里不许有纯虚函数**：代码生成器会生成多继承它的类，纯虚会让下游生成的类变成抽象类，报错落在"实例化"那一行，看不出是基类的问题。照 `InkingButtonLike` 的写法：虚函数 + 头内空实现。
3. **静态档不许顺手继承 `InkingDynamicWidget`**。每新增一个动态类，在 `src/inking.cpp` 补一对镜像断言（现有四条在 `:148-158`，照抄写法）。
4. **凡是"失败时不报错、不崩溃、只是不对"的行为，必须三处同改**：`examples/demo/selftest.cpp` 的 `kSilentFailures`、`docs/AGENTS.md` §7 的锚点块、以及代码里的 `NoteSilentFailure("...")`。`ctest silent_failure_list_in_sync` 会比对前两处，漏一处就红。本系列至少有两个候选：`FitToContent()` 在布局容器里不生效（G-05 已改成返回 false + WARN，**别把它悄悄改回"返回 true 但什么都没写"**）、以及 `XxxLike` 里不小心加了纯虚函数（那一个会编译报错，不算静默）。

## 5 任务

### G-00 修两组、共四处错注释

- 状态：**已完成**（2026-11-02）。两组都是"注释说反了、而下游会照它做判断"：
  - **(a) 两档差异只有几何**：`InkingDynamicWidget.h:6`、`InkingDynamicButton.h:12-14` → 改成六项差异（几何 / 命中 / 输入 / 逻辑步 `Tick` / 渲染帧 `onAnimationTick` / 默认 `onRender`），并指回 `ComponentGuide.md` §2。判据：`grep -rn "唯一区别就是下面这四个" include/ src/` 无输出。
  - **(b) 洞漏登"不会点错"**：`HitTable.h:96-108`、`InkingScene.cpp:1155-1163` → 改成"洞**偏大**只是多问一次；洞**漏登或过期** = 那一片永远问不到动态层 = **点不到**"，并指回 `T-19`。（第三处 `TaskGuide.md` 的 `T-4` 偏离已在立项 `T-19` 时改掉。）
  - 证据：四个文件 `git diff` 共 46 行改动，**过滤掉注释行后为空**——一行逻辑都没动；编译通过（19 s）；自检带 PATH 跑 **失败项 0**；`grep -rn "G-00" include/ src/` 命中 4 处（注释末尾标了编号，符合"注释是跨文件索引"那条）。
  - **当时的一条偏离已消除**：首跑 `ctest` 只过 5/7（`selftest` / `selftest_windows` 红）。查实与代码无关——`add_test` 不设 `PATH`，SDL3 的 dll 在工具链 bin，构建目录里只有 `libinking.dll` ⇒ 字体打不开 ⇒ `glyph_atlas_full` 的静默失效自检被 `[skip]` ⇒ 同步检查判红；带 PATH 手动跑是失败项 0。**同日补掉**：新增 `tools/stage_runtime_deps.sh` + `inking` 的 `POST_BUILD` 拷 ldd 闭包（19 个 DLL），现在 `ctest` 裸环境 **7/7**，`build/bin/*.exe` 直接双击也能跑。登记见 `TaskGuide-Dynamic.md` §2.2 第 5 条。

---

### G-01 `InkingDynamicImage`（新增，复制静态版）

- 状态：**已完成**（2026-11-02）。第 1 笔（复制）交付：`include/image/InkingDynamicImage.h` +
  `src/image/InkingDynamicImage.cpp`，静态版**一个字没动**。
  - 证据：`ctest` **7/7**（含 `strict_warnings` `-Werror`）；`selftest` **失败项 0**，新增
    `CheckDynamicImage()` **15 条**断言，逐条对应下面五项验收；`selftest` 里既有断言一条未改；
    `src/inking.cpp` 补 7 条镜像断言（含 `HasSetDisplayTransform<InkingDynamicImage>`——
    钉住"A13 保留"这条关键决定，谁想"避免两套真相"把它关掉，构建当场红）。
  - **落地时改掉的原稿判断**：原文偏离说"拖动期间每帧一次整表重建，与静态版 A13 拖同价"。
    `T-19` 先落地之后这句**不再成立**——动态图片是叶子，拖动只刷洞（实测那一路 8.4 µs vs
    798 µs）；只有静态版靠 A13 拖才付全价。两档的代价差从"零"变成了"两个数量级"。
- **目标**：几何可在运行期改的图片。绘制、图片加载、命中形状与 `InkingStaticImage` **完全一致**；差别只有两条：几何能改、输入靠拉。
- **落点**：新增 `include/image/InkingDynamicImage.h`、`src/image/InkingDynamicImage.cpp`；`src/inking.cpp` 补断言
- **API**：

```cpp
class InkingDynamicImage : public InkingDynamicWidget {
public:
    InkingDynamicImage(InkingWidget* parent, const ImageData& data);

    // 继承来的几何写入口：Resize / ChangeSelfAnchor / ChangeTraceAnchor / ChangeOffset

    void SetDraggable(bool on) noexcept;     // 拖 = ChangeOffset
    void SetResizable(bool on) noexcept;     // 缩 = Resize，绕左上角，夹 [0.2, 5]
    bool IsDragging() const noexcept;

    // 与静态版同名同义的观测口
    bool HitTest(float worldX, float worldY) const override;
    const ShapeSpec& GetShape() const noexcept;
    const ButtonAppearance& GetAppearance() const noexcept;
    std::uint32_t GetDisplayColor() const noexcept;
    TransformSpec GetSubtreeTransform() const noexcept override;
    ShapeBounds GetHitEnvelope() const override;
    bool LoadAsset(const std::string& relative);
    void SetImageBytes(const std::uint8_t*, std::size_t, const std::string& name);
    std::size_t AssetByteCount() const noexcept;
    bool UsingAssetBytes() const noexcept;
};
```

**实现比上面多几个与静态版同名的观测口**（`GetDisplayTransform` / `AcceptsExternalTransform` /
`IsDraggable` / `IsResizable` / `IsScalingDrag` / `GetData` / `ImageName` / `AssetLoadCount`）：
静态版有的公开面它一个不少，G-02 抽 `ImageLike` 时不用回头补。

- **关键决定（写下来免得后来者改回去）**：内置的**拖 / 缩功能**改的是**几何**（`ChangeOffset` / `Resize`），不是 A13；但 `AcceptsExternalTransform()` **保持 true**——外部显示变换仍然可用。
  - 为什么保留：A13 与几何是**正交**的两件事（屏幕抖动 / 整体缩放 vs 占位变化），关掉等于删掉一个已有能力。而且两套变换在绘制与命中处**天然复合**：`GetDisplayTransform()` 就是"外部变换 ∘ 配置那份"，`PaintButtonAppearance` 与 `HitTest` 读的是同一个结果（`InkingStaticImage.h:70-75`）。
  - 为什么"两套真相"不成立：风险不在"两者并存"，而在"**同一个拖动功能**同时写两边"。把拖动钉死在几何上，问题就没了；外部变换交给调用方按需叠加。
  - 代价（照实写）：调用方要清楚 `GetOffset()`（几何）与 `GetDisplayTransform()`（显示）回答的是不同问题——这正是 `ComponentGuide.md` §1 那套"目标值 / 显示值分开命名"的同一根线。
- **验收**（写在 `examples/demo/selftest.cpp`，跟着 `ctest selftest` 跑）：
  1. `IsDynamic()` 为真且**不进命中表**：构造前后比 `GetStructureGeneration()` 与命中表条目数，动态节点不增加条目（占地记成"洞"）。
  2. `Resize(120, 80)` 后 `GetWidth()/GetHeight()` 为 120/80，且同一世界点的 `HitTest` 结果跟着变（几何变了命中就变）。
  3. `SetDraggable(true)` 后模拟"按下 + 拖 10px + 抬起"一次，`GetOffset` 变化量 = 10px，`GetDisplayTransform()` 仍是单位变换。
  4. A13 仍可用且**与几何复合**：`ChangeOffset(30, 0)` 后再 `SetDisplayTransform(平移 5px)`，`GetDisplayTransform()` 非单位、`HitTest` 按"几何 + 变换"的合成结果判；`GetDisplayTransformRejectedCount()`（`InkingWidget.h:613`）保持 0。
  5. `src/inking.cpp` 新增 `static_assert(!HasResize<InkingStaticImage>, ...)` 等**已有**的四条不变，另加 `static_assert(std::is_base_of_v<InkingDynamicWidget, InkingDynamicImage>)`。
- **命中表与登记影响**：**不进表**，自己维护命中状态；占地记成"洞"（遮挡由派发侧处理，与 `InkingFixedPopup` 同档）。`OwnsSubtreeHit()` 保持 false（图片是叶子）。
- **偏离**：内置拖 / 缩不走 A13（见上，但 A13 本身保留）；不做"拖动合并成一帧"这类优化——现有静态版的逐事件写法先照抄。**拖动期间每帧一次整表重建**那句原稿判断已被 `T-19` 推翻：动态图片是叶子，移动只刷洞、不重建整表（§2.1.1）；静态版走 A13 仍照付全价，这是两档在代价上唯一实质的差别。

---

### G-02 抽 `ImageLike`（纯重构）

- 状态：**已完成**（2026-11-02，**纯重构**）。新增 `include/image/ImageLike.h`，承载图片数据、
  资源字节、绘制外观、源矩形与全部共用观测；两个图片类都继承它。两档保留原公开声明为
  ABI 薄转发，重绘标脏仍留在各自类里；拖 / 缩按任务书不抽。
  - 证据：`ctest` **7/7**（debug 与 Release 两套）、`inking_strict` 过；`selftest.cpp` 与基线
    逐字 diff 为空；静态版 public 段签名 diff 为空；`InkingStaticImage` 的 **44 个导出符号**
    `nm` diff 为空。首轮曾因 `ImageLike` 虚析构新增两个 thunk、并删掉私有
    `refreshRenderAppearance` 符号而不满足判据——改成受保护非虚析构 + 保留私有转发后归零。
- **目标**：把 G-01 与 `InkingStaticImage` 里共用的东西收进 `ImageLike`，两个类都继承它。**行为零变化。**
- **落点**：新增 `include/image/ImageLike.h`；改 `include/image/InkingStaticImage.h`、`src/image/InkingStaticImage.cpp`、`InkingDynamicImage.*`
- **API**：`ImageLike` 承载数据与观测（`ImageData _data`、`ButtonAppearance _renderAppearance`、`_assetBytes`、`_renderSourceRect`）+ `SetRenderSourceRect` / `LoadAsset` / `SetImageBytes` / `GetShape` / `GetAppearance` / `GetDisplayColor` / `GetHitEnvelope` / `AssetByteCount` / `UsingAssetBytes` / `refreshRenderAppearance`。**无纯虚函数**（第 4 节第 2 条）。`onRender` 留在两档各自类里（各 3 行，接 `PaintButtonAppearance`）。
- **验收**：**纯重构的三条客观判据，缺一不可**
  1. `ctest` 7 条全绿，且 `selftest.cpp` 里**一条断言都不许改**（改了就不是纯重构）。
  2. 导出符号集不变：`nm -g --defined-only build/bin/inking.dll | grep InkingStaticImage | sort` 前后 `diff` 为空（新增的 `ImageLike` 符号只增不删）。
  3. 静态版公开面不变：`include/image/InkingStaticImage.h` 里 public 段的函数签名列表 `diff` 为空。
- **命中表与登记影响**：无（`ImageLike` 不参与登记；两档的档位不变）。
- **偏离**：不把拖 / 缩逻辑放进 `ImageLike`——动静两档的拖动**语义不同**（静态改 A13、动态改几何），只在数据与观测上共用。这是本系列唯一一个"看起来能抽、实际不能抽"的地方。

---

### G-03 `InkingDynamicSprite`（新增，复制静态版）

- 状态：**已完成**（2026-11-02）。第 1 笔（复制）交付：`include/image/InkingDynamicSprite.h` +
  `src/image/InkingDynamicSprite.cpp`；`InkingSprite` / `InkingStaticImage` **一个字没动**。
  - 证据：`ctest` **7/7**（debug 与 Release 两套，含 `strict_warnings` `-Werror` 与两档 `selftest`）；
    `selftest` **失败项 0**，新增 `CheckDynamicSprite()` **30 条**断言：覆盖下面四项验收，
    另有几条任务书没列但会踩的边界（见后两条），既有断言一条未改；`src/inking.cpp` 补 7 条
    镜像断言（`HasSetDisplayTransform` 那条也在，与 G-01 同一条"外部变换保留"的决定）。
  - **帧机同源的机械证据**（不靠"看着一样"）：两份 `.cpp` 各取 `rebuildFromData` → `fps` 那一段，
    用 `g++ -fpreprocessed -E` 去注释、类名归一、再去掉全部空白后 **`cmp` 逐字节相同（8808 B）**
    ——覆盖 `rebuildFromData` / `validateSheetGeometry` / `loadFrameBytes` / `syncSheetBytes` /
    `SetSpriteData` / 全部观测口 / `Play`~`SeekToFrame` / `onAnimationTick` /
    `fireEventsBetween` / `applyFrame`。剩下的行级差异**只有换行位置**（这个类名长 10 个字符，
    80 列放不下），没有一处 token 差异。范围外唯一的一段是 `applyData`：两档写几何的方式本来
    就不同（静态直写基类成员、动态走写入口），那正是"复制"这一笔的落点。
  - **落地时补的两条判断**（任务书没写、但容易踩）：
    1. **没有内置拖动**。G-03 的 API 里没有 `SetDraggable`，所以没有把 G-01 那套拖 / 缩复制
       过来——精灵的"每帧走位"是实体层调 `ChangeOffset`，不是用户拽着走。头文件里写明了这条，
       免得后来者以为漏了一笔。
    2. **`SetSpriteData` 不动尺寸**被单独钉了一条断言（验收清单里没有）：配置里带
       `width/height`，而动态档多了 `Resize`，很容易被"顺手"实现成"换配置时套用配置尺寸"。
       自检拿 100×100 的配置喂给一个已经 `Resize` 到 60×40 的精灵，断言宽高不变。
  - **另加一条像素对拍**（任务书没列）：静态 / 动态各画一次同一个图集格，逐像素
    **差异 0 / 400（颜色 400 种）**。理由：上面 30 条里 28 条在读状态，而"动态节点真的进了
    绘制列表并画出来"只有画面能证明（动态档最容易的静默失效就是漏登记：状态全对、就是
    不渲染）；"切格生效"同理——`srcRect` 值对不对与绘制有没有用它，是两件事。
- **目标**：可每帧走位的帧动画。帧推进逻辑与 `InkingSprite` **逐字相同**（同一套 `applyFrame` / `ensureTimeBase` / `fireEventsBetween`），差别只有几何与输入。
- **落点**：新增 `include/image/InkingDynamicSprite.h`、`src/image/InkingDynamicSprite.cpp`；`src/inking.cpp` 补断言
- **API**：`InkingDynamicSprite(InkingWidget*, const SpriteData&)`；`Play` / `Pause` / `Stop` / `SeekToFrame` / `SetSpriteData(data, keepPhase)`；三个事件槽 `frameChanged` / `frameEvent` / `finished`；观测口 `IsPlaying` / `GetFrameIndex` / `GetFrameCount` / `GetFrameAdvanceCount` / `GetSkippedFrameCount` / `IsSheetMode` / `IsValid` / `GetConfigError`。绘制复用 `PaintButtonAppearance`（图集切格仍走 `SetRenderSourceRect`）。
- **验收**：
  1. **帧逻辑同源的硬证据**：同一份 `SpriteData` 分别喂静态与动态精灵，跑 100 帧后 `GetFrameIndex()`、`GetFrameAdvanceCount()`、`GetSkippedFrameCount()` 三项**逐一相等**。
  2. 图集模式：`Resize` 之后仍按 `SheetWidth/SheetHeight` 切格（源矩形不随几何变），且 `IsValid()` 仍为 true。
  3. `Resize` 后 `HitTest` 跟着变（几何驱动命中）。
  4. `src/inking.cpp`：`static_assert(std::is_base_of_v<InkingDynamicWidget, InkingDynamicSprite>)` + `static_assert(!HasResize<InkingSprite>)` 仍在（后者证明静态档没被顺手改成动态）。
- **命中表与登记影响**：**不进表**，自己维护；占地记"洞"。
- **偏离**：`SetSpriteData` **仍不动尺寸**（沿 `InkingSprite.h:145-147` 的既有语义："改尺寸属布局层"）。动态档把这件事交给调用方：要跟着动作改尺寸，就自己调 `Resize`。**不做内置拖动**：G-03 的 API 没有 `SetDraggable`，精灵的走位由实体层调几何入口，不复制 G-01 的拖 / 缩（"差别只有几何与输入"里的"输入"在本任务＝**没有新的输入**，不是漏做）。

---

### G-04 抽 `SpriteLike`（纯重构）

- 状态：**已完成**（2026-11-02，**纯重构**）。新增 `include/image/SpriteLike.h`；实现落在
  `src/image/InkingSprite.cpp`（**不新增 TU**，同 G-02）。两档都继承它，帧机只剩一份。
  - 三条客观判据全过：
    1. `ctest` **7/7**（debug 与 Release 两套，含 `strict_warnings` `-Werror`），`inking_strict` 过；
       `examples/demo/selftest.cpp` **逐字节未改**（`md5sum -c` 为 OK，321 行 G-03 的断言一字不动）。
    2. 导出符号：`nm -g --defined-only build/bin/libinking.dll` 里 `InkingSprite*` 的 **56 个符号名
       一个不少**，只新增 1 个（`InkingSprite::hooksFor`）与 32 个 `SpriteLike::*`。
       **地址列必然变**（重排后布局不同），所以判据按**符号名**比——任务书那句"diff 为空"要这样读，
       照字面比地址只会得到 56 行全变。
    3. 静态版公开面：`include/image/InkingSprite.h` 的 public 段函数签名 **18 行 diff 为空**。
  - 第四条门槛（`InkingSpriteGroup` 的帧相关自检一条不改就过）：`CheckSpriteGroup` 在 `ctest` 里
    原样通过；`InkingSpriteGroup` 只用到 `SetSpriteData` / `ApplySelfSize`（`InkingWidget` 的
    friend），没有一处碰私有帧机。
  - **"搬家没改语义"的机械证据**：帧机那 349 行逐行对比，只有 **10 处机械替换**——3 个绘制钩子的
    4 处调用（`setBytes` ×2、`loadAsset`、`setSourceRect`）、3 个事件钩子（`frameChanged` /
    `frameEvent` / `finished`）、2 个访问器（`GetAppearance` → `_image->`、`GetScene` → `_host->`）、
    1 个改名（`onAnimationTick` → `advanceFrame`）。把这 10 处替换掉、去掉空白后
    **8865 B `cmp` 逐字节相同**。
  - **关键决定：`SpriteLike` 里一个虚函数都没有**（连析构都是受保护非虚）。理由是 G-02 踩过的坑：
    给**次基类**一张 vtable，`InkingSprite` 的析构就会多出 thunk，`nm` 判据当场红。于是
    "要组件替它做的事"不用虚函数，用**构造期注入的 `Hooks`**（6 个 `std::function`）：
    它们是 `SetImageBytes` / `LoadAsset` / `SetRenderSourceRect` 与三个 `Emit` 的绑定，
    在**调用时机**上与原来写在 `applyFrame` / `onAnimationTick` 里的那几行完全等价。
    `src/inking.cpp` 加了两条编译期断言把这条钉住：`!is_abstract_v<SpriteLike>`（§4 第 2 条的
    "不许有纯虚"）与 `!has_virtual_destructor_v<SpriteLike>`（不许有 vtable）。
  - **代价照实写**：每个精灵多 6 个 `std::function`（约 190 B）；每次换帧多 1~2 次间接调用
    （12fps 下每帧 12 次，可忽略）。换来的是"帧机只有一份实现"，G-03 那条"改一处必须改两处"
    的纪律**从今天起作废**。
  - **两档保留转发的差别**：静态档 9 个私有函数在头里留了"只为符号存在"的一行转发（判据 2 要求）；
    动态档没有历史 ABI，私有段只留 `applyData` / `refreshRenderAppearance` / `hooksFor`，
    **公开面 17 个函数保留一行转发**（`selftest` 因此一个字都不用改）。
- **目标**：把 G-03 与 `InkingSprite` 共用的帧机（`_sprite` / `_frames` / `_frameBytes` / 时间基准 / 计数 / 事件派发）收进 `SpriteLike`。**行为零变化。**
- **落点**：新增 `include/image/SpriteLike.h`；改 `include/image/InkingSprite.h`、`src/image/InkingSprite.cpp`、`InkingDynamicSprite.*`
- **API**：`SpriteLike` 承载整套帧推进与观测口，**无纯虚函数**；`onAnimationTick` 的实现在 Like 里（两档都走 `onAnimationTick`，见六项差异第 5 项）。
- **验收**：同 G-02 的三条（`ctest` 全绿 + `selftest` 断言不改 + `nm -g` diff 为空），另加一条：`InkingSpriteGroup` 的帧相关自检（组内同步、换动作改尺寸）一条不改就过——它是 `InkingSprite` 的派生类，是这次重构最容易踩到的下游。
- **命中表与登记影响**：无。
- **偏离**：`InkingSpriteGroup` 不动（零改名方案下它挂的就是静态档）。

---

### G-05 `InkingDynamicLabel`（新增，复制静态版）

- 状态：**已完成**（2026-11-02）。第 1 笔（复制）交付：`include/text/InkingDynamicLabel.h` +
  `src/text/InkingDynamicLabel.cpp`；`InkingStaticLabel.h/.cpp` 与 `LabelData.h` **一个字没动**
  （`git diff --stat` 对这三个路径为空）。
  - 证据：`ctest` **7/7**（`build` 与 `build-release` 两套，含 `strict_warnings` `-Werror`）；
    `selftest` **失败项 0**，新增 `CheckDynamicLabel()` **27 条**断言，逐条对应下面六项验收；
    `src/inking.cpp` 补 6 条镜像断言（含 `HasSetDisplayTransform`，与 G-01 / G-03 同一条
    "A13 保留"的决定）。
  - **复制同源的机械证据**（不靠"看着一样"）：两份 `.cpp` 各取 `EffectiveTextColor` → `GetHitEnvelope`
    那一段（7 个函数，含 `LayoutForWidth` / `CacheMatches` / `MeasureLayout` / `onRender`），
    用 `g++ -fpreprocessed -E` 去注释、类名归一、`tr -d '[:space:]'` 后 **`cmp` 逐字节相同
    （3793 B）**；`MeasureWithTextLayer` 那 10 行也相同（329 B）。剩下的差异只有本类自己
    多出来的构造 / 写入口 / `FitToContent`。
  - **落地时改掉的原稿判断**：原稿说 `FitToContent()` = "按 `MeasureLayout` 的结果写一次几何"。
    实际要**量到自洽才写**：排版交出的 `width` 是**内容宽**（`InkingTextLayout.cpp:443-446`），
    写回去就变成新的换行基准 ⇒ 于是高度可能是按旧宽排的。现在的实现量到"在这个整数宽下再量
    还是这个宽"（实测 1~2 趟，上限 4 趟防病态输入），自检第 3 条正是拿它对的账。
    **代价照实写**：`width` 也会被收到内容宽，配置里的宽只是第一趟的换行基准——头文件与
    `docs/zh/api/` 都写了"要只撑高就自己 `Resize(width, h)`"。
  - **没有像素对拍**（G-03 有）：文字那一笔在机械证据那 3793 B 里、与静态版逐字节同源，
    所以改用 `GetDrawItemCount()` +1 钉住"漏登记"这个静默失效形态（状态全对、就是不画）。
  - **静默失效清单没新增条目**（§4 第 4 条那两个候选的处置）：`FitToContent()` 在布局容器里
    现在是"返回 false + 一条 WARN + `IsManagedByLayout()` 可查"⇒ **不静默**，不入表
    （入表的是"写不出触发条件 / 出错了不发声"的那一类）；"`SetText` 不撑高 ⇒ 多出的行被裁"
    属 `text_clip_by_height`（0.8.3）的既定语义，也不新增。
  - **没有配置驱动的构造函数**（静态版有一个）：与 G-01 / G-03 一致，生成器那条路等 G-08
    打通 `dynamic` 之后再定（生成的类自己取 `LabelData` 喂进来）。
- **目标**：文字与几何都能在运行期改的标签。排版、主题取色、`MeasureLayout` 与静态版一致。
- **落点**：新增 `include/text/InkingDynamicLabel.h`、`src/text/InkingDynamicLabel.cpp`；`src/inking.cpp` 补断言
- **API**：`InkingDynamicLabel(InkingWidget*, const LabelData&)`；`SetText` / `SetTextColor` / `GetTextLayout` / `MeasureLayout`；**新增 `bool FitToContent()`**——按 `MeasureLayout` 的结果写一次几何（`Resize` 到测量宽高）。
- **关键决定一**：`SetText` **不自动改几何**。静态版靠 `RelayoutParent()` + 自测量隐式撑高（`InkingWidget.h:881`），动态版把这件事故意做成显式：高度什么时候变，由调用方喊 `FitToContent()`。理由与 A6 同一条纪律——"框架不做隐式重排，什么时候重排始终可见"。
- **关键决定二（初稿漏了，会静默失效）**：**在布局容器里 `FitToContent()` 不生效**。布局通道 `ApplyLayoutRect`（`InkingWidget.h:860`，friend 是 `detail::layout::ArrangeSubtree`）会在下一次 `Relayout()` 时把 `_width/_height` **整体重写**，所以自己量的尺寸会被父容器的排布覆盖。处理方式照 A6 的既有纪律：
  - `FitToContent()` 里先判"我是否被布局管着"（父容器有有效 `LayoutSpec` 且自己 `layoutItem` 参与布局）；是则 **返回 false 并记一次 WARN**，不写几何——宁可喊一声，也不要写一个下一帧就消失的尺寸。
  - 这条和 `RelayoutSelfIfContentSized()` 的"布局容器内一律不自测量"（`InkingWidget.h:878-879`）是同一条规矩的两个落点，别修一处忘一处。
- **验收**：
  1. `SetText("更长的文字")` 之后 `GetHeight()` **不变**（证明没有隐式撑高）。
  2. 随后调 `FitToContent()`，`GetHeight()` == `MeasureLayout(无约束).height` 取整（±1）。
  3. `Resize(200, 40)` 后 `GetTextLayout()` 按 200 重排（`availableWidth` 进缓存键）。
  4. 标签仍**默认不参与命中**（`HitTest` 恒 false，除非调用方开），与静态版一致。
  5. `src/inking.cpp`：`static_assert(std::is_base_of_v<InkingDynamicWidget, InkingDynamicLabel>)` + `static_assert(!HasResize<InkingStaticLabel>)` 仍在。
  6. **布局容器内不生效**：把一个动态标签放进带 `LayoutSpec` 的面板，`FitToContent()` 返回 false、`GetHeight()` 不变、日志里有一条 WARN（不静默）。
- **命中表与登记影响**：**不进表**；标签本来就不吃点击，动态版的"占地"只是绘制区域，不改变遮挡策略。
- **偏离**：不做自动撑高（见上）；布局容器里不写几何（见决定二）；不做滚动文字（`TextScrollX` 那条是 `InkingTextInput` 的，不在本系列）。

---

### G-06 抽 `LabelLike`（纯重构）

- 状态：**已完成**（2026-11-02）。落点：新增 `include/text/LabelLike.h` + `src/text/LabelLike.cpp`；
  改 `InkingStaticLabel.h/.cpp`、`InkingDynamicLabel.h/.cpp`，`src/inking.cpp` 补 4 条静态断言。
  **`InkingTextInput.h/.cpp` 一个字没动**（`git status` 里两个路径都不出现）——它是
  `InkingStaticLabel` 的派生类、自己重写了 `onRender` 与 `TextScrollX()`，重编即过。
  - 证据一（行为零变化）：`ctest` **7/7**（`build` 与 `build-release` 两套，含 `strict_warnings`
    `-Werror`）；`selftest` **失败项 0**，**一条断言都没改**（G-05 的 `CheckDynamicLabel` 27 条、
    输入框那几十条、`font_fallback_chain`、`label_self_size` 全原样通过）。
  - 证据二（机械判据，同 G-04）：从重构**前**的 `InkingStaticLabel.cpp`（`git show HEAD:`）切出
    `EffectiveTextColor` → `onRender` 那一整段，与 `LabelLike.cpp` 的对应段做
    `g++ -fpreprocessed -E` + 去空白后 `cmp`：**3526 B 逐字节相同**。归一只做了 8 处机械替换，
    逐条可复算：类名（`InkingStaticLabel` / `LabelLike` → `CLS`）、`MeasureLayout` ↔ `MeasureLabel`、
    `onRender` ↔ `paintLabelText`、`TextScrollX()` ↔ `scrollX`、`_host->GetWidth()` ↔ `_width`、
    `_host->GetHeight()` ↔ `_height`、`_host->GetMagnification()` ↔ `GetMagnification()`、
    `_host->GetExternalTransform()` ↔ `GetExternalTransform()`；另加一处"签名多出的 `float` 参数"
    归一（`paintLabelText` 比 `onRender` 多一个 `scrollX`）。**语义改动为零**。
  - 证据三（ABI）：重构前后 `nm -g --defined-only libinking.dll` 对三个标签类取符号名集合，
    **只少两条**：`InkingStaticLabel::LayoutCache::~LayoutCache` 与 `InkingDynamicLabel::…`——
    它们随嵌套类型搬进了 `LabelLike`，同名符号以 `LabelLike::LayoutCache::~LayoutCache` 仍在
    （一条不少地"改名"，不是消失）。**多出来的符号为 0**：两档留的那三个私有转发与原来
    **同签名同名字**，于是同一批符号原样复用。
  - **对任务书 API 列表的一处扩充**：`SetText` / `SetTextColor` 的**数据手术**
    （`assignText` / `assignTextColor`：改字段 + 让排版缓存失效 / 刷新主题色缓存）也收进了
    `LabelLike`。理由：那几行两档逐字相同，复制第二份就是 G-04 之前那种"改一处必须改两处"；
    而"之后做什么"（静态档 `RelayoutParent()`、动态档只标重绘脏）**仍留在各自的薄转发里**，
    所以 G-05 的关键决定一没有被抹掉。
  - **`TextScrollX()` 在动态档保留**（任务书列表里没有它）：G-05 为它导出过符号，nm 判据要求"一条都不少"；
    而且 G-06 之后它正好是 `paintLabelText(..., scrollX)` 那个参数的来源。
- **目标**：把 G-05 与 `InkingStaticLabel` 共用的排版缓存与取色收进 `LabelLike`。**行为零变化。**
- **落点**：新增 `include/text/LabelLike.h`；改 `include/text/InkingStaticLabel.h`、`src/text/InkingStaticLabel.cpp`、`InkingDynamicLabel.*`
- **API**：`LabelLike` 承载 `LabelData _data`、`LayoutCache _cache`、`LayoutForWidth` / `CacheMatches` / `EffectiveTextColor` / `MeasureLayout` / `onRender`（文字那一笔），**无纯虚函数**。
- **验收**：同 G-02 三条；另加：`InkingTextInput` 的自检一条不改就过（它是 `InkingStaticLabel` 的派生类，且自己重写了 `onRender` 与输入）。
- **命中表与登记影响**：无。
- **偏离**：`InkingTextInput` 不动。

---

### G-07 `InkingDynamicPanel`（新增，独立实现，不抽 `Like`）

- 状态：**已完成**（2026-11-02）。落点：新增 `include/panel/InkingDynamicPanel.h` +
  `src/panel/InkingDynamicPanel.cpp`；`src/inking.cpp` 补 **7 条**断言（3 条档位 +
  4 条写入口，含 `!std::is_base_of_v<InkingStaticPanel, InkingDynamicPanel>`——两档是兄弟）。
  - 证据：`ctest` **7/7**（`build` 与 `build-release` 两套，含 `strict_warnings` `-Werror`，
    新 TU 的 strict obj 已按 `-Werror` 编过）；`selftest` **失败项 0**，新增
    `CheckDynamicPanel()` **25 条**断言，六项验收逐条对上（拖动 → `GetScrollY()`=62.5 与
    条几何换算一致；松开后再动指针一个字节不变；同场景静态滚动容器保持 0；`Resize` 后裁剪区
    184→284；`GetSubtreeTransform().translateY == -60 == -GetScrollY()`；一帧内包络调用恰好 1 次）。
- **落地时对原稿的三处修正**（都写进了代码注释）：
  1. **`SetScroll()` 只标重绘脏，不标几何移动**。动态档要交"洞"，而本类的
     `GetHitEnvelope()` 已经按滚动范围把洞扩过（内容能滚到本体之外，洞必须盖住所有滚动位置），
     所以**洞与滚动量无关**——每滚一像素白付一次"整棵子树并集"没有任何收益。反过来说：
     谁把那段扩范围的代码删了，症状是"滚过去点不动"，且不报错。
  2. **`PressScrollAt()` / `DragScrollTo()` 收世界坐标并走三步换算**（撤祖先仿射 → 减绝对
     位置 → 过自身显示变换的逆），不复用静态版那两行减法——那两行只在"父级在原点且无变换"
     时等价，而本档两样都可能不成立（它能被 `ChangeOffset` / `Resize` 搬走）。
  3. **`HitTestNode()` 不重写**：基类那份已经是"子树优先"的容器语义（静态版抄的那一份是
     0.4.0 之前基类还没有它时留下的）。少一份重写就少一处会走形的地方。
- **照抄的部分**（本系列"先复制、再抽取"的纪律）：`onRender` / `onRenderDecoration` 是三笔接线
  （`PanelPaint.cpp` 实现，与静态版共用）；`UpdateScrollRange` / `GetBarMetrics` /
  `ContentViewport*` / `paintScrollBars` 与静态版逐字同源（`paintScrollBars` 是那 45 行的第二份
  ——它纯绘制、无时序依赖，**第三个能滚的容器出现时再抽进 `PanelPaint.h`**）。
- **自检里踩到的一条**（值得记住）：算世界坐标时要**自己减滚动量**——`GetAbsX/Y` 是**布局**
  位置，滚动位移在子树变换里。第一版写成"面板绝对位置 + 250"，落到子节点之外，
  于是"被剪掉"与"本来就不占那儿"分不清（那条断言红了，另一条却是绿的）。
- **不做滚轮**（按原稿）：也没重写 `HandlesSubtreeWheel()`——它是推通道的机制，对动态档无效。
- **不变**：`InkingStaticPanel` / `InkingFixedPopup` / `MenuPopup` 一行没改。
- **目标**：几何可变、可滚动的容器。填 / 影 / 描边三笔照抄静态版那 11 行接线；滚动、裁剪、容器命中三组**自己实现**。
- **落点**：新增 `include/panel/InkingDynamicPanel.h`、`src/panel/InkingDynamicPanel.cpp`；`src/inking.cpp` 补断言
- **API**：`InkingDynamicPanel(InkingWidget*, const PanelData&)`；`PanelLook _look`（与静态版同一份两态外观 / 过渡）；观测口照静态版：`GetShape` / `GetAppearance` / `GetDisplayColor` / `ContentViewportWidth/Height` / `GetScrollX/Y`；容器契约：`OwnsSubtreeHit() = true`、`GetSubtreeTransform()` 含滚动位移、`GetSubtreeClipBounds()` 扣掉滚动条、`onRender` + `onRenderDecoration`（三笔走 `detail::PaintPanelAppearance` / `PaintSurfaceShadow` / `PaintSurfaceStroke`）。
- **关键决定（两条，都是踩过的坑）**：
  1. **滚动条拖动从 `onPointerStateChanged` 自判**：`PointerProbe` 拿 `pressed` / `released` / `hover`，等价静态版 `onPointerPress` + `onPointerDrag`。动态档收不到推，这是唯一入口。
  2. **`HandlesSubtreeWheel` / `FindSubtreeWheelOwner` 对动态面板无效**：它们是**推通道**的机制（沿父链找声明者再 `PushPointerWheel`），而 `dispatchTarget` 对动态节点直接返回 nullptr（`src/scene/InkingScene.cpp:1341`）。本任务**不实现滚轮**，理由见下面的"为什么不做滚轮"。
- **为什么不做滚轮（本任务最重要的取舍，别顺手加回来）**：滚轮在动态档**没有可靠的读取时机**。
  - 事实一：滚轮按**渲染帧**累加、按**渲染帧**清零（`SetPointerWheel` 只累加 `src/scene/InkingScene.cpp:943-948`；清账在帧末 `ConsumeFrameDirty` → `src/window/InkingWindow.cpp:1037-1039`）。
  - 事实二：动态组件只能在 `TickLogic` 里读（`PushPointerStateChanged` 不被滚轮触发——`SetPointerWheel` 不置 `_pointerDirty`）。而 `TickLogic` 由固定步长回调驱动（`src/window/InkingWindow.cpp:953-957`），一个渲染帧里可能跑 **0 次或 2 次以上**。
  - 后果：0 次 ⇒ 这一帧滚轮被清掉、**丢了**；2 次 ⇒ 同一份增量读两遍、**滚双倍**。60fps 渲染 + 50Hz 逻辑下，两种都会真实发生。
  - 结论：这不是组件能补的，是**框架**的滚轮通道少了一条"给动态档的、每帧一次且必达"的通知。要么给场景加一个"滚轮帧"通知，要么把清零时机挪到 `TickLogic` 末尾——两条都改框架语义，**必须单独立项**。本系列只做滚动条拖动（够 PvzAdventure 的"改占位"诉求），滚轮留给那项任务。
- **验收**：
  1. **滚动条能拖**：模拟"按在滑块上 + 拖 40px + 抬起"，`GetScrollY()` 变化 = 40（按 `PanelData::scroll` 的换算），且松手后不再跟随（捕获释放）。
  2. **不与静态滚动容器串台**：同一场景放静态滚动面板 + 动态面板，指针从动态面板拖滚动条时静态面板 `GetScrollY()` 不变。
  3. `Resize(300, 200)` 后 `GetSubtreeClipBounds()` 跟着变；放一个超出本体的子节点，其超出部分 `HitTest` 落空（裁剪与命中同源）。
  4. 滚动到中段后 `GetSubtreeTransform()` 的平移量 == `-GetScrollY()`。
  5. **包络成本有观测口**：`GetHitEnvelope()` 被调用次数记一个计数（§2.1 第 3 条），自检里断言"一帧内不超过 1 次/节点"，防止将来有人把它做成每帧多次。
  6. `src/inking.cpp`：`static_assert(std::is_base_of_v<InkingDynamicWidget, InkingDynamicPanel>)` + 静态版反向断言仍在。
- **命中表与登记影响**：**不进表**；`OwnsSubtreeHit() = true` ⇒ 子孙也不进表，由 `HitTest` 递归解答（与静态面板同一条语义）。占地记"洞"。
- **偏离**：不抽 `PanelLike`（附录 A.2）；不复用 `InkingStaticPanel` 的滚动代码（它在推通道上，时序不同，抄过来会带进一套用不上的状态）；**不做滚轮**（理由见上，属框架级待办）；`InkingFixedPopup` / `MenuPopup` 不动。

---

### G-08 代码生成器支持四个类型的 `dynamic`

- 状态：**已完成**（2026-11-02）。五处落点全改：`Fields.h` 三张白名单各加 `dynamic` + Image
  描述串去掉"没有 dynamic"；`Cxxcss.cpp` 的 `kRejectedImageFields` 删掉 `dynamic`、三个 reader
  各加一段 `key == "dynamic"`（只认 bool）；`Cxxcss.h` 三个 Config 各加 `bool dynamic`；
  `Emit.cpp` 的 include 与基类选择；`CXXCSS.md` 正文 + 支持矩阵 + §9 锚点块重生成。
  - 证据一（旧产物不变）：把现有 18 份配置（去掉四个新样例）喂给新生成器，与改动前的产物
    逐字对比——**去掉注释行后 `diff` 为空**（`.h` 与 `_register.cpp` 两份各自为空）；换
    「按类比」更硬：旧产物里的 **14 个类逐个逐字节相同**，新增的正好是 4 个动态类。
    剩下的行差异只有两类，都不是行为：新增的 `#include` 与段落注释，和"来源路径"那一行
    （临时目录生成，路径前缀不同）。
  - 证据二（生成物真的换了基类）：`inkgen_compile_check` 里四条
    `static_assert(is_base_of_v<InkingDynamicImage, DynamicWalker>)` / `…Sprite, DynamicRunner` /
    `…Label, DynamicTitle` / `…Panel, DynamicDrawer`，外加**四条反向**断言
    （没写 `dynamic` 的 `CardArt` / `Walker` / `MenuTitle` / `DrawerPanel` **不得**是动态档）
    ——反向那四条是这次加的：光验正向，把默认值改成 true 也照样绿。
  - 证据三（判据齐）：`ctest` **7/7**（`build` 与 `build-release` 两套，含 `strict_warnings`
    `-Werror`）；`inkgen_validate_cxxcss` / `inkgen_compile_check` / `field_table_in_sync` 三条
    全绿；`grep "只有静态档"` 与 `grep "没有 dynamic"` 对 `CXXCSS.md` 均无输出。
  - 证据四（错法不静默）：`"dynamic": "yes"` 报
    `dynamic 必须是 true 或 false（它决定生成的类继承哪一档：静态 / 动态 × 图片 / 帧动画）`，
    带文件与行号。
  - **落地时对原稿的两处增补**（都写进了代码/文档注释）：
    1. **样例补到四个**（原稿只要求 Image + Sprite）：`Label/dynamicTitle.json` 与
       `Panel/dynamicDrawer.json` 也各加一份。理由：`Emit` 对四种组件是**四段独立代码**，
       只验图片那两条，标签 / 面板的基类选择写错照样绿——而它们的验收证据只有"能生成、
       能编过"这一条。四个样例让 `inkgen_compile_check` 一次覆盖全部四条分支。
    2. **`main.cpp` 的配置摘要改成报真实档位**：原来硬写"静态面板"/"静态标签"，加了
       `dynamic` 之后它会**说谎**（画面上是动态档、日志说静态，人就去查"为什么几何改不动"）。
       现在按配置报 `静态` / `动态`；Image 那个计数不动（它报的是"帧动画几张"，与档位正交）。
  - **踩到的一个 CMake 坑**：`inking_strict` 的门是 `file(GLOB tools/inkgen/*.cpp)`，而
    `compile_check.cpp` 现在 include **构建期生成**的头（`inkgen/all/button_service.h`）——
    干净树里那个头还不存在，门会以 `No such file or directory` 红。已在 `CMakeLists.txt`
    里用 `list(REMOVE_ITEM …)` 显式排除并写明理由：这道门是**纯源码**的门，那个 TU 由
    `inkgen_compile_check` 每次构建真编一遍。别把这条当成"顺手绕过 `-Werror`"——它只有
    几十行断言、没有逻辑。
  - **偏离**：样例文件名用仓库既有的 camelCase（`dynamicWalker` 等）而不是原稿的
    `dynamic_walker`——`MakeNameConstant` 只把首字母大写，下划线名会生成 `Dynamic_walker`
    这种类名；`Canvas` / `Video` 不加 `dynamic`（§0）；`tools/package.sh` 未动。
- **目标**：`"dynamic": true` 在 Image / Sprite / Label / Panel 的 json 里生效，生成的类继承对应的 `InkingDynamicXxx`。
- **落点**（五处，初稿只写了两处——这是本任务最容易漏的地方）：

  | # | 位置 | 要动什么 |
  |---|---|---|
  | 1 | `tools/inkgen/Fields.h` | `kKnownPanelFields`（`:40-46`）、`kKnownLabelFields`（`:49-54`）、`kKnownImageFields`（`:62-69`）**各加 `"dynamic"`**；`:232-233` 那条 Image 描述串里"没有 dynamic"要删（它会被 dump 进文档字段表） |
  | 2 | `tools/inkgen/Cxxcss.cpp` | `kRejectedImageFields`（`:3365-3367`）**删掉 `"dynamic"`**；给 Image / Label / Panel 三个 reader 各加一段 `key == "dynamic"` 解析（照 `:2439-2450` 的 Button 那段，包括"只认 bool、写字符串要报错"） |
  | 3 | `tools/inkgen/Cxxcss.h` | 三个 Config 结构各加 `bool dynamic = false;`（照 `:103` 的 ButtonConfig 注释写法） |
  | 4 | `tools/inkgen/Emit.cpp` | **include 也要跟着换**：`:1050`（panel）、`:1054`（label）、`:1062` / `:1070`（image / sprite）现在写死静态头；基类字符串在 `:1260,1267`（panel）、`:1296,1303`（label）、`:1321-1322,1331-1332`（image / sprite）。**只改基类不改 include，生成物编不过**——好在 `inkgen_compile_check` 会抓 |
  | 5 | `CXXCSS/CXXCSS.md` | 正文三处（`:598` 图片报错列表、`:1119` Panel 不支持 dynamic、`:1450` 字段表行）+ 支持矩阵两行（`:1318`、`:1324`）+ **§9 字段表锚点块重生成**（见验收第 3 条） |

- **API**：json 字段 `dynamic`（可选布尔），语义与 Button 的 `dynamic` 一致（`CXXCSS.md:881`）。
- **验收**：
  1. `ctest inkgen_validate_cxxcss` + `inkgen_compile_check` 全绿（后者会把生成出来的 `.h/.cpp` 真编一遍，是"include 换没换对"的唯一客观判据）。
  2. **旧配置产物逐字不变**（向后兼容的硬判据）：动手前先把 `build/inkgen/all/` 整个目录拷一份，改完重新生成后 `diff -r` 为空——除了新增的那个样例。任何一行差异都说明"没写 `dynamic` 的配置被改了行为"。
  3. `ctest field_table_in_sync` 全绿。**它的修法只有一条**：跑 `inkgen --dump-fields`，把输出贴回 `CXXCSS.md` 的 `<!-- BEGIN FIELD TABLE -->` 与 `<!-- END FIELD TABLE -->` 之间（`cmake/check-field-table.cmake:1-12` 逐字比，不走 Markdown 解析）。**不要手改那两张表**。
  4. 新增样例 `CXXCSS/Image/dynamic_walker.json`（`"dynamic": true`），生成类里加一条 `static_assert(std::is_base_of_v<inking::InkingDynamicImage, cls>)`；再给一个 sprite 样例（`frames` + `dynamic`）断言 `is_base_of_v<InkingDynamicSprite, cls>`——**这是唯一能证明"图集路径也认 dynamic"的检查**，因为图片与 sprite 共用同一段 emit 代码、只差一个 `isSprite` 分支。
  5. `grep -n "只有静态档" CXXCSS/CXXCSS.md` 与 `grep -n "没有 dynamic" CXXCSS/CXXCSS.md` 均无残留（后者会命中字段表描述串，重生成后自然消失）。
- **命中表与登记影响**：无（只影响生成的类继承谁；继承谁决定档位，而档位语义由 G-01 / G-03 / G-05 / G-07 各自保证）。
- **偏离**：不给 `Canvas` / `Video` 加 `dynamic`（本文 §0）；不改 json 既有字段语义；不顺手改 `tools/package.sh`；不给 Panel 的 `dynamic` 顺手加滚轮（见 G-07 的取舍）。

---

## 6 批次门槛与进度

### 6.1 门槛

| 批次 | 发布前必须满足 |
|---|---|
| 第 1 批（G-00 + Image 两笔） | `ctest` 7 条全绿；`nm -g` 符号 diff 为空；`selftest` 断言未改；动态图片的拖动改的是几何而非 A13 |
| 第 2 批（Sprite 两笔） | 同上；另加"静态 / 动态精灵 100 帧后三项计数相等" |
| 第 3 批（Label 两笔） | 同上；另加"`SetText` 不改高度、`FitToContent` 才改" |
| 第 4 批（Panel） | 同上；另加"滚动条拖得动且不与静态滚动容器串台"、"包络调用次数有观测且一帧 ≤ 1 次/节点"两条 |
| 第 5 批（G-08 生成器） | `inkgen_validate_cxxcss` / `inkgen_compile_check` / `field_table_in_sync` 全绿；`CXXCSS.md` 已同步 |
| 全量发布 | 五个批次的门槛全过；`inking_strict` 过（`-Werror`）；`docs/zh/api/` 新增四个条目；`CHANGELOG.md` 一条；版本号从 `kVersion` 起改（**注意 `CMakeLists.txt:4` 的 `project(... VERSION)` 也要跟**，否则 `inkingConfigVersion.cmake` 会报旧版本） |

### 6.2 进度表

| 编号 | 任务 | 状态 |
| - | - | - |
| G-00 | 修两组、共四处错注释 | **已完成**（2026-11-02） |
| G-01 | `InkingDynamicImage`（复制） | **已完成**（2026-11-02） |
| G-02 | 抽 `ImageLike`（纯重构） | **已完成**（2026-11-02） |
| G-03 | `InkingDynamicSprite`（复制） | **已完成**（2026-11-02） |
| G-04 | 抽 `SpriteLike`（纯重构） | **已完成**（2026-11-02） |
| G-05 | `InkingDynamicLabel`（复制） | **已完成**（2026-11-02） |
| G-06 | 抽 `LabelLike`（纯重构） | **已完成**（2026-11-02） |
| G-07 | `InkingDynamicPanel`（独立实现） | **已完成**（2026-11-02） |
| G-08 | 生成器支持 `dynamic` | **已完成**（2026-11-02） |

完成任务后，本节状态改 `已完成`，正文对应段落按压缩规则收成一行——两处必须同时改。

## 7 明确不做

| 不做的事 | 原因 |
|---|---|
| 给四个类改名（含 `InkingSprite` → `InkingStaticSprite`） | 附录 C.3：176 处引用 + 下游已生成的代码，收益只是命名整齐；真要改，留 0.9 的破坏性窗口单独做 |
| 抽 `PanelLike` | 附录 A.2：绘制已共用，能抽的只剩 11 行胶水 |
| 把 `detail::PointerProbe` 升进 `util/` | 附录 B.3：它承诺的是时机不是接口；出现第二个外部项目抱怨再说 |
| 给静态档开几何写入口 | `src/inking.cpp:148-158` 的断言就是拦这个的 |
| `InkingSpriteGroup` 的双档设计 | 需要单独设计（子项坐标跟随 / 组内几何传播 / 成组动画） |
| 给 `Canvas` / `Video` / `Scene` / `Window` 加动态档 | 几何设计期定型，没人要 |
| 顺手修 `CMakeLists.txt:4` 的 `VERSION 0.7.1` | 与本系列无关；它单独记在 `TaskGuide-Dynamic.md` §2.2（发布前必须处理，但别混进重构提交） |
| 顺手合并两份默认 `onRender` | G-00 的偏离里写了：helper 已共用，override 合不了 |
| **动态容器的滚轮** | 滚轮按渲染帧清零、动态组件只能按固定步长读（一帧 0~N 次）⇒ 会丢或双读。属框架级通道问题，见 G-07"为什么不做滚轮" |
| **每帧重建的优化** | 已由 **`TaskGuide.md` 的 `T-19`** 收拾（叶子只刷洞，2026-11-02 落地）。G 只管"组件能不能动"，不碰场景架构；静态版走 A13 仍每帧全量重建（外部变换无界，包络算不出来） |
| 顺手修 A4 的文档偏离（"动态组件只能在 Tick 里读滚轮"） | 那句话现在是**对的**（隐患在时序，不在那句话），要改的是通道本身；本条与上面"滚轮"一并处理 |

## 附录 A 自检记录（MD-05，2026-11-02）

### A.1 脚本基线：它查不到这份文档

```bash
python <skill-dir>/scripts/check_task_book.py docs/zh/TaskGuide-G.md
== docs\zh\TaskGuide-G.md ==

合计：0 个错误，0 条提示
退出码=0
```

**这个 0 不是"通过"，是"没查"。** 脚本 `check_task_book.py:24` 的
`TASK_HEADING = re.compile(r"^###\s+(T-[0-9A-Za-z]+...)")` 写死了 `T-` 前缀，`G-` 系列
一个都进不了检查列表。证据：`TaskGuide-Dynamic.md` 里做过实验——把全文改成 `G-` 系列、
故意塞一条正文不存在的任务、再把进度表状态改成与正文矛盾，脚本照样报 0 个错误、退出码 0。
所以本系列的验收**不依赖这个脚本**，改用下面 8 条人工核对。

### A.2 人工核对（逐条可判真假）

| # | 检查项 | 判据 | 结果 |
|---|---|---|---|
| 1 | 五段齐全 | 每个 `### G-xx` 下都有 目标 / 落点 / API / 验收 / 偏离，无空段 | **通过**：9 个任务 × 5 段 = 45 段齐全；另有 9 条"命中表与登记影响"（复核后仍为 9/9/9/9/9/9，逐个字段 `grep -c` 验的） |
| 2 | 编号连续 | `G-00`…`G-08` 无重复无跳号 | **通过**：`grep -oE "^### G-[0-9]{2}"` 得 9 个，各 1 次 |
| 3 | 进度表与正文一致 | 表里每个编号都能在正文找到标题 | **通过**：表 9 行，正文 9 个标题，一一对应 |
| 4 | 验收可断言 | 每条验收能回答"谁在什么条件下跑什么、看到什么算过" | **通过**：全部验收条目都是数值 / 状态 / `ctest` 名，无"看起来对"；复核后又补了 4 条（G-05 的布局容器、G-07 的包络计数、G-08 的旧产物 diff 与 sprite 断言） |
| 5 | 编号可 grep | `grep -rn "G-0" include/ src/` 能命中实现注释 | **部分适用**：`G-00` 四处注释末尾都标了 `G-00`，`grep -rn "G-00" include/ src/` 命中 4 处。其余 8 个任务尚未实施，**从 `G-01` 起**每个新增/改动的文件都要在注释里带 `G-xx` |
| 6 | 系列已登记 | `docs/zh/TaskGuide.md` §1.1 有 `G` 行 | **通过**：§1.1 已从"六个系列"改为"七个系列"，`G` 行指向本文件 |
| 7 | 文档体系齐 | `docs/zh/api/` 与 `CHANGELOG.md` 在落点 / 门槛里 | **通过**：写在 §6.1 全量发布门槛（发布前必须做，不是每个任务做） |
| 8 | 边界任务的断言 | 改了静态 / 动态边界的任务，验收含 `src/inking.cpp` 的 `static_assert` | **修正后通过**：G-01 / G-03 / G-05 / G-07 各有断言条目；首次自检发现 G-03 与 G-05 的**落点写了"补断言"但验收里没有对应条目**，已补 |

### A.4 复核记录（2026-11-02，第二遍）

按"疏漏 / 与设计意图冲突 / 性能下降 / 与生成器冲突"四个角度重查了一遍，改了四处，都是**原文写错或漏写**，不是补细节：

| # | 角度 | 原稿 | 实际 | 处置 |
|---|---|---|---|---|
| 1 | **性能（并推翻了自己的理由）** | §1 说"用 A13 会每帧重排，所以要用动态档" | 四个几何入口也调 `MarkStructureChanged()`，**同价**：都是每帧一次整表重建 | §1 改写 + 新增 **§2.1 已知成本**；`TaskGuide-Dynamic.md` 附录 A.3 加了"更正"段，原句划掉保留 |
| 2 | **设计意图冲突** | G-01 让 `AcceptsExternalTransform()` 返回 false（"避免两套真相"） | 这是**删掉一个正交能力**；风险在"拖动写哪边"，不在外部变换本身，而两者在绘制/命中处天然复合 | G-01 改为"A13 保留、只把内置拖动钉在几何上"，验收加"两者复合"一条 |
| 3 | **疏漏（会静默失效）** | G-05 只说 `SetText` 不自动撑高 | `FitToContent()` 在**布局容器里会被 `ApplyLayoutRect` 覆盖**，写进去的尺寸下一帧就没了 | G-05 加"决定二"：先判是否被布局管着，是则返回 false + WARN；验收加一条 |
| 4 | **与生成器冲突（5 处）** | G-08 只写了 `Emit.cpp` 与 `CXXCSS.md` 两句 | 实际有 5 处：3 张字段表加 `dynamic`、`kRejectedImageFields` 删 `dynamic`、3 个 reader + 3 个 Config、Emit 的 include 也要换、`Fields.h:232` 描述串、`CXXCSS.md` §9 锚点块要 `--dump-fields` 重生成 | G-08 改成五行表格；验收加"旧产物逐字不变""sprite 也可断言""field_table 的修法是重生成" |

另加一条**新发现的框架级问题**（不属于本系列，但必须留档）：**动态组件的滚轮没有可靠读取时机**——滚轮按渲染帧清零，动态组件只能在固定步长回调里读，一帧 0 次（丢）或 2 次以上（双读）。G-07 因此**不做滚轮**，只做滚动条拖动；`§7` 记明属框架待办。

### A.3 已知偏离（不是问题）

| 项 | 说明 |
|---|---|
| 第 5 条暂不适用 | 本系列还没实施。它是"实施期"检查项，不是"文档期"检查项——写在这里是为了第一次提交时有人记得 |
| 编号与 `TaskGuide.md` 的 `T-` 系列不同构 | `T-1`…`T-4` 用连字符 + 单位数，`G-00`…`G-08` 用连字符 + 两位数。差异原因是本系列一开始就预计超过 10 项，两位数是排序友好的。`TaskGuide.md` §1.2 没有规定位数，不冲突 |
