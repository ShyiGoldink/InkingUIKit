# 场地层：投影 / 格子 / 物体（1.2）

> 覆盖的头文件：`field/InkingFieldProjection.h`（纯函数，零 SDL）、
> `field/InkingField.h`（组件），以及 `scene/InkingScene.h` 上新增的
> `SetField` / `GetField`。
>
> 需求与逐条定稿：[`../plans/2026-11-05-field-layer-tasks.md`](../plans/2026-11-05-field-layer-tasks.md)。
> 那一份的术语表（§3）**先读**——`board` / `ground` / `world` 三个词都已被占用，
> 本页只用定稿后的那套名字。

---

## 什么时候用它

要画一块**带深度的地面**，上面站着图元，并且"点哪儿是哪一格"要说清楚。
典型场景：2.5D 战棋、塔防、经营、棋盘式的解谜。

不要用它做：纯 UI（那是现有组件）、真正的 3D（这里是**正俯视 + 斜投影**，
不是透视相机）、实体/系统链/黑板（**那些一律不进库**——这是组件库与引擎库的
边界，不是遗漏）。

---

## 一、投影：正俯视 + 斜投影

```
ld = d − camera.d
s  = 1 / (1 + h·ld)          h = (1/depthScaleFar − 1) / farD
sx = cx + (x − camera.x + sin(skew)·ld) · zoom · s
sy = H  − cos(skew)·ld · zoom · s − y · zoom · s
```

`cx = designWidth/2`、`H = designHeight`（地面在屏幕底边）、`y` 是离地高度。

**只有 `s = 1/(1+h·ld)` 这种形式能让世界直线保持是直线。** 它是 Möbius 变换，
把直线映成直线；线性衰减或任意曲线都会让纵线变弯（scene25d 那条 21.36 px 的
曲线缺陷就是这么来的）。所以这不是调参口味，是**正确性条件**——自检里有一条
"纵线最大偏离 < 0.01 px"专门钉它。

两个参数正交，别合并：

| 参数 | 作用 | 备注 |
| --- | --- | --- |
| `skew` | 深度线偏离垂直的角度（度） | 0 = 正俯视；有效区间 `[0, 80]`（再大 `cos` 趋 0，地面反解无解） |
| `depthScaleFar` | 最远处相对近处的尺寸比 `(0, 1]` | 1 = 无透视。**它必然同时带来汇聚**——缩放与汇聚是同一个除法的两面 |

消失点落在 `cx + sin(skew)/h`，即**倾斜那一侧**。`skew = 0` 又开透视才会落到
屏幕正中，那是被明确否决的"单点透视"。

### 两种模式

| 模式 | `skew` | `depthScaleFar` | 地图 | 用途 |
| --- | --- | --- | --- | --- |
| `Mode::Grid` | 锁 0 | 锁 1 | 无限格 | 设计 / 美术基准视图；`zoom = 1` 时 **1 世界单位 = 1 像素** |
| `Mode::Oblique` | 自由 | 自由 | 有限（`cols × rows`） | 真正的"带纵深"的表现 |

格子模式锁死那两个参数是有意的：它们是斜投影的自由度，在正俯视里没有意义。
**设了不生效**，而且不报错——见 §7 的 `field_follow_perspective_inert`。

### 两种坐标，别混

| 词 | 含义 | 类型 |
| --- | --- | --- |
| **场地坐标** | 连续世界量 `(x, d, y)`，单位 = 像素 | `InkingFieldPos` |
| **格坐标** | 整数 `(i, j)`，`i ↔ x`、`j ↔ d` | `InkingFieldCell` |
| **设计坐标** | 库既有的 UI 布局空间（1280×720 那块画布） | `InkingDesignPos` |

"设计坐标"这个词在库里**早就指 UI 布局空间**了（`InkingSetDesignSize` /
LETTERBOX），所以场地自己那套连续量必须叫别的名字——名字撞了的症状是"位置差
了一截"，而两边各自看起来都对。

---

## 二、格子

