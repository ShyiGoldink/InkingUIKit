# 任务书：场地层 `field/`（1.2 的第一个引擎能力）

> 状态：**已执行**（2026-11-05：T-01 ~ T-05 全部完成，自检与 `ctest` 全绿）
> 日期：2026-11-05
> 来源：2026-11-05 四轮商讨的定稿结论。参考材料 `D:/GoldingCore/probe-perspective/`
> （DESIGN.md / REQUIREMENTS / TASK-CORE）与 `D:/GoldingCore/scene25d`（实验品，**不作规格**）。
> **冲突时以本文为准**，那些文档里过期的部分见 §8。
> 目标版本：`1.2`（只加不改，不改既有行为；2.0 再做 ABI 冻结，之后只修 bug）
> 涉及层：`include/field/`（新）· `src/field/`（新）· `docs/zh/api/`（新页）

---

## 0. 结论（TL;DR）

四轮讨论收敛成六条，全部体现在 §4：

1. **投影不是透视，是"正俯视 + 斜投影"**；"越远越小"是可选的一次除法，不是基础模型。
2. **场地只管几何与提交**。实体、系统链、黑板、进程模型一律不进库（这是"库进化成引擎库"的边界，不是遗漏）。
3. **场地是场景的固定槽位**，一个场景最多一个，永远在 UI 之下。
4. **地面命中不靠图元**，靠闭式反解；物体命中复用每帧投影结果，不重算。
5. **瓦片是地形面的内容**：库只给"可见格 + 格四边形"，autotile 与变体归引擎。
6. **地面和天空是场地的字段，不是场地的元素**。这一条在类型上就钉住（配置 vs 数组）。

| 编号 | 任务 | 批次 | 落点 | 代价 |
| - | - | - | - | - |
| T-01 | 投影与格子纯函数层 | 1.2-a | `include/field/InkingFieldProjection.h`（新） | 中 |
| T-02 | 场地组件：地形面 + 物体面 | 1.2-b | `include/field/InkingField.h`（新） | 高 |
| T-03 | 命中与点击回调 | 1.2-b | 同上 | 中 |
| T-04 | 自检 + 静默失效三处同步 | 1.2-c | `selftest.cpp` + `AGENTS.md` §7 | 低 |
| T-05 | 文档页与 API 索引 | 1.2-c | `docs/zh/api/InkingField.md`（新） | 低 |

**T-01 可以单独发布**：它零依赖、零 SDL、可脱窗口单测，引擎的 core 进程（headless）只需要它。

---

## 1. 维护规则

三段式、完成即压缩、DoD 四条沿用 `2026-11-04-dev010-game-engine-tasks.md` §1，不复述。本轮额外四条：

1. **`field/` 的依赖方向是硬的**：只许 include `dataStruct/`、`asset/`、`widget/` 与标准库；**不许** include `scene/`、`window/`、`render/`。这条是"场地能被无窗口测试"的前提。
2. **纯函数头只许 include `<cstdint>` / `<cstddef>` / `<cmath>`**。`InkingFieldProjection.h` 里出现任何 SDL 类型即视为失败（本库"新公开头零 SDL 依赖"的既有规矩）。
3. **设计尺寸不许自己存**。组件构造时取 `InkingDesignWidth()` / `InkingDesignHeight()`，且每帧重读；纯函数层用显式参数（为了单测）。两份尺寸不同步 = "某个东西位置差了一截"。
4. **任何"设了不生效"的入口必须登记静默失效**（§7），并且自检里要有一条断言证明"它真的不生效"——不是只写文档。

---

## 2. 基线事实（核实结论）

