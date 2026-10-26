# 场景与场景库

**回到API目录：**
[API index](../API.md)

场景是 **window 之下最高的节点**，继承静态锚点。它永远静态：没有位置与尺寸写入口，
只负责"渲染 / 不渲染"。会动的东西压在 widget 层。

本页覆盖四个头文件：

| 头文件 | 内容 |
| --- | --- |
| `scene/InkingScene.h` | 场景本体（生命周期、两组逻辑、绘制列表、命中、输入派发） |
| `scene/SceneLibrary.h` | 进程级场景登记表（静态类） |
| `scene/SceneRegisterToken.h` | RAII 登记令牌（模板） |
| `scene/HitTable.h` | 静态命中表（簇 + 格子 + CSR 候选） |

`#include <scene/InkingScene.h>` 一处即可把前三个带进来。

## 一、登记：同名同期只能有一个

场景**构造即登记**（`registerToSceneLibrary()` 在构造函数末尾）。名字被占了就**当场驳回**
——对象照样构造出来了，但它没进库，所以既不会渲染也不会被 tick，是一块"停放"的荒地。
想用这个名字，得先把占用者 `SceneLibrary::CloseScene(名字)` 关掉。

**库不拥有场景对象**：关掉也不析构它，生命周期始终归调用方。于是
"从库里拿指针 → 关掉 → 再自己决定何时析构"不会踩野指针。

登记本身用 RAII 令牌（`RegisterToken<InkingScene, SceneLibrary>`），令牌是场景的成员，
析构时一定先于场景走完——**注销不会漏，也不会晚**。令牌必须记住"我到底进没进库"
（`_active`）：驳回的令牌是空转的，析构时**绝不能**去注销，那会把正主从库里摘掉。

## 二、四种状态：两组回调的组合

| 状态 | 谁造成 | 渲染 | `onRenderBoundTick` | `onTick` |
| --- | --- | --- | --- | --- |
| **活跃** | `SetActiveScene(this)` | ✅ | ✅ 每渲染帧一次 | ✅ 固定逻辑步 |
| **静默** | 被别的场景顶掉 | ❌ | ❌ | ✅ **照跑** |
| **停放** | 注册被驳回（同名） | ❌ | ❌ | ❌ |
| **关闭** | `CloseScene` | ❌ | ❌ | ❌ |

**静默不等于销毁**：数据还在、能读，只是不渲染。"后台的事情不该因为切场景就停摆"，
所以固定逻辑步照跑。

- 想"**不渲染但逻辑动**"→ 写 `onTick`（它不依赖活跃态）。
- 想"**不渲染就彻底别动**"→ 写 `onRenderBoundTick`。
- 更细的频率（例如 24fps 的动画）**不走这两个钩子**，去订阅 `Timeline`
  （见 [InkingRuntime.md](InkingRuntime.md)），那里按需采样。
- **关闭 ≠ 销毁**：`DestroyScene` 只入队，帧末（`CommitPendingDestructions`）才真析构。

## 三、渲染树：骨架 + 扁平绘制列表

层级（`_parent`）是**唯一真相源**，场景持有的绘制列表只是它的派生索引。

> **0.5.0 起这句话是字面意义上的**：`_contentDirty` 时 `sortDrawList()` 从场景根做一次
> **先序 DFS**，扫到谁就收谁（顺带把 `_scene` 认领到本场景）。以前是**增量登记**
> （构造时沿父链登记一次），于是"先建子树、后 `AddChild`"的节点永远进不了列表
> ——症状是**节点在、`IsVisibleInTree()` 为真、就是不画**，且不报错。树本来就在那儿，
> 扫一遍 O(N)，比维护"哪次登记漏了"便宜得多。
>
> 增量入口 `AddDrawNode()` / `RemoveDrawNode()` 仍然保留：前者退化成"标脏"，
> 后者仍要立刻从列表里摘掉（节点可能正在析构，留着就是野指针）。

为什么扁平而不是递归：

- 绝对坐标必须走父链，而 `GetAbsX()` 是 O(深度)；扁平列表里父指针是现成的；
- 命中侧已经定了"压平成 HitTable + 平铺 `_drawList`"，渲染跟着扁平，两边遍历才不会越走越远；
- 线性遍历对缓存友好，也没有深递归的栈风险。