```
格 (i, j) 覆盖 [i·cellWidth, (i+1)·cellWidth) × [j·cellDepth, (j+1)·cellDepth)
CellOf(pos)     : i = floor(x / cellWidth),  j = floor(d / cellDepth)
CellCenter(i,j) : ((i+0.5)·cellWidth, (j+0.5)·cellDepth)
```

- **必须 `floor`，不许 `trunc`**：无限格意味着负坐标是常态，`trunc(−0.5) = 0`
  会把左上那片区域全算进第 0 格——小场景测不出来。
- **格线由整数索引生成，不许从世界坐标反推**：反推的线在浮点边界上和命中差
  一格，是坐标类 bug 里最难查的一种。
- 一格投到屏幕上**不是矩形**（斜投影下是平行四边形），所以瓦片走自由顶点，
  不走 `InkingQuad`。

### 1px 判据

```
远端格子的屏幕边长 = min(cellWidth, cellDepth) · zoom · depthScaleFar
若 < 1px ⇒ InkingFieldDepthLimitForPixel() = (min(cellWidth,cellDepth)·zoom − 1) / h
```

平行投影下 `h = 0` ⇒ 这个界限是**无穷**。这就是"无限格子"能成立的数学依据：
格子永远不会小于 1px，不需要裁。亚像素继续画不是"看不出"，是**几何在抖**
（闪烁、摩尔纹），所以透视下必须裁，并且**要计数**。

### 可见格

`InkingFieldVisibleCells()` 一次回答"这一屏能看到哪几格"，三条边界一起作用：
视口上下沿反解出的深度区间、1px 截断、`cols/rows` 地图边界。返回的范围是
保守的（多算几格只是白画，少算就是漏洞）。

---

## 三、物体 `InkingFieldObject`

```cpp
InkingFieldObject enemy;
enemy.x = 3.5f * 64;  enemy.d = 2.0f * 64;   // 场地坐标（起点角）
enemy.width = 48; enemy.depth = 24; enemy.height = 64;
enemy.texture = InkingLoadTexture("slime.png");
enemy.lift = 0;                              // 脚底离地高度
field->SetObjects(&enemy, 1);
```

| 字段 | 语义 | 坑 |
| --- | --- | --- |
| `depth` | **不参与视觉**，只用于占位（影子 / 选中框 / 吸附） | 立着的东西在屏幕上是正矩形，不斜；"贴地且要斜"的是瓦片 |
| `lift` | 脚底离地多高 | 与 `height` 分开：混成一个，"跳高一点"会变成"变高" |
| `rotation` | 绕屏幕矩形中心转（度，顺时针为正） | 与 `InkingQuad` 同向 |
| `flipX` | **只翻 UV**，不翻几何 | 翻 `x` 会让锚点语义当场变掉，占位跑到锚点左边 |
| `followPerspective` | `false` ⇒ 尺寸与 `lift` 的 `s` 全按 1 | 格子模式里设了没反应 |
| `anchor` | `Corner`（起点角）/ `Center` | 起点角是**世界 `(min x, min d)`**，不是屏幕左下角 |

`lift` 与 `height` 都乘 `s`，与宽度一致——整个物体等比缩小。这是"远处跳得更高"
那个假象的唯一解法。

**注意 `followPerspective = false` 只免掉"尺寸"，不免掉"位置"**：`sx` 里的
`(x − camera.x)` 照样乘 `zoom·s`。所以 x = 200、d = 640、s = 0.5 的物体落在
屏幕 x = 740（不是 840）——纵深越大，所有位置都朝画布中心收，这是透视该有的
样子。关掉它只是"远处不变小"（血条 / 贴花），不是"不受透视影响"。

**改了物体字段必须重新 `SetObjects`**：库拿的是**只读视图**，不会替你发现数组
内容变了（那份数据每帧都可能变，逐帧比对哈希不值当）。只改结构体不重设 =
画面和命中都停在上一份。