| 条目 | 现状 | 证据 |
| - | - | - |
| 批量提交能力 | **已有** | `include/image/InkingGeometryLayer.h`（dev010 T-02/T-03）：`SetQuads` 自动切批、`SetGeometry` 任意顶点 |
| 纹理句柄 | **已有** | `include/asset/InkingTexture.h`：`InkingLoadTexture` / `InkingTextureHandle` |
| 投影 / 相机 / 世界坐标 | **完全没有** | `grep -r "Camera\|WorldPos\|Entity" include/` 无结果 |
| 九宫格 / 遮罩 | **已有** | `dataStruct/InkingNinePatch.h` / `image/InkingMask.h` |
| 公开面成本 | **每加一个头都是永久承诺** | `CMakeLists.txt:533` 是 `install(DIRECTORY include/ ...)` |
| 绘制坐标 | **= 设计坐标** | 窗口层 LETTERBOX，`GetMagnification()` 恒 1.0 |
| 5000 图元成本 | **0.94 ms/帧**（一个图层 + 5000 quad） | dev010 T-02 实测；同规模 5000 个组件 4.8 ms |
| `SetQuads` 上限 | **自动切批**（撞 16384 顶点就切，5000 图元 = 3 批） | dev010 §2.2 |
| `SetGeometry` 上限 | **16384 顶点，超了整批拒绝、保留上一帧** | dev010 T-03 三条硬约束 |
| `SetQuads` 与 `SetGeometry` | **互斥**，后设的赢并清掉前者 | 同上 |

**两条直接决定实现形态的推论**：

- 物体面用 `SetQuads`（自动切批，可放心交 5000 条）。
- 地形面必须用 `SetGeometry`（平行四边形不是旋转矩形），而它**超限整批拒绝** ⇒ 地形面必须自己分批/分池（§5 T-02）。

---

## 3. 术语表（**先读这一节**）

这一节存在的原因：`board` / `ground` / `world` 三个词都已被占用，且"设计坐标"在库里已有另一个含义。不统一命名，后面每轮讨论都要重新解释一遍。

| 词 | 含义 | 代码 | 备注 |
| - | - | - | - |
| **场地** | 带深度的地面空间 | `InkingField` | 中文不叫"格子场"；`field` 单独出现时只许指"配置字段" |
| **场地坐标** | 连续世界量 `(x, d, y)`，单位 = 像素 | `InkingFieldPos` | **不叫世界坐标、不叫设计坐标** |
| **格坐标** | 整数 `(i, j)` | `InkingFieldCell` | `i` ↔ x，`j` ↔ d |
| **设计坐标** | 库既有的 UI 布局空间 | （既有） | 1280×720 虚拟画布；**保留原义，不许挪用** |
| **格宽 / 格深** | 一格的横向 / 进深世界尺寸 | `cellWidth` / `cellDepth` | 就是贴图像素尺寸 |
| **倾斜度** | 深度线偏离垂直的角度（度） | `skew` | 0 = 正俯视；有效区间 `[0, 80]` |
| **深度缩放** | 最远处相对近处的尺寸比 `(0,1]` | `depthScaleFar` | 1 = 无透视（格子模式锁定 1） |
| **格子模式** | 正俯视、1 单位 = 1 像素、无限格 | `Mode::Grid` | 设计/美术基准视图 |
| **倾斜模式** | 斜投影（+ 可选透视）、有限地图 | `Mode::Oblique` | 原文档叫"地平线模式"，**改名理由见 §8** |
| **物体** | 场地里的一个立着的图元 | `InkingFieldObject` | 宽 / 深 / 高 / 离地 四个量 |
| **离地高度** | 脚底离地多高（"飞起来"） | `lift` | 与 `height` 不是一个东西 |
| **起点角** | 物体的默认锚点：世界 `(min x, min d)` 那个角 | `Anchor::Corner` | **不叫"左下角"**，理由见 §4.3 |
| **地形面** | 天空 + 地面底色 + 瓦片，固定在最下层 | （内部） | 自由顶点提交面 |
| **物体面** | 立着的东西，按 `(d, lift)` 排序 | （内部） | quad 提交面 |

命名纪律：**坐标永远用单个字母 `x / d / y`，尺寸永远用词 `width / depth / height`**，两者拆成不同类型（`InkingFieldPos` / `InkingFieldSize`），混用编译不过。

---

## 4. 模型定稿

### 4.1 投影