顺序的维护方式是**标脏 + 帧末惰性重建**：骨架变化只把场景标脏，不立刻排序；
重建推迟到 `Render()` 开头做。提交顺序是 **z 从小到大**（先画底下的），
与 `IsAbove` 的答案相反但**规则同源**——两边不同源，就会"画的是 A、点到的是 B"。

**显式重建**（`RebuildRenderList()` / `RebuildHitTable()`）给三个场合用：自检想在
"下一帧之前"就断言、宿主怀疑表与树不一致、有人在树外改了父子关系。正常路径不需要它们。

## 四、三个"脏"的三个消费点

| 脏 | 触发源 | 消费点 | 谁消费 |
| --- | --- | --- | --- |
| **命中脏** | 几何 / 可见性 / 层级 / 父子 | **帧首** | `sortDrawList()` → `rebuildHitTable()` |
| **指针脏** | 指针状态变了，悬停判断还没做 | 帧末 | `ConsumeFrameDirty()` |
| **重绘脏** | 颜色 / 文字 / 变换 | 帧末 | `ConsumeFrameDirty()`（记账 + 计时清零） |

指针脏**不代表命中表要重烘**——指针移动不改任何几何。把两者混在一起，就会"鼠标一动
就重烘整张表"。

## 五、命中：烘焙表 + 动态洞 + 三态

`HitTable` 的形态是 **簇（粗块）→ 簇内均匀格子 → CSR 候选**：

- **格子只负责缩小范围**：命中与否仍由节点自己的 `HitTest` 回答，所以格子算粗了只是候选
  多几个，**不会误命中**。
- **候选按层级降序**填（`IsAbove`：树序 + 兄弟内 z），"第一个真正包含该点的"就是最上面那个。
  这条规则是**全序**——任何两个条目都可比。
- **建表正比于 UI 数量**，不正比于画布面积（空白区没有簇）。

与设计稿的两处差异（写在这里免得下一个人以为漏了）：

1. **没有溢出桶**：组件**构造即登记进绘制列表**，而表跟着列表重建——所以"运行时新增 UI"
   表现为"表脏了"，不是"表外还挂着条目"。
2. **没有跨簇条目仲裁**：层级虽然是局部的（树序 + 兄弟内 z），但仍是**全序**，
   不存在"说不清谁在上面"的两个条目，所以查表按这个序降序取第一个命中的就行。
   一个条目会登记进多个簇（大面板横跨粗块），那只是为了查得到，不涉及"谁在上面"。
   **等到引入真正不可比的东西（允许子节点逃出父级的 portal 层）时，这两条才要补回来。**

**动态组件不进表**：它自判命中，场景在表里给它挖一个**洞**（`AddHole`）。查询落在洞里时，
才去现问动态层最上面那个，再与静态结果按同一套层级规则合并。

## 六、输入派发：位置 / 焦点 / 快捷键

每帧顺序（窗口层调，见 [InkingWindow.md](InkingWindow.md)）：

```
ResetFrameFlags → 事件泵（MouseInput / KeyboardInput）
   → 快捷键 Dispatch（命中就消费本帧 Down 事件）
   → Scene::SetPointerState / SetPointerWheel / SetKeyboardInput
   → Scene::DispatchPointer       （按位置：hover / press / click / wheel / drag）
   → Scene::DispatchKeyboard      （按焦点：key / text / typing）
   → Scene::TickLogic / TickFrame （逻辑与动画）
   → Render
   → Scene::ConsumeFrameDirty     （帧末消费）
```

**为什么快捷键排在最前**：快捷键是"不管焦点在哪都响应"，它必须住在焦点**之上**。
命中之后那一下按键不该再推给焦点组件（否则输入框里冒出一个控制字符）。
`KeyboardInput::ConsumeFrameEvent` 只消费**本帧 Down 事件**，不动跨帧的 `IsKeyDown`。

**为什么键盘派发排在指针之后**：先按位置把焦点算好（可能刚因为点击换了人），
再把这一帧的按键推给**新的**焦点节点——否则"点进输入框并立刻打字"的第一下会丢。

**焦点规则只有三条**：