**±90° 测不出逆旋转的符号**：64×32 的矩形转 +90° 和 −90° 得到的是**同一个**
包围盒。要验符号得用 30° 这类角度——同一个点对 +30 在框内、对 −30 在框外。

### 业务侧派生（库不承诺实体）

```cpp
struct MyEnemy {
    InkingFieldObject base;   // **必须是第一个成员**
    int hp;
};
static_assert(offsetof(MyEnemy, base) == 0);

field.SetObjects(enemies.data(), enemies.size());   // 跨距自动 = sizeof(MyEnemy)
```

`SetObjects` 是**只读视图**：库不拷你的数组，回调拿到的指针指向**你自己那份
内存**，`reinterpret_cast<MyEnemy*>` 拿回去是安全的（标准布局 + 首成员，标准
保证）。

为什么有个模板重载而不是让你自己转成 `InkingFieldObject*`：派生结构的跨距比
基类型大，"当成基类型数组来索引"会**逐元素错位**——第 1 个之后全读到别人的
字段里去，而且不崩、不报错。模板把 `sizeof(T)` 作为跨距传下去，三条约束
（标准布局 / 首成员叫 `base` / 偏移 0）在编译期断死。

---

## 四、层序与分批

```
背景板
  └ 地形面   天空底色 / 地面底色 / 瓦片      ← 自由顶点，固定最下
  └ 物体面   按 (d, lift, 提交序号) 排序     ← 矩形 + 旋转
  └ UI 树    既有控件，树序
```

- **场地是场景的固定槽位**：`scene.SetField(&field)`。最多一个，直接挂在场景
  下，层序由构造保证（z 压到最低），不靠"记得先 AddChild 它"。
- **背景板不在这张图里**：`SetBackdrop` 是**窗口层的绘制 pass**
  （`drawFrame` 里 `paintBackdrop` → `RenderScene`），根本不是场景里的兄弟节点，
  所以它永远在场地下、也永远不会被场地盖住。`SetField` 压 z 管的是**场景内**
  那一半（UI 树）。
- 地形面与物体面**分成两笔是接口逼出来的**：`SetQuads`（矩形 + 旋转）表达不了
  平行四边形，而 `SetGeometry` 与它互斥。
- 代价（知情）：地形面永远在物体面之下，两者不逐笔交错 ⇒ 贴地且要遮挡的东西
  （草、石头、树根）必须当**物体**。

排序键是 `d`（远 → 近）、`lift`（低 → 高）、提交序号。末键必须是提交序号：
否则同帧同输入的排序结果不可复现（"既会闪、又查不出来"）。同深度且屏幕重叠的
两个物体在跳跃过程中可能交换两次顺序、边缘闪一下——**这是知情的取舍**，
不加锁。

分批：每批 ≤ 4096 四边形（= 16384 顶点，提交层的单批上限），地形面层池 8 批。
超出截断并记账（`Stats::truncatedTiles`）。

---

## 五、命中与回调

三条路，都不用 zoom 逆变换：

| 目标 | 判据 | 成本 |
| --- | --- | --- |
| UI 控件 | 既有命中表 | 已有 |
| 场地物体 | **每帧投影时存下的屏幕矩形**，逆序扫（近的在前） | 零 |
| 地面 / 格子 | 闭式反解，一次除法 | 一次除法 |

- **场地不参与场景命中表的仲裁**：构造期 `SetHitPassThrough(true)`，`HitTest`
  恒 false。所以"UI 优先"天然成立，不需要把两边的 z 混成一个数（UI 的顺序是
  "树序 + 兄弟 z"，不是可加的数）。
- **命中绝不重新投影**：复用提交时那一份结果，否则两份算式迟早分叉，表现是
  "看着点中了却打偏"。
- 以下都**不可命中**：`hitEnabled = false`、`tint` 的 alpha 为 0、被 1px 规则
  剔除的（看不见的不该能点）。
- 地面点在**设计画布之外**、地平线之上、地图之外、1px 界之外 ⇒ `HitCell` 返回
  false。