```
ld = d − camera.d
s  = 1 / (1 + h·ld)          h = (1/depthScaleFar − 1) / farD
sx = cx + (x − camera.x + sin(skew)·ld) · zoom · s
sy = H  − cos(skew)·ld · zoom · s − lift · zoom · s
```

- `cx = designWidth/2`，`H = designHeight`。
- **格子模式**：`skew` 锁 0、`depthScaleFar` 锁 1 ⇒ `sx = cx + (x − camera.x)·zoom`、`sy = H − ld·zoom − y·zoom`。此时 1 世界单位 = 1 像素（`zoom = 1`）。
- **倾斜模式**：`skew`、`depthScaleFar` 自由；地图有界（`cols × rows`）。

**只有 `s = 1/(1 + h·ld)` 这种形式能让世界直线保持是直线。** 任何其它形式（线性衰减、任意曲线）都会让纵线变弯——scene25d 那个 21.36 px 的曲线缺陷就是这么来的。这不是可选项，是正确性条件。

**透视与倾斜是两个正交参数**，不要合并：

- `skew > 0` 让深度线朝一侧倾斜（格子变平行四边形）。
- `depthScaleFar < 1` 让远处变小。**它必然同时带来汇聚**——缩放和汇聚是同一个除法的两面。消失点在 `cx + sin(skew)/h`，即**倾斜那一侧**；`skew = 0` 时才会落到屏幕正中（那是被否决的"单点透视"）。

### 4.2 格子

```
格 (i, j) 覆盖 [i·cellWidth, (i+1)·cellWidth) × [j·cellDepth, (j+1)·cellDepth)
CellOf(pos)     : i = floor(x / cellWidth),  j = floor(d / cellDepth)
CellCenter(i,j) : ((i+0.5)·cellWidth, (j+0.5)·cellDepth)
```

- **必须 `floor`，不许 `trunc`**。无限格子意味着负坐标是常态；`trunc(−0.5) = 0` 会把左上区域全算进第 0 格，而小场景测不出来。
- **格线由整数索引生成，不许从世界坐标反推**。反推的线在浮点边界上和命中差一格，是坐标类 bug 里最难查的一种。
- 无限格子**只在格子模式**。倾斜模式的格子数由使用者给定（`cols × rows`）。

**1px 判据（闭式，配置期算一次）**：

```
远端格子的屏幕边长 = min(cellWidth, cellDepth) · zoom · depthScaleFar
若 < 1px  ⇒  ld_1px = (min(cellWidth,cellDepth)·zoom − 1) / h
```

`depthScaleFar = 1` 时该不等式恒成立 ⇒ **平行投影下格子永远不会小于 1px**，这正是"无限格子"能成立的原因。亚像素继续画不是"看不出"，是**几何在亚像素抖动**（闪烁、摩尔纹），所以必须裁，并且要有计数。

### 4.3 物体

```cpp
struct InkingFieldObject {
    // 位置（场地坐标；锚点见 anchor）
    float x, d;
    float lift;              // 脚底离地高度
    // 尺寸（世界单位 = 贴图像素）
    float width, depth, height;
    // 外观
    InkingTextureHandle texture;
    InkingShapeBounds   src;
    std::uint32_t       tint;
    float               rotation;   // 度，顺时针为正（与 InkingQuad 同向）
    bool                flipX;
    // 投影行为
    bool followPerspective;  // false ⇒ 尺寸与 lift 的 s 全按 1 处理
    bool hitEnabled;
    // 锚点
    InkingFieldAnchor anchor;  // Corner（默认）/ Center
};
```