1. **点击获得**：指针按下时命中的节点，若 `WantsKeyboardFocus()` 为 true，焦点归它；
2. **点别处就换**：下一次按下落在别的节点（或空白）上，旧焦点先收到 `onFocusChanged(false)`；
3. **看不见了就释放**：焦点节点在树里不可见、或从渲染树里摘掉时自动清空。

业务也可以直接调 `SetKeyboardFocus(node)`（"打开弹窗把焦点给输入框"），那是显式接管。

## 七、预加载：把"构造"与"init"分开

场景构造即登记，构造体里做重活会拖住整个启动。预加载把重活挪到构造之后：

| 走哪条路 | `onPreload` 跑在 | 收尾在哪 |
| --- | --- | --- |
| `SceneLibrary::PreloadScene(名)` | **管家线程** | UI 线程帧首（`DrainPreloadCompletions`） |
| `InkingWindow::Show(名)` | UI 线程（同步） | 紧随其后 |

两条路**共用同一套状态机**（`Idle → Queued → Done / Failed`，一次 CAS 推进），
所以"先手动调过、再 Show"只跑一次，反过来也一样。

`onPreload()` 里**只能做与 UI 无关的准备**，三条禁令（越界就是数据竞争，而且不报错、只偶发）：

1. **不要碰场景树**（`new InkingStaticButton(this, ...)` 会登记进绘制列表，UI 线程正在遍历）；
2. **不要碰渲染器**（`SDL_CreateTextureFromSurface` 不是线程安全的）；
3. **不要读写任何渲染 / Tick 里也会读写的成员**（例外：`IsPreloaded()` 为真之后的读，
   那时有一次 release/acquire 的先行关系兜着）。

第 1、2 条要干的活放 `onPreloadFinished(bool ok)`——它**整个生命周期只来一次**。

## 用到的数据结构

>**InkingScene::DrawItem**

```C++
struct DrawItem {
    InkingWidget* node = nullptr;
    InkingWidget* parent = nullptr;
    float absX = 0.0f;
    float absY = 0.0f;
};
```

绘制列表里的一项。`absX/absY` 是**静态节点**建表时算一次就冻住的绝对左上角；
**动态节点存在这里的是建表那一刻的快照**，每帧提交时会重新算——别拿它当动态节点的当前坐标。

>**InkingScene::PreloadState**

```C++
enum class PreloadState : std::uint8_t { Idle = 0, Queued, Done, Failed };
```

跨线程（管家线程写、UI 线程读），所以是原子量。

>**SceneLibrary::RegisterResult**

```C++
enum class RegisterResult { Ok, NameTaken, Invalid };
```

`NameTaken` = 同名被占（停放态）；`Invalid` = 名字为空或指针为空。
"被关过就不能再注册"由 `IsClosed()` 表达，不在 `RegisterResult` 里。

>**HitKind / HitResult**（`scene/HitTable.h`）

```C++
enum class HitKind { Miss, Static, Dynamic };
struct HitResult {
    HitKind kind = HitKind::Miss;
    InkingWidget* node = nullptr;
    bool IsHit() const noexcept;
};
```

`HitTable::Query(x, y, bool* holeHit)` 返回的就是它；`holeHit` 顺带告诉调用方
"这个点落在动态洞里"。

>**HitTable::Entry / Cluster**

```C++
struct Entry {               // 一条静态条目
    InkingWidget* node;
    ShapeBounds envelope;    // 相对场景左上角（设计坐标）
    bool passThrough;
};
struct Cluster { float cellSize; ... };   // 一个粗块 + 它自己的格子尺寸
```

`HitTable` **不认识场景**：它只收"条目（节点 + 包络 + 让不让过）"和"动态洞的包络"，
由 `InkingScene` 负责在帧首重建、在派发时查询。所以表可以单独被自检对账。

## APIS

### InkingScene — Public

>**RebuildRenderList() / RebuildHitTable()（0.5.0 A5 / D6）**

```C++
std::size_t RebuildRenderList();
std::size_t RebuildHitTable();
```

立刻按当前树重建收集表 / 命中表，**不必等帧**。前者返回重建后的项数，后者返回静态条目数。

正常路径不需要它们：`sortDrawList()` 在结构脏时本来就会重建。三个正当用途——
自检想在"下一帧之前"断言、宿主怀疑表与树不一致、有人在树外改了父子关系。
`QueryHit()` 自己会先推一次重建，所以"窗口 Show 之前验证某个位置点得到谁"直接可用。

