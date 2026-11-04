# 任务书：dev010 游戏引擎能力（R1 批量提交 / R2 九宫格）

> 状态：**待执行**（本文件按任务逐条跟踪，完成规则见 §1.2）  
> 日期：2026-11-04  
> 来源：`D:/GoldingCore/probe-perspective/REQUIREMENTS-InkingUIKit.md`（提出方：2.5D 场景系统）  
> 目标版本：`dev010`（对外版本号建议 **1.1.0**，1.0.0 未冻结 ABI 前发布时再定）  
> 涉及层：`asset/`（新）、`image/`（新）、`dataStruct/`（新）、`render/`（内部）、`scene/`、`examples/demo/selftest`

---

## 0. 结论（TL;DR）

需求书 **4 条缺口，2 条成立、1 条归因需修正、1 条不成立**；拆成 **8 个任务 / 4 批**。

| 编号 | 任务 | 批次 | 对应来源 | 落点 | 代价 |
| - | - | - | - | - | - |
| T-01 | 公开纹理句柄与跨帧持有 | B1 | R1（配套） | `asset/InkingTexture.h`（新） | 中 |
| T-02 | 批量四边形图层组件 | B2 | R1（主体） | `image/InkingGeometryLayer.h`（新） | 中 |
| T-03 | 任意顶点提交与索引校验 | B2 | R1（遮罩前提） | 同上 | 低 |
| T-04 | 九宫格与平铺展开（纯函数） | B3 | R2 | `dataStruct/InkingNinePatch.h`（新） | 低 |
| T-05 | 形状遮罩（细分 + SDF 烘 alpha） | B3 | R1（顺带） | `dataStruct/InkingMaskGeometry.h`（新） | 高 |
| T-06 | 带纹理路径的顶点重建计数 | B4 | 本轮发现 | `render/InkingBatch.h` / `.cpp` | 低 |
| T-07 | 混排顺序与批次边界自检 | B4 | R1 验收③ | `examples/demo/selftest.cpp` | 低 |
| T-08 | 静默失效清单三处同步 | B4 | 项目规矩 | `selftest.cpp` + `AGENTS.md` §7 | 低 |
| T-09 | 缓存纹理查表与嗅探结论 | B5 | §2.3（自测） | `render/InkingText.cpp` | 中 |
| T-10 | 矩形网格快路 | B5 | §2.3（自测） | `render/InkingDraw.cpp` | 中 |

两条设计取向：

1. **新能力一律做成"组件 + 纯几何函数"，不做全局状态 API**。需求书建议的 `SubmitQuads(...)` 自由函数与 `SetNinePatch(tex, l, t, r, b)` 都是状态式入口，与"提交顺序 = 树序"和"每笔参数制"两条既有纪律冲突，见 T-02 / T-04 的候选表。能落在纯几何层（可单测、不碰渲染器）的不落在提交层。
2. **先有能力、后补护栏**。T-06/T-07/T-08 排在最后：它们是"给新能力装观察口"，提前做会被新行为绕过。

---

## 1. 维护规则（与既有任务书一致，此处只写差异）

三段式、完成即压缩、DoD 四条沿用 `2026-11-04-sprite-group-and-animate-group.md` §1 那一套，不重复。本轮额外两条：

- **新公开头一律零 SDL 依赖**：`include/asset/`、`include/image/`、`include/dataStruct/` 下的新头不得 `#include <SDL3/SDL.h>`，句柄用整数，纹理指针只在 `.cpp` 里出现。
- **"进程唯一"的存储落 `.cpp`**：句柄注册表、细分上限计数都是 `AGENTS.md` §6 那条老规矩（头内 inline 静态量 ⇒ DLL/exe 各一份）。

---

## 2. 基线事实（核实结论）

### 2.1 逐条裁定