- **`depth` 不参与视觉**。立着的东西在屏幕上是正矩形，不斜；`depth` 只用于占位（影子、选中框、格子吸附、将来的立体命中）。若某个东西"贴地且要斜"，它是瓦片那一类，不是物体。
- **`lift` 与 `height` 必须分开**：`lift` 是脚底离地（跳跃、台面），`height` 是自身高度。混成一个字段后，"角色跳高一点"会变成"角色变高"。
- **`lift` 与 `height` 都乘 `s`**，与宽度一致——整个物体等比缩小。这是"远处跳得更高"这个假象的唯一解法。
- **锚点是"起点角"（世界 `min x, min d`），不是屏幕上的左下角**。`skew > 0` 时两者相差 `depth·sin(skew)` 的横向量，正好是半个格子的斜移；而且角度越小差得越少，小角度下根本测不出来。`Corner` 模式向 `+x / +d / +y` 展开，和 `CellOf` 的 `floor` 约定同源。
- **翻转只翻 UV，不翻几何**。`flipX` 若去改 `x`，锚点语义当场变掉，占位会跑到锚点左边。
- **`followPerspective = false` 在格子模式里静默无效**（那里 `s ≡ 1`），必须登记（§7）。

### 4.4 层序

```
背景板（既有 SetBackdrop）
  └ 地形面 ①  天空底色 / 地面底色 / 瓦片     ← 自由顶点，固定最下
  └ 地形面 ②… 超出 4096 四边形时的续层
  └ 物体面    按 (d, lift, 提交序号) 排序    ← quad 批
  └ UI 树     既有控件，树序
```

- **场地是场景的固定槽位**（`InkingScene::SetField`），最多一个，层序由构造保证，不靠文档。
- **场地必须是场景的直接子节点**。它的提交坐标是**绝对设计坐标**，挂到任何有平移/缩放的容器下都会整体错位，而且不报错。
- 地形面与物体面**分成两个图层是接口逼出来的**：`SetQuads` 与 `SetGeometry` 互斥，而地面/瓦片在倾斜下是平行四边形，`InkingQuad`（矩形 + 旋转）表达不了。
- 地形面永远在物体面之下：地面本来就是背景，不需要与物体逐笔交错。**代价知情**：贴地且会遮挡的东西（草、石头、树根）必须当物体，不能当瓦片。

### 4.5 命中

三条路，都不用 zoom 逆变换：

| 目标 | 判据 | 成本 |
| - | - | - |
| UI 控件 | 既有命中表 | 已有 |
| 场地物体 | **每帧投影时存下的屏幕矩形**，逆序扫（近的在前） | 零 |
| 地面 / 格子 | 闭式反解，一次除法 | 一次除法 |

- **仲裁：UI 优先，UI 未命中才进场地**。不要真的把两边的 zindex 混成一个数——UI 的顺序是"树序 + 兄弟 z"，不是可加的数。
- **命中绝不重新投影**。复用提交时的正向结果，否则两份算式迟早分叉，表现是"看着点中了却打偏"。
- 命中形状用**屏幕矩形**（含旋转时绕中心逆旋转一次），不用世界盒再投影——`followPerspective = false` 时两者会分叉。
- `tint` alpha 为 0、`hitEnabled = false`、被 1px 规则剔除的，都**不可命中**（看不见的不该能点）。
- `InkingField` **不进命中表**（`SetHitPassThrough(true)`），否则它铺满全屏会挡住所有 UI。

**回调**：`InkingField` 可被派生并重写 `OnFieldClick`，也可挂一个回调。参数是 `const InkingFieldObject*` —— 指向**调用方传进来的那份内存**，不是副本。为此 `SetObjects` 必须是只读视图（§5 T-02），否则指针指向内部副本，用户映射不回自己的对象。

业务侧派生依赖一条硬约定，必须在头注释里写死：

```cpp
struct MyEnemy {
    InkingFieldObject base;   // 必须是第一个成员
    int hp; float cooldown;
};
static_assert(offsetof(MyEnemy, base) == 0);
```

首成员与整体指针可互转是标准保证（标准布局），`reinterpret_cast` 拿回去是安全的。**这就是"库不承诺实体、业务自己派生"的落地方式**：派生在业务侧，库侧只是一段 POD。

### 4.6 排序与分批