```cpp
// 上层（UI）明确未命中之后：
field->ClickAt(designX, designY);       // 命中物体 ⇒ OnFieldClick / 回调
```

派生重写 `OnFieldClick` 或挂 `SetOnFieldClick` 都行，两个都会被调用。参数里的
`object` 指向**调用方那份内存**，`cell` 是它脚下那一格，`at` 是点击处。

---

## 六、观测口

```cpp
const InkingField::Stats stats = field->GetStats();
```

| 字段 | 口径 | 意义 |
| --- | --- | --- |
| `objectCount` / `tileCount` | 上一帧 | 调用方给了几个（不是画了几个） |
| `objectsDrawn` | 上一帧 | 真的提交了几个 |
| `visibleCells` | 上一帧 | 枚举到几格（含没瓦片的空地盘） |
| `terrainBatches` / `objectBatches` | 上一帧 | 切成几批 |
| `rejectedBatchesTotal` | **累计** | 地形批被拒的次数，正常恒 0（名字带 `Total` 是有意的） |
| `culledSubPixel` | 上一帧 | **投影缓存重建时**被 1px 规则剔掉几个；没重建的帧就是 0 |
| `truncatedTiles` | 上一帧 | 因层池满截掉的瓦片数 |
| `skyQuads` | 上一帧 | 天空贴图提交了几个四边形（0 = 没画贴图；`Cover` / `Stretch` 恒 0 或 1） |
| `transformBlocked` | 上一帧 | 1 = 祖先带变换，整块没画 |
| `projectionValid` | 当前 | 1 = 当前参数合法 |

**两种时间口径**：只有 `rejectedBatchesTotal` 是累计，其余都是"上一帧"。同一
结构体里放两种口径是刻意的——前者回答"出过事没有"，攒起来才有意义；后者回答
"这一帧画成什么样"，攒起来只会误导。`culledSubPixel` 尤其别当累计量看：物体没
动、相机没动的帧它是 0。

**一帧的量**：`terrainBatches` 的期望值可以直接算——可见格 ÷ 4096 向上取整
（设计 1280×720、格 8×8 ⇒ 14400 格 ⇒ 4 批）；超出层池 8 批的部分记进
`truncatedTiles`（36864 格 ⇒ 8 批 + 4096 块被截）。

**这一层的失效全是静默的**（远处少画、地形停在上一帧、瓦片被截），画面上只是
"少了点东西"，不看数就查不出来。

---

## 七、边界与坑

1. **场地必须直接挂在场景下**，而且父链上不能有变换。提交坐标是**绝对设计
   坐标**，祖先的仿射会在提交层再叠一次 ⇒ 整块错位，而且不报错。
   `SetField` 会拒（WARN + 不生效），场地自己每帧还会再查一次（有人事后把它
   挪走也能发现），发现就**整块不画**——画在错的地方比不画难查得多。
2. **格子模式下 `followPerspective` / `SetSkyColor` / `horizonRatio` 不生效**
   （`s ≡ 1`；地平线在屏幕外，没有天空区域）。天空贴图在那里设了也不画，但**会
   喊一条 WARN**（`field_sky_texture_inert`）——前两个不喊是历史，第三个不再沉默。
3. **物体没有纹理就不可见也不可点**（`texture.Valid()` 为假直接跳过）。
4. **`InkingField` 不进命中表**：它铺满全屏，进表会挡住所有 UI。
5. **投影参数不合法时保留上一次那份**（一条 WARN + `projectionValid = 0`），
   不会悄悄换成默认值——那会让画面莫名跳一下。
6. **改 `cols` / `rows` 不动已算出的坐标**：地图尺寸不是投影参数。

---

## 八、天空：地平线位置 + 贴图（1.5）

场景的两条基础属性：**天空占多少画面**（可调）与**天空贴什么图**（可换）。
两者都是纯标量与纯句柄——没有回调、没有动画曲线，因为提出方的代码生成器要把
它们逐个输出成 `constexpr`。

### 地平线位置 `horizonRatio`