| 来源条目 | 判定 | 证据 |
| - | - | - |
| R1「没有公开的批量顶点提交，`InkingBatch` 在 `inking_detail`」 | **成立**（但"不可达"是错的） | `include/render/InkingBatch.h:33` 确在 `inking_detail`，注释 `:22` 自称"框架内部头"；然而 `CMakeLists.txt:533` 是 `install(DIRECTORY include/ ...)`，`dist/inking-1.0.0-preview-win64/include/render/` 里**确实躺着** `InkingBatch.h`。是"没承诺"而非"够不到" |
| R1「上千物体时节点开销压过渲染」 | **成立，但漏了真正的成本项** | 探针见 §2.2：5000 图元 7.59 ms/帧，**draw call 只有 3 条**；同规模纯色面板（不进纹理、不重建网格）7.10 ms ⇒ 成本在**每节点的固定开销**，不在 draw call |
| R1「无法提交 N 个精灵而不产生 N 个节点」 | **成立** | 每条路径都要一个 `InkingWidget`：`InkingStaticImage`/`InkingSprite` 都是组件类，`InkingScene::Render` 按 `drawList` 逐节点 `SubmitToRenderer`（`src/scene/InkingScene.cpp:1817` 起） |
| R1 验收「1000 四边形 draw call ≤ 10」 | **现状已满足**（不是新能力的收益） | 探针实测 1000 图元 `textureDraws == 1`。`fillShapeTextured` 的 `AppendTexturedGeometry` 早已按纹理合批（`src/render/InkingDraw.cpp` 正常路径）。**该验收指标不能用来证明 T-02 的价值**，改用帧时间 + 节点数 |
| R1「没有任意形状遮罩」 | **成立** | `CanvasContext` 全部裁剪入口只有 `PushClip(x,y,w,h)`（`include/canvas/CanvasContext.h:140`）；`grep stencil/scissor/ClipShape/SetMask` 无结果 |
| 「`InkingShapeContains` 公开、`AppendContour` 在 detail」 | **成立** | `include/dataStruct/InkingShapeSpec.h:826` 公开；`include/render/InkingMesh.h:71` 在 `inking_detail`（同 R1，物理可达、无承诺） |
| R2「`InkingImageFit` 只做整图缩放，拉伸会让边框变形」 | **成立** | `include/dataStruct/InkingImageFit.h` 三种模式 `Fill/Contain/Cover`，`Fill` 即整图拉伸；无九宫格 |
| R3 粒子容器 | **不成立（作为框架需求）** | 业务侧"数组 + 一次提交"由 T-02 直接覆盖，框架不必提供 |
| R4 shader | **不成立（SDL_Renderer 无此能力）** | `ImmediateDraw` 只给 `SDL_Renderer*`；需求书自己也判"不做"，两边一致 |
| §4「alpha 混合不是缺口」 | **认可** | `InkingDraw.h:88-93` 注释已记录实测 `0xFF7F0080`；本轮不复核 |

### 2.2 探针数字（`build/probe_1000_sprites.cpp`，可重跑）

软件渲染器 1920×1080、`walk.png` 32×32、每帧清屏 + `RenderScene`，预热 3 帧后计 60 帧均值：

| 规模 | 每帧耗时 | `textureDraws` | `batchSplits` |
| - | - | - | - |
| 1000 个图片图元 | **1.39 ms** | 1.00 | 0 |
| 3000 个图片图元 | **4.67 ms** | 2.00 | 1 |
| 5000 个图片图元 | **7.59 ms** | 3.00 | 2 |
| 5000 个纯色面板（对照） | **7.10 ms** | 0.00 | 2 |

三条读法：

1. **每节点 ≈ 1.4 µs**，线性、无拐点；5000 图元已吃掉 16.6 ms 预算的 46%。
2. **合批早就成立**：5000 图元只有 3 次 `SDL_RenderGeometry`，切批是撞到 `kMaxBatchVertices = 16384`（`src/render/InkingBatch.cpp:19`），不是按节点切。
3. ~~主因是节点层~~ **（2026-11-04 二次探针修正，见 §2.3）**：空组件 5000 个只要 **0.10 ms** —— 节点框架几乎免费，"每节点 1.4 µs" 全部花在 `onRender` 内部（键拼接、SVG 嗅探、网格生成），不在框架的遍历 / 锚点 / 提交槽上。所以 T-02 的收益**不是**"节点数变少"，而是"绕开那条重型路径"；"把节点数压到 1"本身只值 0.1 ms。