- 排序键：**主键 `d`，次键 `lift`，末键提交序号**。
- 次键用 `lift` 是知情的取舍：跳跃过程中 `lift` 变化，**同深度且屏幕重叠**的两个物体可能交换两次顺序、边缘闪一下。接受闪烁，因为不加锁；但**末键必须是提交序号**，保证同帧同输入排序结果完全一致——否则会变成一个"既会闪、又不可复现"的问题。
- 排序排的是**索引数组**，不动数据本身；提交与命中共用这个序列（同源）。
- `SetQuads` 自动切批（撞 16384 顶点），`SetGeometry` **超限整批拒绝** ⇒ 地形面自己分批。地形面层池上限 8，超出截断并计数（不静默）。

---

## 5. 分批任务

### 5.1 第一批 1.2-a：纯函数层（T-01）

**落点**：`include/field/InkingFieldProjection.h`（新）+ `src/field/InkingFieldProjection.cpp`（新）

```cpp
enum class InkingFieldMode : std::uint8_t { Grid = 0, Oblique = 1 };
enum class InkingFieldAnchor : std::uint8_t { Corner = 0, Center = 1 };

struct InkingFieldGrid       { float cellWidth = 64.0f, cellDepth = 64.0f; };
struct InkingFieldCamera     { float x = 0.0f, d = 0.0f, zoom = 1.0f; };
struct InkingFieldPos        { float x = 0.0f, d = 0.0f, y = 0.0f; };
struct InkingFieldCell       { std::int32_t i = 0, j = 0; };
struct InkingFieldCellRange  { std::int32_t i0, i1, j0, j1; bool valid; };
struct InkingDesignPos       { float x = 0.0f, y = 0.0f; };

struct InkingFieldProjection {
    InkingFieldMode  mode = InkingFieldMode::Grid;
    InkingFieldGrid  grid{};
    InkingFieldCamera camera{};
    float skew = 0.0f;              // 度
    float depthScaleFar = 1.0f;     // 1 = 无透视
    float farD = 640.0f;            // 只在 depthScaleFar < 1 时有意义
    float designWidth = 1280.0f;
    float designHeight = 720.0f;
    std::int32_t cols = 0, rows = 0;  // 倾斜模式的规划格数；0 = 无限（仅格子模式合法）
};

InkingDesignPos InkingFieldProject(const InkingFieldProjection&, InkingFieldPos) noexcept;
bool  InkingFieldGroundHit(const InkingFieldProjection&, InkingDesignPos, InkingFieldPos* out) noexcept;
InkingFieldCell InkingFieldCellOf(const InkingFieldProjection&, InkingFieldPos) noexcept;
InkingDesignPos InkingFieldCellCenter(const InkingFieldProjection&, InkingFieldCell) noexcept;
InkingDesignPos InkingFieldCellQuad(const InkingFieldProjection&, InkingFieldCell, InkingDesignPos out[4]) noexcept;
InkingFieldCellRange InkingFieldVisibleCells(const InkingFieldProjection&) noexcept;
float InkingFieldDepthLimitForPixel(const InkingFieldProjection&) noexcept;  // ld_1px
bool  InkingFieldValidate(const InkingFieldProjection&, const char** why) noexcept;
```

**验收**

| 断言 | 门槛 | 为什么 |
| - | - | - |
| 12 个固定世界坐标的屏幕坐标 | ±0.5 px | 期望值用**独立实现**（Python 按 §4.1 公式另写一份）算出来，不从 C++ 抄 |
| 纵线直线性（`skew > 0`） | 最大偏离 **< 0.01 px** | `s` 是 Möbius 形式 ⇒ 严格共线，浮点噪声级 |
| 格子等大（`depthScaleFar = 1`） | 任意两格边长差 < 1e-4 | 平行投影的判据 |
| 反解往返 | 格子模式 < 1e-3 px、倾斜模式 < 0.5 px | 闭式反解，不是逼近 |
| 改 `cols/rows` 不动已算出的坐标 | 完全相等 | 地图尺寸不是投影参数 |
| `depthScaleFar = 1` 时 `DepthLimitForPixel` 返回无穷 | 是 | "无限格子"的数学依据 |
| `skew = 0 & depthScaleFar = 1` ⇒ 1 单位 = 1 像素 | 1e-6 | 美术基准 |