>**FindByName(name) / Activate(name)（0.5.0 B2 配套 / D7）**

```C++
InkingWidget* FindByName(const std::string& name) noexcept;
bool Activate(const std::string& name) noexcept;
```

`FindByName` 深度优先、先序、返回**第一个**同名节点（不含场景自己）；找不到返回 nullptr。
同名是允许的（配置复制粘贴），要全部拿到请自己走 `GetChildren()`。

`Activate` = `FindByName` + `InkingWidget::Activate()`。自检 / 快捷键 / 脚本用它代替
`dynamic_cast<InkingStaticButton*>` 再调 `TriggerClick()`——**调用方从此不认识控件类型**。

>**构造函数（三种）**

```C++
InkingScene();
explicit InkingScene(const std::string& sceneName);
explicit InkingScene(const std::string& sceneName, const AnchorData& anchorData);
```

第一条：不绑定场景名，**不登记进场景库**（运行期自建场景用）。
第二条：配置驱动，构造即登记；名字被占则进入停放态。
第三条：不用 CXXCSS，直接给场景必须的参数（一般用在小场景；场景通常要和窗口大小对齐，
否则会有奇怪的黑边）。推荐用 `k` 常量命名场景名，编译期就能排查问题。

拷贝 / 移动四个全 delete。

>**GetSceneName() / IsRegistered() / IsRejected() / IsClosed() / IsActive()**

```C++
const std::string& GetSceneName() const noexcept;
bool IsRegistered() const noexcept;
bool IsRejected() const noexcept;
bool IsClosed() const noexcept;
bool IsActive() const noexcept;
```

`IsActive()` 由 `SceneLibrary::SetActiveScene` 维护：同一时刻最多一个为 true，
切场景时旧的自动变 false（静默）。

>**TickLogic(fixedStepSeconds) / TickFrame(deltaSeconds)**

```C++
void TickLogic(double fixedStepSeconds);
void TickFrame(double deltaSeconds);
```

由时间线 / 框架驱动。`TickLogic`：`onTick` + 遍历绘制列表里的**动态组件**逐个 `Tick`
（步长是**固定的**，不是真实 delta——状态机要的就是确定的步）。
`TickFrame`：**只在活跃时**调 `onRenderBoundTick`（真实 delta）。

>**BuildDrawList() / GetDrawItemCount() / NotifyStructureChanged()**

```C++
const std::vector<DrawItem>& BuildDrawList() noexcept;
std::size_t GetDrawItemCount() noexcept;
void NotifyStructureChanged() noexcept;
```

`BuildDrawList` 把绘制顺序重建到最新并返回只读列表（没脏时直接返回现成的）。
名字用 `Get` 是为了说明"它不会改你的东西"。

>**ConsumeFrameDirty()**

```c++
void ConsumeFrameDirty() noexcept;
```

帧末统一消费（**只对活跃场景**）：推进每个节点的安静帧计数，然后清指朽脏 / 滚轮量。
它**不重建绘制列表**——那是帧首的事。静默 / 停放 / 关闭的场景不消费（脏要留着）。

>**GetRepaintingNodeCount() / GetHitDirtyNodeCount() / IsPointerDirty()**

```C++
int GetRepaintingNodeCount() const noexcept;
int GetHitDirtyNodeCount() const noexcept;
bool IsPointerDirty() const noexcept;
```

观测口。动画在跑时 `GetRepaintingNodeCount()` 应该一直 > 0，动画停下
`kRepaintQuietFrames` 帧之后回落到 0——"画面稳了"就是这么看的。

>**QueryHit(designX, designY)**

```C++
HitResult QueryHit(float designX, float designY) noexcept;
```

查一次命中：命中的最上面那个 + 三态。坐标是**设计坐标**。悬停与点击共用它。

>**GetHitTableEntryCount() / GetHitTableClusterCount() / GetHitTableCellCount() / GetHitTableMaxCellCandidates()**

```C++
std::size_t GetHitTableEntryCount() noexcept;
std::size_t GetHitTableClusterCount() noexcept;
std::size_t GetHitTableCellCount() noexcept;
std::size_t GetHitTableMaxCellCandidates() noexcept;
```

