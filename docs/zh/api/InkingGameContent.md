# 游戏内容侧：批量提交 / 遮罩 / 九宫格（1.1.0）

> 覆盖的头文件：`asset/InkingTexture.h`、`image/InkingGeometryLayer.h`、
> `image/InkingMask.h`、`dataStruct/InkingNinePatch.h`，
> 以及 `image/ImageLike.h` 上的 `SetEdgeFeather`。
>
> 需求来源：2.5D 场景系统（`D:/GoldingCore/probe-perspective/`）。
> 任务书与逐条核实：[`../plans/2026-11-04-dev010-game-engine-tasks.md`](../plans/2026-11-04-dev010-game-engine-tasks.md)。

---

## 什么时候用它

`InkingStaticImage` / `InkingSprite` 是**一个组件 = 一个图元**。这在 UI 上是对的：
每个组件有自己的锚点、命中记录、绘制列表项。但一帧画几千个精灵时，这些记账
全部变成固定成本。

实测（软件渲染器，1920×1080，5000 个 32×32 图元）：

| 做法 | 每帧 | 场景节点 | 命中表记录 |
| --- | --- | --- | --- |
| 5000 个 `InkingStaticImage` | 4.1 ms | 5000 | 5000 |
| 同上 + `SetEdgeFeather(false)` | 1.4 ms | 5000 | 5000 |
| **1 个 `InkingGeometryLayer` + 5000 个 quad** | **0.94 ms** | **1** | **0** |

**选型判据**：这个图元需要独立的命中、布局、动画状态吗？
需要就继续用组件；只是一张会动的图，用图层。

---

## 一、纹理句柄 `asset/InkingTexture.h`

```cpp
const InkingTextureHandle walk = InkingLoadTexture("walk.png");   // 相对资源根
if (walk.Valid()) {
    int w = 0, h = 0;
    InkingTextureSize(walk, &w, &h);
}
// ...
InkingReleaseTexture(walk);   // 不再用了一定要放
```

四条语义，都是刻意的：

| 语义 | 为什么 |
| --- | --- |
| 同一路径重复 Load 返回**同一个 id**，只解码一次 | 上千个精灵引同一张图，不该解上千次 |
| 每次 Load 是**一份持有**，Release 减一次，减到 0 才失效 | 两个系统各持一份，先放的那个不该废掉另一个 |
| 句柄钉住的纹理**不参与字节预算淘汰** | 契约是"跨帧持有"，被 LRU 收走就成野指针了 |
| 句柄是**整数 id**，不是指针 | 公开头不拖 SDL；查不到时画不出来而不是崩 |

**不 Release 就一直占内存**——这是"跨帧持有"的代价，不是泄漏判定问题。
换渲染器 / 退出前调 `InkingTextureReleaseAll()`。

不支持 SVG：SVG 要按目标尺寸光栅化，"一张固定纹理"对它不成立，Load 返回无效
句柄 + 一条 WARN。图片走组件那条路（`InkingStaticImage` 会按组件框烘）。

---

## 二、批量四边形 `image/InkingGeometryLayer.h`

```cpp
auto* layer = static_cast<InkingGeometryLayer*>(
    scene.AddChild(std::make_unique<InkingGeometryLayer>(&scene, "sprites")));

std::vector<InkingQuad> quads;
quads.push_back(InkingQuad{
    .texture = walk,
    .src = {},                              // 空 = 整张纹理
    .dst = {x, y, 64, 64},                  // 设计坐标，相对图层左上角
    .tint = 0xFFFFFFFFu,                    // 0xAARRGGBB，调制色
    .rotation = 0.0f,
});
layer->SetQuads(quads);                     // 整段替换
```

### 坐标

`dst` 是**设计坐标、相对图层左上角**。图层挂在 `(0,0)` 时它就等于世界坐标。
图层跟着父容器动（走祖先仿射），与别的组件一致。

### 顺序

层内 quad 的**提交顺序 = 绘制顺序**，不重排。图层在树里的位置决定它相对其他
节点的先后——所以"画的和点的不一致"那类 bug 不会从这里冒出来。

### 命中

图层**不进命中表**（构造期 `SetHitPassThrough(true)`）。几千个物体各占一条命中
记录正是这条要消掉的东西。要点击在业务侧判：

```cpp
if (InkingShapeContains(shape, localX, localY, w, h)) { /* 命中 */ }
```

`InkingShapeContains` 与渲染同源（判据 `f <= 0`），不会出现"看着在这儿、点不到"。

### 三个观测口

`GetQuadCount()` / `GetSkippedQuadCount()` / `GetQuads()`。第二个是"句柄失效被跳过
多少块"——一个坏句柄会让那一块静默消失，这个数是唯一能发现它的地方。

---

## 三、任意顶点 `SetGeometry`

```cpp
InkingVertex verts[3] = {
    {80, 20, 0.0f, 0.0f, 0xFFFFFFFFu},   // x, y, u, v, color
    {20, 80, 0.0f, 1.0f, 0xFFFFFFFFu},
    {80, 80, 1.0f, 1.0f, 0xFFFFFFFFu},
};
const std::uint16_t indices[3] = {0, 1, 2};
layer->SetGeometry(walk, verts, 3, indices, 3);
```

两个坐标口径，**混了就会画歪**：

- `x/y` 是**设计坐标、相对图层左上角**（与 `InkingQuad::dst` 同一套）；
- `u/v` 是 **0~1 归一化**（SDL 的 `tex_coord` 语义，**不是像素**）。