**为什么这一批单独可发**：它零依赖、零 SDL，引擎的 core 进程（headless）只需要它就能做"点击 → 格坐标"。T-02 不给它也能用。

### 5.2 第二批 1.2-b：场地组件 + 命中（T-02 / T-03）

**落点**：`include/field/InkingField.h`（新）+ `src/field/InkingField.cpp`（新）

```cpp
class InkingField : public InkingWidget {   // 实际基类按库内惯例选，须能进场景树
public:
    void SetProjection(const InkingFieldProjection&);
    const InkingFieldProjection& Projection() const noexcept;

    // 只读视图：调用方保证内存在下次 SetObjects 之前有效（命中返回的指针指向它）
    void SetObjects(const InkingFieldObject* items, std::size_t count);
    void SetTiles(const InkingFieldTile* tiles, std::size_t count);

    void SetSkyColor(std::uint32_t argb);
    void SetGroundColor(std::uint32_t argb);

    bool HitObject(float designX, float designY, const InkingFieldObject** out) const;
    bool HitCell  (float designX, float designY, InkingFieldCell* out) const;

    // 观察口（都要非零才正常，见 §7）
    struct Stats {
        std::uint32_t objectCount, tileCount;
        std::uint32_t terrainBatches, objectBatches;
        std::uint32_t rejectedBatches;   // SetGeometry 超限被拒次数
        std::uint32_t culledSubPixel;    // 1px 规则剔掉的物体数
        std::uint32_t truncatedTiles;    // 层池满被截断的瓦片数
    };
    Stats GetStats() const noexcept;

protected:
    virtual void OnFieldClick(const InkingFieldObject&, InkingFieldCell, InkingDesignPos) {}
};
```

**提交形态**

- 地形面：`SetGeometry`（自由顶点，4 顶点/格），按 4096 四边形分批，层池上限 8。
- 物体面：`SetQuads`（矩形 + 旋转），自动切批。
- 天空/地面底色：地形面的第一批（天空块 + 地面块）。**格子模式没有天空**，`SetSkyColor` 在那里无效（登记）。

**验收**

| 断言 | 门槛 |
| - | - |
| 5000 个物体的增量帧时间 | **≤ 2.0 ms**（参考：scene25d 流水线 0.74 ms / 图层提交 0.94 ms） |
| 5000 个物体 → `objectBatches` | ≤ 4 |
| 地形面 5000 格 | `rejectedBatches == 0`（层池够用），`truncatedTiles == 0` |
| 命中与绘制一致 | 无重叠时逐物体取包围矩形内一点，命中必须返回该物体；扫描 100 个采样点全中 |
| UI 优先级 | 场地铺满全屏时，UI 按钮仍可点（场地不进命中表） |
| `OnFieldClick` 拿到的是调用方内存 | `reinterpret_cast<MyEnemy*>` 后 `hp` 与调用方一致 |
| 1px 剔除 | 远端剔除数 > 0 且这些物体 `HitObject` 返回 false |
| 场地挂到有平移的父节点 | **不允许**（`SetField` 时断言父节点无变换；违反则 WARN + 不生效） |

### 5.3 第三批 1.2-c：自检与文档（T-04 / T-05）

- 自检函数：`CheckFieldProjection()` / `CheckFieldSubmit()` / `CheckFieldHit()`，条数写进 `selftest` 汇总。
- 静默失效三处同步（`kSilentFailures` + `AGENTS.md` §7 + `NoteSilentFailure`），新增 4 条见 §7。
- `docs/zh/api/InkingField.md` 新页 + `docs/zh/API.md` 索引 + `README` 的文件数。
- `ctest` 增一条：`field_pure_headers_have_no_sdl`（`InkingFieldProjection.h` 不许出现 `SDL`）。

---

## 6. 批次门槛与进度