运行方式：

```
cd D:/InkingUIKit && g++ -std=c++20 -O2 -DINK_ISLOG=1 -Iinclude \
  -I/c/msys64/ucrt64/include build/probe_1000_sprites.cpp \
  -Lbuild/lib -linking -L/c/msys64/ucrt64/lib -lSDL3 -lSDL3_image -lSDL3_ttf \
  -lSDL3_mixer -o build/bin/probe_1000_sprites.exe
cd build/bin && ./probe_1000_sprites.exe 5000        # 图片
cd build/bin && ./probe_1000_sprites.exe 5000 panel  # 纯色对照
```

### 2.3 成本分项（`build/probe_node_cost.cpp`，5000 图元，软件渲染器）

§2.2 那条"主因是节点层"的读法是**错的**。把同一规模拆成六个模式重测：

| 模式 | 每帧 | 相对上一档 | 这一段在干什么 |
| - | - | - | - |
| `empty` | 0.10 ms | — | 节点框架：绘制列表遍历 / 锚点 / 渲染槽 |
| `quad` | 1.35 ms | **+1.25** | 每次 `ResolveTextureHandle`（两次哈希）+ 4 顶点逐点追加 |
| `filltex` | 4.04 ms | **+2.69** | `BuildShapeMesh` 每帧重建矩形网格（轮廓采样 + 羽化） |
| `sprite` | 7.5 ms | **+3.46** | `PaintImage` 的包装层：纹理键字符串拼接、SVG 嗅探、`SDL_GetTextureSize`、fit 计算 |
| `batch` | 0.85 ms | — | 1 节点、每帧重建 20000 顶点、一次提交（T-02 的动态上限） |
| `static` | 0.82 ms | — | 顶点只算一次，每帧只提交（静态上限） |

三档稳定读数（sprite 7.49 / 7.55 / 8.51 有 ±12% 抖动，其余 ±3%）。结论：

- **节点框架只占 1.3%**（0.10 / 7.5）。"5000 个节点太重"这个直觉是错的——重的是每个节点在
  `onRender` 里干的活。
- **`PaintImage` 包装层占 46%**，其中绝大部分是**每帧重复计算不变的东西**（键字符串、嗅探结论、
  纹理尺寸）。这一块是纯白干活，也是后面 T-09 的主要目标。
- **网格重建占 36%**：矩形 + Fill 的网格骨架是常量，现在每帧都从轮廓采样重算一遍。

**② `filltex` 那 2.69 ms 里，网格生成只占 0.36。** 再加一档 `fixed9`（预生成的 9 顶点网格直接提交，
跳过 `BuildShapeMesh`）：3.85 ms。与 `filltex` 的 4.21 只差 **0.36 ms**。

真正的差在**三角形数**：`quad` 是 4 顶点 / 2 三角形（1.39 ms），`fixed9` 是 9 顶点 / 12 三角形
（3.85 ms）。羽化带让每个矩形多出 10 个三角形，5000 个就是 5 万个——那 2.46 ms 是抗锯齿的代价，
不是"轮廓采样写得慢"。

**③ 去掉羽化带省 2.46 ms，但会改变画面。**（T-10 最终以 **开关** 形式落地：默认保持现状，游戏侧按需关掉。下面这段是当时的实测依据。） `build/probe_feather_diff.cpp` 同一张图、同一位置各画一次：

| | 结果 |
| - | - |
| 不同像素 | **228 / 40000（0.57%）** |
| 最大通道差 | **232**（几乎完全不同的颜色） |
| 差异范围 | x 85..114 / y 86..113 —— **整张图的内部**，不只是边缘一圈 |