三条硬约束，违反的**整次拒绝**（不部分提交）：

| 约束 | 违反时 |
| --- | --- |
| 顶点 ≤ 16384 | 拒绝 + ERROR + `GetRejectedGeometryCount()` |
| 每个索引 `< vertexCount` | 同上 |
| 索引数是 3 的倍数 | 同上 |

截断一半的三角形列表会画出"少一块但看着像对的"画面，比整块不画难查得多。
与 `SetQuads` **双向互斥**：后设的赢。

---

## 四、形状遮罩 `image/InkingMask.h`

```cpp
InkingQuad source{.texture = walk, .dst = {40, 40, 100, 100}};
InkingShapeSpec circle;
circle.kind = InkingShapeKind::Circle;

layer->SetMaskedQuad(walk, source, circle, 100.0f, 100.0f, 16);  // density = 16
```

### 为什么必须先细分

一个 100×100 的方图只有 4 个顶点，**全在圆外**。如果只把 SDF 烘进这 4 个顶点
的 alpha，整块会变成完全透明——四个角都在外，插值出来也全是 0。

所以按网格把四边形切成 N×N 格，每个顶点算一次 `InkingSignedDistance`，
`alpha = clamp(0.5 - f, 0, 1)`（与 `fillShape` 的羽化带同一条公式），边缘因此有
约 1 像素过渡。

自检里有一条专门的反证：密度 1（就是那 4 个角）⇒ 可见顶点 **0 个**。

### 代价

| 密度 | 顶点 | 三角形 |
| --- | --- | --- |
| 16（默认） | 289 | 512 |
| 32 | 1089 | 2048 |
| 64（上限） | 4225 | 8192 |

比不遮罩贵两个数量级，且**每帧重建**。静态图元用它；动态的每帧重建遮罩网格
会很痛。

### 纯几何入口

`InkingBuildMaskedQuad(...)` 不碰渲染器，返回 `InkingMaskedQuad{vertices, indices,
clamped}`，可以直接单测。`SetMaskedQuad` 只是它的便捷封装。

---

## 五、九宫格与平铺 `dataStruct/InkingNinePatch.h`

纯 inline、零依赖（只靠 `InkingShapeBounds`），**不需要渲染器就能测**。

```cpp
InkingNinePatchBorder border{10, 10, 10, 10};      // 源图像素
layer->SetNinePatch(walk, quad, border);            // 展开成 9 个 quad
```

两条规则：

- **边框先夹源再夹目标**。顺序反了的反例：源 100 宽、目标 20 宽、边框 60/60 ——
  夹完目标剩 10/10，而源侧仍是 60，中间块变成负宽。负宽会画出翻转的图块。
- 边框之和超过目标时**等比缩**，不报错（`60+60` 超过 `100` 夹到 `50/50`）。

平铺是两段式（与遮罩同一口径）：

```cpp
const std::size_t need = InkingExpandTiled(dst, src, 1.0f, nullptr, 0);
std::vector<InkingNinePatchTile> tiles(need);
InkingExpandTiled(dst, src, 1.0f, tiles.data(), tiles.size());
```

边缘那一列 / 行不足一整块时，**源矩形等比缩小**（不是裁切）——"半块"显示的是
被压窄的整张图，而不是露出图的一部分。地形重复时两者视觉完全不同。

---

## 六、边缘羽化开关 `InkingImageLike::SetEdgeFeather`

```cpp
sprite.SetEdgeFeather(false);   // 默认 true = 与 1.0.0 逐像素相同
```

只对「矩形 + `fit: fill`」生效。关掉之后：

| | 开（默认） | 关 |
| --- | --- | --- |
| 32×32 的图元实际占 | **33×33**（羽化带把内圈内缩 0.5、外圈外扩 0.5） | **32×32** |
| 每个图元的三角形 | 12 | 2 |
| 5000 图元一帧 | 4.1 ms | **1.4 ms** |
| 边缘 | 抗锯齿 | 硬边（靠素材自己的 alpha） |

**UI 图片不要关**：纯色块 / 圆角面板关掉会有肉眼可见的硬边。游戏精灵的素材
自带 alpha 边缘，那圈带子除了让图元大 1 px 之外没有别的作用。

---

## 七、边界与坑

1. **`SetGeometry` 一次最多 16384 顶点**，要更多拆成多个图层。
2. **遮罩密度上限 64 格**，超了夹取并打一条 WARN（`mask_vertex_capped`）。
3. **九宫格与 `rotation` 同时给**时以旋转为准，边框会被拧歪（没做"先转再展开"）。
4. **句柄不 Release 就一直占内存**，且不参与字节预算淘汰。
5. **图层不进命中表**，点击要自己用 `InkingShapeContains` 判。
6. **`u/v` 是 0~1 不是像素**。写成像素的表现是"整块都是纹理的第一个像素"。
7. **像素断言别读单点**：精灵图中间可能本来就是透明像素，那种"没画出来"是素材
   问题。自检里踩过两次，改成整块统计才稳。

---

## 相关页面

- 提交层内部纪律：`render/InkingBatch.h`（三条：保持顺序 / 顶点与渲染器无关 /
  计数是一等公民）
- 形状与 SDF：`api/InkingShapeSpec.md`
- 观测口读数口径：`UsageReport.md` §3