```cpp
projection.horizonRatio = 0.30f;   // 天空占 30%，地平线在设计坐标 y = 216（H = 720）
projection.skyFit       = InkingSkyFit::Cover;   // 默认
```

| 取值 | 含义 |
| --- | --- |
| `-1`（缺省） | **不覆盖**，地平线仍由 `skew / zoom / depthScaleFar / farD` 算出（1.4 的行为，逐位不变） |
| `[0, 1)` | **覆盖**：库反解 `farD`，使 `horizonY == ratio × designHeight` |

反解（唯一一处）：

```
farD = (1/depthScaleFar − 1) · (1 − horizonRatio) · designHeight / (cos(skew) · zoom)
```

**为什么反解 `farD` 而不是 `zoom`**：`zoom` 是作者用的尺度（一格几像素），被地平线
位置顺手改掉会让整张地图缩放跳动；`farD` 的语义就是"多远缩到最远"，它才是"地平线
落在哪"的直接决定量。反解结果用 `InkingFieldEffectiveFarD()` 查得到，`Projection()`
里也是**如实反映**的那个值（编辑器要能显示"farD 被地平线位置定住了"）。

**语义：`h` 变大以后每一格都变矮，不只远处。** 唯一不动的是 `ld = 0` 那条底边
（`sy = H`、横向尺度 = `zoom`）。所以实现里**不许**写成"只裁掉地面带"——那会让
格子比例与灭点对不上，症状是"地面从天空里穿出来"，看起来像美术画错了。

`cd = 64`、`skew = 28°`、`zoom = 1.5`、`depthScaleFar = 0.9`、`H = 1080` 时：

| 天空 | 第 1 格 | 第 2 格 | 第 3 格 |
| --- | --- | --- | --- |
| 30% | 76.2 px | 62.2 px | 51.8 px |
| 50% | 73.3 px | 55.8 px | 43.9 px |

拒绝（`InkingFieldValidate` 返回 false + `why`，**不夹取**）：`ratio` 既不是 `-1` 也
不在 `[0, 1)`（含 `1.0` / `1.2` / `-0.5` / NaN）；`depthScaleFar >= 1`（无透视就没有
天空带）；反解出的 `farD <= 0`。

### 天空贴图 `SetSkyTexture`

```cpp
field->SetSkyTexture(handle);   // 无效句柄 = 回到 SetSkyColor 的纯色
const InkingShapeBounds band = field->GetSkyBand();   // (0, 0, designWidth, horizonY)
```

1. 天空带 = `(0, 0) … (designWidth, horizonY)`；贴图**只画在带内**（裁剪），不盖地面；
2. `skyFit` 三档：`Cover`（等比铺满、居中裁切，默认）/ `Stretch`（拉伸，会变形）/
   `Tile`（原始像素 1:1 从带左上角起铺，右/下边缘画半块）；
3. 有贴图就**不画纯色**（两者不混用：叠一层半透明底色会让颜色偏掉）；
4. `Grid` 模式 / 平行投影（`h <= 0`）下带高为 0 ⇒ 贴图被忽略并**打一条 WARN**
   （每个场地实例一次，见 `field_sky_texture_inert`）；
5. **句柄所有权在调用方**：库不加载、不释放、不缓存路径；动态天空 = 每帧
   `SetSkyTexture(handle)`，库不提供动画抽象、也不读时钟；
6. 句柄失效（换过渲染器 / 已 Release）⇒ 退回纯色（`staleSubmits` 记账），
   画面不会开一个洞；
7. `Tile` 单帧上限 4096 块（一批），超了发一条 WARN 再停——贴图配太小会到十万级。

---

## 相关页面

- [游戏内容侧](InkingGameContent.md) —— 批量提交 / 纹理句柄 / 遮罩（场地图的
  提交走的就是那一层）
- [InkingScene](InkingScene.md) —— 场景树、命中表、层序
- [`../plans/2026-11-05-field-layer-tasks.md`](../plans/2026-11-05-field-layer-tasks.md)
  —— 四轮商讨的定稿、被否决的方案、术语表