原因：现状的羽化带是"内圈内缩 0.5 px、外圈外扩 0.5 px"，而 UV 跟着顶点外推 ⇒ **图片实际画成
33×33 而不是 32×32**。所以"去掉羽化"不是"边缘变硬"，是"图缩小 3%"，两者都不能算等价改动。

```bash
cd build/bin && for m in sprite filltex fixed9 quad batch static empty; do ./probe_node_cost.exe $m 5000; done
cd build/bin && ./probe_feather_diff.exe     # 羽化带的像素差异
```

**T-09 落地后复测**（同一探针、同口径）：1000 / 3000 / 5000 = **0.90 / 2.85 / 4.80 ms**（原 1.39 / 4.67 / 7.5）。
剩下的成本结构：`fixed9` 下限 3.85 里约 3.4 是三万多个**三角形**的固定开销（羽化带），1.4 是光栅化本身。

---

## 3. 第一批 B1：纹理句柄（dev010-a）

- T-01 公开纹理句柄与跨帧持有-已完成（2026-11-04，同路径两次 Load 得同一 id 且只解码一次（`loads +1 / cacheHits +1`）；引用计数减到 0 才失效；钉住时预算压到 1 字节连跳 10 帧 `textureEvictions` 增 **0**，放掉后 3 帧增 **1**（对照证明预算真在跑）；`CheckTextureHandle()` 9 条绿、`ctest` 8/8）

---

## 4. 第二批 B2：提交层（dev010-b）

- T-02 批量四边形图层组件-已完成（2026-11-04，`include/image/InkingGeometryLayer.h`：`InkingQuad`（句柄 / 源矩形 / 目标矩形 / tint / 旋转）+ `SetQuads`（整段替换）/`AppendQuads`/`ClearQuads`，构造期 `SetHitPassThrough(true)` ⇒ 图层不进命中表。**5000 quad 一帧 0.94 ms**（现状 `InkingStaticImage` 4.24，省 78%；手写提交上限 0.91）；每帧重设整批（40 KB 拷贝）只多 0.01 ms。`onRender` 里**按 id 段复用**句柄解析（1.17 → 0.94），连续同纹理并成一次 `SDL_RenderGeometry`。自检 `CheckGeometryLayer()` 7 条绿：1000 块 = 1 次 draw、只占 1 个节点、重叠区是后提交那块（红 3614 / 蓝 124）、失效句柄跳过 + 计数且不连累别块。`ctest` 8/8）

- T-03 任意顶点提交与索引校验-已完成（2026-11-04，`InkingVertex{x,y,u,v,color}`（设计坐标 + 0~1 归一化 UV）+ `SetGeometry`，三条硬约束**整次拒绝**：顶点 ≤ 16384（单批上限）、索引全部 `< count`、索引数是 3 的倍数；与 `SetQuads` 互斥（后设的赢）。提交前先 `FlushGeometry()`，保证这一块独占一批。自检 9 条绿：三角形落笔 500 像素且远区干净、越界索引拒绝 + 记账且保留上一次内容、16385 顶点被拒、双向互斥。`ctest` 8/8）

---

## 5. 第三批 B3：几何展开（dev010-c）

- T-04 九宫格与平铺展开-已完成（2026-11-04，`include/dataStruct/InkingNinePatch.h` 纯 inline、零依赖：`InkingExpandNinePatch(dst, src, border, out)` 输出 9 块 `{src, dst}`；边框**先夹源再夹目标**（顺序不能反，反了源侧不受约束），夹取等比缩、不产生负宽；`InkingExpandTiled` 两段式（nullptr 只算数），边缘半块的源矩形**等比缩小**而不是裁切。图层加 `SetNinePatch` 便捷入口。自检 8 条绿：面积和 10000.0 / 四角 10 中心 80 / 60+60 夹到 50-50 / 平铺 16 块且两段式一致、每块不越界。`ctest` 8/8）
- T-05 形状遮罩-已完成（2026-11-04，`include/image/InkingMask.h` 纯几何：按 density 细分四边形，逐顶点 `InkingSignedDistance` + `alpha = clamp(0.5-f,0,1)`（与 `fillShape` 羽化带同一公式），默认 16 格 = 289 顶点 / 1536 索引，上限 64 格（夹取 + 1 条 WARN）。图层加 `SetMaskedQuad` 便捷入口。自检 6 条绿：alpha 三档齐（0/76 满/193 中间/20）、**密度 1 反证 4 顶点全透明**（这就是必须细分的原因）、圆裁方图后四角干净且圆内落笔、密度 1000 夹取成功且只 1 条 WARN。`ctest` 8/8）