自检与预算估算用。最后那个是一次查询的候选上界——用来判断格子尺寸定得是不是太粗。

> **怎么读这些数（口径）**

上面这一族的**口径与读法**在 [../UsageReport.md §3「观测口与读数口径」](../UsageReport.md)
有完整一张表（含 `BatchStats`、`GetPaintCount()` / `GetReplayCount()`、
`GetDisplayTransformRevision()` 等）。三条最容易踩的：

- `BatchStats*` 是**累计值**，自检必须取前后差；
- `GetDrawItemCount()` **含隐藏节点**——它不是"可见节点数"；
- `GetHitTableEntryCount()` **不含容器子孙**（`OwnsSubtreeHit()` 为真的容器只登记一条包络），
  动态组件记为"洞"。

>**骨架登记写入口（只由 `InkingWidget` 调）**

```C++
void AddDrawNode(InkingWidget* node) noexcept;
void RemoveDrawNode(InkingWidget* node) noexcept;
void DetachSceneFromNodes() noexcept;
std::uint64_t GetStructureGeneration() const noexcept;
```

`GetStructureGeneration()` 是"骨架结构变了多少次"的计数（构造 / 析构 / 换父级 / 换场景），
自检用它钉"幂等性"（重复 `Relayout` 不该涨这个数）。

#### 指针

>**SetPointerState(designX, designY, down, inside[, source]) / SetPointerWheel(deltaX, deltaY)**

```C++
// 完整版（窗口层用）：两个**语义键** + 来源
void SetPointerState(float designX, float designY,
                     const PointerDownState& down, bool inside,
                     PointerSource source) noexcept;
// 便捷版（只关心主键的调用方 / 自检用）：转发到上面那个
void SetPointerState(float designX, float designY, bool primaryDown,
                     bool inside) noexcept;

void SetPointerWheel(float deltaX, float deltaY) noexcept;
```

窗口层每帧调，**只存不判**。坐标是**设计坐标**（窗口层已经用渲染器换算过，含 letterbox 偏移）。
滚轮一帧里调多次会**累加**；它是**一次性量**——本帧没派发出去也照样在帧末被清掉
（"滚了但没人要"就是丢掉，不该攒到下一帧）。

`down` 是 `PointerDownState{primary, secondary}`：**语义键**，不是设备键。
鼠标左右键一一对应；触摸轻点 = `primary`，**长按由场景自己转义成 `secondary`**
（见下面的"触摸长按"）。

`source` **只在按下那一刻被读一次**（内部锁定）。不能每帧照抄：触摸屏上用户
一边按着手指一边动鼠标，来源会被刷成 `Mouse`，长按计时被打断。

>**触摸长按 → 次键**

触摸屏上没有右键，长按是它唯一的表达方式。按住 `_longPressSeconds`（默认 **0.5 秒**）
且移动不超过 `_longPressSlop`（默认 **8 设计像素**）时，本次按下会被**改判**成一次
完整的次键交互：

```
按下 → Press(Primary,true)         （立刻，和轻点一样）
0.5s → Press(Primary,false)        （主键捕获作废，**不发 click**）
     → Press(Secondary,true)
抬手 → Press(Secondary,false) + Click(Secondary, x, y)
```

两个参数是 `protected` 成员（派生场景可在构造函数里改）。移动超过 slop 则**放弃**
本次长按（那是拖动 / 滚动），且**不会迟到触发**——手指停在远处继续按住也不弹菜单。

>**DispatchPointer()**

```C++
void DispatchPointer();
```

按当前指针状态派发一次：悬停进出、按下、抬起触发点击、滚轮、拖动。每帧调一次，**幂等**。

>**指针只读查询（给动态组件"自己拉"用）**

```C++
float GetPointerX() const noexcept;
float GetPointerY() const noexcept;
bool IsPointerDown() const noexcept;                 // 主键（语义同 0.4.x 的"左键"）
bool IsSecondaryPointerDown() const noexcept;        // 次键（右键 / 长按）
const PointerDownState& GetPointerDown() const noexcept;
MousePoint GetPressOrigin(PointerButton button) const noexcept;
bool IsPointerInside() const noexcept;
float GetPointerWheelX() const noexcept;
float GetPointerWheelY() const noexcept;
InkingWidget* GetPressedNode() const noexcept;           // 主键捕获槽
InkingWidget* GetSecondaryPressedNode() const noexcept;  // 次键捕获槽
InkingWidget* GetDraggingNode() const noexcept;
InkingWidget* GetHoveredNode() const noexcept;
```