| 批次 | 发布前必须满足 |
| - | - |
| 1.2-a | 12 点基准 + 直线性 + 等大 + 反解往返全绿；`inking_strict` 0 warning；新头不含 SDL |
| 1.2-b | 5000 物体 ≤ 2.0 ms；`objectBatches ≤ 4`；命中/绘制一致；UI 优先级断言绿 |
| 1.2-c | 静默失效元自检绿；`AGENTS.md` §7 与代码一致；文档页与 API 索引齐 |

| 编号 | 任务 | 批次 | 状态 |
| - | - | - | - |
| T-01 | 投影与格子纯函数层 | 1.2-a | 完成（`include/field/InkingFieldProjection.h` + `.cpp`） |
| T-02 | 场地组件：地形面 + 物体面 | 1.2-b | 完成（`include/field/InkingField.h` + `.cpp`、`InkingScene::SetField`） |
| T-03 | 命中与点击回调 | 1.2-b | 完成（屏幕矩形逆序扫 + 闭式反解；`ClickAt` / `OnFieldClick`） |
| T-04 | 自检 + 静默失效三处同步 | 1.2-c | 完成（`CheckFieldProjection/Submit/Hit` + 21 条清单 + `field_pure_headers_have_no_sdl`） |
| T-05 | 文档页与 API 索引 | 1.2-c | 完成（`docs/zh/api/InkingField.md`、`API.md` §3/§8、CHANGELOG 1.2.0） |

---

## 7. 静默失效清单（新增 4 条，三处同步）

| 编号 | 症状 | 原因 | 怎么发现 |
| - | - | - | - |
| `field_follow_perspective_inert` | 格子模式里 `followPerspective` 设了没反应 | 那里 `depthScaleFar` 锁 1 ⇒ `s ≡ 1` | 自检断言"格子模式下改它，画面逐像素不变" |
| `field_parent_transform_drifts` | 场地整体错位 | 提交坐标是绝对设计坐标，祖先仿射会叠加 | `SetField` 时检查父链无变换；自检里造一个带平移的父节点，断言 WARN |
| `field_terrain_batch_rejected` | 地形面整块消失、停在上一次内容 | `SetGeometry` 超 16384 顶点 ⇒ 整批拒绝 | `Stats::rejectedBatches` 必须为 0 |
| `field_subpixel_culled` | 远处少画物体/格子，无任何提示 | 1px 规则主动剔除 | `Stats::culledSubPixel`；截断另有 `truncatedTiles` |

另：`SetSkyColor` 在格子模式无效（没有天空区域）——并入 `field_follow_perspective_inert` 同一类"参数在当前模式无意义"，文档里合并说明。

---

## 8. 已否决（别翻案）

| 方案 | 否决理由 |
| - | - |
| 透视作为基础模型（`1/k` + pitch 混合 + `lateralConverge`） | 只有 `1/(1+h·ld)` 保直线；混合两套参数化会让纵线变弯（scene25d 实测 21.36 px） |
| 中心单点透视（`skew = 0` 且开透视） | 所有深度线扎向屏幕正中，被明确否决；倾斜后消失点偏到一侧 |
| 把场地的世界坐标叫"设计坐标" | 库里"设计坐标"已指 UI 布局空间（`InkingSetDesignSize` / LETTERBOX），撞名 ⇒ "位置差了一截" |
| 用 `InkingStaticCanvas` 画游戏内容 | 画布是"记录命令表 → 脏了重放"，5000 图元 8.77 ms；图层 0.94 ms |
| 命中时重新投影一遍 | 两份算式必然分叉，表现是"看着点中了却打偏" |
| 两套 `s`（宽/高各一个开关） | 会产生"远处变瘦高"的组合；统一一个 `s`，跟随与否全有或全无 |
| 独立的深度压缩参数 `m` | 矩形格子已经提供"压扁"自由度；两个旋钮同自由度必然打架 |
| 公开 `ZIndexOf` 整数编码 | 量化边界（d 分辨率、y 串键）会变成永久契约；排序是内部实现 |
| 瓦片作为可命中图元 | 地面命中由闭式反解完成，精确、O(1)、天然支持无限格；每格一条命中记录是纯开销 |
| 物体带虚函数 / 库承诺实体 | 跨 DLL 派生是长期负债；图片与帧动画在提交侧只差一个 `src` |
| 黑板 / 双进程 / 系统链进库 | `kLayoutVersion` 是项目的 ABI，不是库的 ABI；进库等于把内容焊进机制 |
| 现在做 shader / 换 SDL_GPU | 失去软件后端 ⇒ 像素级验证体系失效；收益 0.4 ms 对不上重写 render 层的成本 |