---

## 6. 第四批 B4：护栏（dev010-d）

- T-06 带纹理路径的顶点重建计数-已完成（2026-11-04，`AppendTexturedGeometry` 里补 `++vertexRebuilds`——这条路**没有顶点缓存**，每帧都重算，原来观察口读出来是 0。不记 `vertexReuses`（没有复用可言，记了等于撒谎）。自检断言「1000 块 ⇒ +1000」，实测正好 1000。`ctest` 8/8）

- T-07 混排顺序与批次边界自检-已完成（2026-11-04，`CheckMixedOrder()`：图片节点 + 图层 quad（tint 只留红）+ 标签文字三种提交叠在一起，跑两遍——先量「没有图层」的右半边，再设图层量第二次，判据是「两次不同」（45 个像素变了）。**不能用「红色更深」那种判据**：tint 是调制，结果取决于纹理本身的颜色，第一版写死方向实测 5045 vs 4731 直接翻车。另断言 `batchFlushes <= 4`（实测 +2）。`ctest` 8/8）

- T-08 静默失效清单三处同步-已完成（2026-11-04，清单 13 → 17 条：`texture_handle_stale` / `geometry_index_overflow` / `mask_vertex_capped` / `ninepatch_border_clamped`，各自在对应自检末尾登记一次，`docs/AGENTS.md` §7 同步。**踩到一次**：登记点原本放在「资源读不到就早退」的分支之后，而 ctest 的工作目录不是 `build/bin` ⇒ `fopen("assets/...")` 读不到 ⇒ 整个函数提前 return ⇒ 元自检报「这条自检没跑」。改走 `InkingAssetLoad`（资源根）才稳）

---

## 7. 第五批 B5：去白干活（dev010-e）

> 这一批**不是**需求书要的，是 §2.3 分项测出来的。它只**去重复计算**、不改任何绘制结果，但走的是
> 所有现有组件的路径，所以单独一批、带逐像素比对。
>
> **它不依赖 B1~B4，可以先做。** 做完之后现有的 `InkingStaticImage` 每帧成本就落到 T-02 的同一量级
> （7.5 → 3 ms），T-02 剩下的价值就只剩"不建节点"和"任意顶点/遮罩"这两条结构性收益。

- T-09 缓存纹理查表与嗅探结论-已完成（2026-11-04，三处去白干活：位图签名快筛（PNG/JPEG/BMP/GIF/WebP 前 4~8 字节排掉 `SDL_IOStream` + `IMG_isSVG`）、纹理键走复用缓冲 + `to_chars`（稳态零分配）、尺寸读 `TextureEntry::width/height` 不再每帧问 SDL。**5000 图元 7.5 → 4.8 ms**（1000：1.39 → 0.90；3000：4.67 → 2.85），`ctest` 8/8。自检 `CheckImageFastPath()`：位图连画 10 帧新建纹理 0 / 命中 10、SVG 仍走光栅化 +1）

- T-10 矩形无羽化快路（改开关式）-已完成（2026-11-04，`fillShapeTextured` 加 `bool edgeFeather = true`；公开面是 `InkingImageLike::SetEdgeFeather(bool)`（`InkingSprite` / `InkingStaticImage` 都能用）。只对「矩形 + `fit: fill`」生效：走 4 顶点 / 2 三角形直提交，**不再外扩 0.5 px**（32×32 就占 32×32，相邻图元不再互相压 1 px）。**5000 图元 4.1 → 1.4 ms**（省 65%）；图层那条路仍是 0.89。默认 **true** ⇒ 现存 UI 逐像素不变。自检 `CheckEdgeFeather()` 4 条绿：默认开 / 回读为关 / 关掉后 228 个像素变了（与探针 `probe_feather_diff` 同一数字，交叉验证）/ 再开回来与第一次逐像素相同（可逆、无残留）。`ctest` 8/8）