静态组件的输入是**推**的，动态组件是**拉**的。`GetDraggingNode()` 与
`GetPressedNode()` 是同一个答案（"谁被按下"本来就是"谁收这次拖动的全部后续事件"），
单列一个名字是因为两个问题问的是不同的事——**而且这条关系只对主键成立**：
次键有捕获但不产生拖动。

`GetPressOrigin(button)` 是**按下点**（设计坐标）：`onPointerClick` 的坐标、
长按 slop 的基准、拖动增量的基准都取自这一份。它不是"指针位置"（那是
`GetPointerX/Y`），只在"正在按着 / 刚抬起"这两段时间里有意义。

#### 键盘

>**SetKeyboardInput(input) / GetKeyboardState()**

```C++
void SetKeyboardInput(const KeyboardInput& input) noexcept;
const KeyboardState& GetKeyboardState() const noexcept;
```

组件不必自己认识 `KeyboardInput`（那是窗口层的东西），与"指针状态由 `SetPointerState`
喂进来"同一条思路。想"按住就持续做"（方向键移动）或"现在在不在打字"就读这份状态。

>**SetKeyboardFocus(node) / GetKeyboardFocus() / ClearKeyboardFocus()**

```C++
void SetKeyboardFocus(InkingWidget* node) noexcept;
InkingWidget* GetKeyboardFocus() const noexcept;
void ClearKeyboardFocus() noexcept;
```

传 `nullptr` = 清空。规则见上面"焦点规则只有三条"。

>**DispatchKeyboard()**

```C++
void DispatchKeyboard();
```

把本帧的键盘事件推给**焦点节点**（每帧一次，排在 `DispatchPointer()` 之后），
并在"在不在打字"变化时推一次 `onTypingChanged`。

#### 只给库 / 窗口层调

>**MarkClosed() / SetActiveFromLibrary(active)**

```C++
void MarkClosed() noexcept;
void SetActiveFromLibrary(bool active) noexcept;
```

关掉之后不渲染、不再活跃、也不能再注册；数据全部保留仍可读
（"先把重要数据 move 出来再关"就是这么用的）。

>**预加载四个查询 + 三个动作**

```C++
PreloadState GetPreloadState() const noexcept;
bool IsPreloaded() const noexcept;
bool IsPreloadFailed() const noexcept;
bool IsPreloadFinished() const noexcept;

bool RequestPreload();      // 异步：任务丢进 TaskQueue，onPreload 在管家线程跑
bool PreloadNow();          // 同步：在 UI 线程直接跑完并当场收尾
bool ConsumePreloadFinish();// 有结果且没通知过 → 调一次 onPreloadFinished(ok)
```

`RequestPreload` 返回 false = 已受理过 / 已跑过 / 场景已关闭，所以重复调用天然无害。

### InkingScene — Protected 钩子

```C++
virtual void onTick(double fixedStepSeconds);          // 固定逻辑步
virtual void onRenderBoundTick(double deltaSeconds);    // 跟着渲染
virtual bool onPreload();                               // 耗时准备（禁碰树/渲染器）
virtual void onPreloadFinished(bool ok);                // UI 侧收尾，只来一次
virtual void onRenderScene(SDL_Renderer* renderer);     // 场景自己那一笔（默认不画）
void Render(SDL_Renderer* renderer);                    // 框架调：按 z 序提交绘制列表
void registerToSceneLibrary();                          // 构造末尾登记
```

`Render` 是 `protected`：只有框架该调它（窗口层经 `SceneLibrary::RenderScene` 走一道，
保证"只有活跃场景会被渲染"这条规矩有唯一落点）。

### SceneLibrary — 全部 static

>**RegisterScene(name, scene) / UnregisterScene(name, scene)**

```C++
static RegisterResult RegisterScene(const std::string& sceneName, InkingScene* scene);
static void UnregisterScene(const std::string& sceneName, InkingScene* scene);
```

一般不用直接调——`RegisterToken` 在构造 / 析构时替你调。