---

## 9. 复现与验收命令

```bash
G=/c/msys64/ucrt64/bin/g++
# 纯函数头必须能脱离 SDL 编译
$G -std=c++20 -fsyntax-only -Iinclude src/field/InkingFieldProjection.cpp
# 全量
cmake --build build && ctest --test-dir build
cd build && PATH=/d/InkingUIKit/build/bin:/c/msys64/ucrt64/bin:$PATH ./bin/inking_demo_selftest.exe
cmake --build build --target inking_strict
# 新增那条机械检查
ctest --test-dir build -R field_pure_headers_have_no_sdl
```

自检看末行"失败项：N"，**退出码就是 N**。

---

## 10. 复核补丁（2026-11-05 第二轮，七条）

首轮交付后按"覆盖缺口 / 疑点 / 文档"过了一遍，七条全部落地。**三条不是补测试，
而是改了断言所暴露的真问题**。

| # | 条目 | 结论 | 落点 |
| - | - | - | --- |
| ① | 重叠时取最上面（主键 `d`） | 新增：两物体纵叠 44 px，(772,578) 必须取近的；另加一条"不重叠处仍取远的"防反得过火 | `CheckFieldHit` |
| ② | `rotation` 参与命中 | 新增：90° 两条 + **±30° 一条**——±90° 对 64×32 是同一个包围盒，符号反了看不出来 | `CheckFieldHit` |
| ③ | `followPerspective = false` | 新增；**原文表格里 B 的 x 算错了**：`sx` 的 `(x−camera.x)` 同样乘 `zoom·s`，x=200 在 d=640/s=0.5 处落在 **740** 而不是 840 | `CheckFieldHit` |
| ④ | 地形分批真的切过 | 新增两组：8×8 格 ⇒ 14400 格 / 4 批；5×5 格 ⇒ 36864 格 / 8 批 + 截 4096。**`VisibleCells` 上界改为 `ceil−1`**（半开区间），否则格数不可预判 | `CheckFieldSubmit` + `InkingFieldProjection.cpp` |
| ⑤ | 背景板 vs 场地的层序 | 疑点不成立：`SetBackdrop` 是**窗口层绘制 pass**（`drawFrame`：`paintBackdrop` → `RenderScene`），不是兄弟节点。新增断言钉场景内那一半：场地是绘制列表**第一项** | `CheckFieldHit` |
| ⑥ | `inking.h` 注释停在 1.0.0 | 改成与 1.2 / 2.0 计划一致（冻结推迟到 2.0），拆开版本号与阶段的说明 | `include/inking.h` |
| ⑦ | 两个 "field" 两个意思 | `docs/AGENTS.md` §7 加对照表；`check-field-pure-headers.cmake` 头注释同步 | 文档 |

**统计口径**（复核提的两条）：

- `Stats::rejectedBatches` → **`rejectedBatchesTotal`**（累计，名字带 `Total`）；
- 其余字段统一为**上一帧**的账；`culledSubPixel` 改为"上一帧重建投影时剔掉几个"，
  没重建的帧是 **0**（之前是"上次重建以来"，会被当成逐帧量误读）。

**顺带抓到的两个假绿**（都已修 + 加了守卫）：

1. `followPerspective` 那条"逐像素相同"在**活动场景被临时换走**时会渲染空场景，
   两帧当然相同——补了"这一帧真的画了东西（>1000 像素非黑）"的前置断言；
2. 改物体字段后忘了重新 `SetObjects`，测的还是旧缓存（`+30°` 那条第一版就是
   这么红的）。这条已写进 `InkingField.h` 的契约注释：**只读视图，改内容要重设**。