---

## 8. 批次门槛与进度

### 8.1 门槛

| 批次 | 发布前必须满足 |
| - | - |
| dev010-a | `CheckTextureHandle()` 4 条绿；`ctest` 8 条全绿；`dist/` 里新头**不含 SDL** |
| dev010-b | `CheckGeometryLayer()` 全绿；1000 quad 一帧 `textureDraws == 1`；5000 图元帧时间 **≤ 3 ms**（探针同口径） |
| dev010-c | `CheckNinePatch()` / `CheckMaskedQuad()` 全绿；遮罩四边形顶点数 ≤ 上限 |
| dev010-d | 静默失效元自检绿；`inking_strict` 0 warning；`docs/zh/API.md` 增补三个新头 |
| dev010-e | 5000 图元（关羽化）≤ 1.6 ms、图层 ≤ 1.0 ms；开关默认开时 PNG 与改动前逐像素相同；再开回来也要逐像素相同 |

### 8.2 进度表

| 编号 | 任务 | 批次 | 状态 |
| - | - | - | - |
| T-01 | 公开纹理句柄与跨帧持有 | B1 | 已完成 |
| T-02 | 批量四边形图层组件 | B2 | 已完成 |
| T-03 | 任意顶点提交与索引校验 | B2 | 已完成 |
| T-04 | 九宫格与平铺展开 | B3 | 已完成 |
| T-05 | 形状遮罩 | B3 | 已完成 |
| T-06 | 带纹理路径的顶点重建计数 | B4 | 已完成 |
| T-07 | 混排顺序与批次边界自检 | B4 | 已完成 |
| T-08 | 静默失效清单三处同步 | B4 | 已完成 |
| T-09 | 缓存纹理查表与嗅探结论 | B5 | 已完成 |
| T-10 | 矩形无羽化快路（开关式） | B5 | 已完成 |

完成任务后，本节状态改 `已完成`，正文对应段落按 §1.2 压缩成一行——两处必须同时改。

---

## 9. 明确不做（避免反复讨论）

| 不做的事 | 原因 |
| - | - |
| 全局 `SubmitQuads()` 自由函数 | 提交必须在 `BeginFrame`/`EndFrame` 之间且顺序由树序决定（`InkingBatch.h:14-16`、`InkingScene.cpp:1817`）。自由函数把"何时调"交给调用方，错一帧就静默丢弃，错顺序就"画的和点的不一致"。T-02 的图层组件**已经**提供了同样的批量能力，且顺序天然正确 |
| `SetNinePatch(tex, l, t, r, b)` 状态式 API | 纹理属性变全局状态，同一张纹理两处要不同边框就串味；`InkingQuad` 带 border 字段是纯数据，可单测 |
| 把 `InkingBatch` 整体转公开 | 公开头会拖进 `SDL_Vertex`；且 `inking_detail` 是"可改不承诺"的边界，翻案要动命名空间纪律 |
| 非凸轮廓与四边形三角化求交（真·任意遮罩） | 工作量约等于重写 `InkingMesh.cpp` 的三角化；T-05 的细分 + SDF 覆盖圆/圆角矩形/胶囊/多边形（凸）这些实际用例 |
| 粒子系统 `InkingParticleLayer` | T-02 落地后业务侧一个数组 + 一次 `SetQuads` 就够（需求书自己也这么判） |
| 换 `SDL_GPU` 支持 shader | 架构级改动，需求书自己判"不做"；1.x 不动 |
| 复核 §4 的 alpha 结论 | 既有注释 `InkingDraw.h:88-93` 已有像素级实测，重测一遍没有新信息 |