>**FindScene(name) / GetAllSceneNames() / SceneCount() / IsNameTaken(name)**

```C++
static InkingScene* FindScene(const std::string& sceneName);
static std::vector<std::string> GetAllSceneNames();
static std::size_t SceneCount();
static bool IsNameTaken(const std::string& sceneName);
```

`FindScene` 返回**非拥有**指针；找不到为 `nullptr`。

>**CloseScene(name)**

```c++
static bool CloseScene(const std::string& sceneName);
```

关掉：不再渲染、不再活跃、不能再注册；**不析构**。

>**PreloadScene(name) / DrainPreloadCompletions()**

```C++
static bool PreloadScene(const std::string& sceneName);
static void DrainPreloadCompletions();
```

前者把 `onPreload` 丢到管家线程；后者**在 UI 线程帧首**把结果交付回去
（`ConsumePreloadFinish`）——这就是"管家线程干完活之后回 UI 线程"的那条路。

>**AdoptScene(scene) / DestroyScene(name) / CommitPendingDestructions()**

```C++
static bool AdoptScene(std::unique_ptr<InkingScene> scene);
static bool DestroyScene(const std::string& sceneName);
static std::size_t CommitPendingDestructions();
```

`AdoptScene` 把所有权交给库；`DestroyScene` 只**入队**，`CommitPendingDestructions`
在帧末真析构（返回析构了几个）。**关闭 ≠ 销毁。**

>**SetActiveScene(scene) / GetActiveScene() / RenderScene(renderer)**

```C++
static void SetActiveScene(InkingScene* scene);
static InkingScene* GetActiveScene();
static bool RenderScene(SDL_Renderer* renderer);
```

`RenderScene` 是"只有活跃场景会被渲染"的唯一落点。

### SceneRegisterToken

>**RegisterToken&lt;Registered, Library&gt;**

```C++
template <typename Registered, typename Library>
class RegisterToken {
public:
    RegisterToken(std::string name, Registered* object);
    ~RegisterToken() noexcept;                        // 只有 _active 时才注销
    RegisterToken(RegisterToken&& other) noexcept;
    RegisterToken& operator=(RegisterToken&& other) noexcept;

    const std::string& GetName() const noexcept;
    Registered* GetObject() const noexcept;
    bool IsRegistered() const noexcept;               // 我到底进没进库
    Result GetResult() const noexcept;
};
```

拷贝禁、移动可。**被登记的对象不是拥有关系**。

### HitTable

>**Clear() / AddHole(envelope) / Build() / Query(x, y, holeHit)**

```C++
void Clear() noexcept;
void AddHole(const ShapeBounds& envelope) noexcept;
void Build() noexcept;
HitResult Query(float x, float y, bool* holeHit = nullptr) const noexcept;
bool IsInDynamicHole(float x, float y) const noexcept;
```

`AddHole` 给动态组件挖洞（省掉"每个动态节点都当成候选"）；`Build` 才真正建格子与
CSR 候选。`Query` 只回答"候选里第一个真正包含该点的"——**是否真的命中仍由节点自己的
`HitTest` 判**。

>**尺寸观测口**

```C++
std::size_t GetEntryCount() const noexcept;
std::size_t GetClusterCount() const noexcept;
std::size_t GetCellCount() const noexcept;
std::size_t GetCandidateCount() const noexcept;
std::size_t GetHoleCount() const noexcept;
std::size_t GetMaxCellCandidates() const noexcept;
```

## 常见坑

1. **以为 `CloseScene` 会析构场景**：不会。生命周期始终归调用方（或 `AdoptScene` 之后归库）。
2. **在 `onPreload` 里建组件 / 建纹理**：数据竞争，偶发、不报错。放 `onPreloadFinished`。
3. **两个同名场景**：后者静默进入停放态（`IsRejected()`）。除了"什么都没发生"，没有别的提示。
4. **把"关闭"当"销毁"**：关掉只是停；要释放显式 `DestroyScene` + 帧末提交。
5. **在静默场景里指望 `onRenderBoundTick`**：静默不渲染，那组回调也不跑。
6. **`SetPointerState` 传窗口像素**：它要的是**设计坐标**。

---

> 本文档对应版本：**0.5.3（0.x 发布版）**。
