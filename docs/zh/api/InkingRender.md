# 渲染层

**回到API目录：**
[API index](../API.md)

渲染层是**框架内部**：这一层的头文件要么含 SDL，要么含 `inking_detail::` 内部工具。
业务代码不需要 include 它们——组件实现里才会用到。放在公开 `include/` 下只是因为
`include/` 是唯一的头文件搜索根。

本页覆盖：

| 头文件 | 内容 | 含 SDL |
| --- | --- | --- |
| `dataStruct/InkingTextRequest.h` | `InkingTextRequest` / `InkingTextMetrics`（**纯数据，公开**） | 否 |
| `dataStruct/InkingTextLayout.h` | 换行 / 省略 / 对齐（**纯函数，可单测**） | 否 |
| `render/InkingMesh.h` | 形状 → 轮廓点 → 三角索引（**纯几何**） | 否 |
| `render/InkingDraw.h` | `fillShape` / `strokeShape` / `fillShapeTextured` + 渲染器槽 | 是 |
| `render/InkingBatch.h` | 一帧成批提交、顶点缓存、裁剪 | 是 |
| `render/InkingText.h` | 文字 / 图片纹理 + 缓存 | 是 |
| `render/InkingGlyphAtlas.h` | 字形图集（shelf packing） | 是 |

## 一、一笔绘制是怎么走完的

```
Scene::Render
  └─ for each DrawItem（z 升序）
       └─ InkingWidget::SubmitToRenderer(renderer, absX, absY)   // 设计坐标 → 设备像素
            ├─ inking_detail::SetCurrentNode(_vertexCache.get())        // 这个节点用哪个缓存槽
            ├─ currentRendererSlot() = renderer                  // onRender 里从槽里取
            └─ onRender(pixelX, pixelY)
                 ├─ inking_detail::fillShape(...)          → AppendContour → AppendEarClipIndices
                 │                                     → AppendLocalGeometry → SDL_RenderGeometry
                 ├─ inking_detail::fillShapeTextured(...)  → 同样网格 + UV → SDL_RenderGeometry(带纹理)
                 ├─ inking_detail::strokeShape(...)        → 两条轮廓之间的环带
                 └─ inking_detail::PaintText(...)          → 纹理表（缓存）→ SDL_RenderTextureRotated
  └─ inking_detail::BeginFrame / EndFrame（场景 Render 的首尾）
```

**为什么 `onRender` 只有坐标、没有渲染器**：不希望每个组件实现都被迫多接一个参数。
提交前把渲染器挂到"当前渲染器"槽上，`onRender` 期间从槽里取。

**槽位定义在 `.cpp` 里**（`src/render/InkingDraw.cpp`）：头内 inline 函数里的函数内静态量
在动态库下会 exe / DLL 各一份，而且**不报错**。所以 `currentRendererSlot()` /
`currentAncestorAffineSlot()` 只有声明在头里。

## 二、形状同源：渲染 / 命中 / AABB 一份定义

`InkingMesh.h` 是渲染层里**唯一不碰 SDL** 的那一半。它回答的和 `InkingShapeContains`
（[InkingShapeSpec.md](InkingShapeSpec.md)）是**同一个几何问题**：`AppendContour` 采出来的
轮廓必须与 `InkingShapeContains` 判的内外同源，否则就是"画的圆角、点的是直角"那种只在角落
露头的错。

```C++
inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr int kMinCircleSteps = 24;
inline constexpr int kMaxCircleSteps = 512;
inline constexpr int kMinArcSteps = 4;
inline constexpr int kMaxArcSteps = 64;

int  StepsForLength(float pixels, int minSteps, int maxSteps);
int  ContourSteps(const InkingShapeSpec& scaled, float width, float height);
void AppendContour(std::vector<OutlinePoint>& out, const InkingShapeSpec& shape, ...);
bool SameShape(const InkingShapeSpec& a, const InkingShapeSpec& b);
void AppendEarClipIndices(const std::vector<OutlinePoint>& ring, ...);
```

`StepsForLength` 的意思是"**边界长了就多切几段**"——分段数按周长像素算，
所以放大之后圆角不会变成多边形（这是高清屏下最容易被忽略的一处）。

`AppendEarClipIndices` 走 **ear clipping** 三角化，所以 `polygonPath` 的凹口不会被填实。

> `OutlinePoint` 就是 `{float x, y;}` 的轮廓点（相对组件左上角，设计坐标乘过倍率）。

## 三、提交层：一帧成批，顺序不变

`InkingBatch.h` 做两件事：**缓冲**（形状填充不再立刻 `SDL_RenderGeometry`，而是追加进
当前批次）+ **成批提交**（连续的同一种提交合成一次调用）。批分的种类是**纹理句柄**：
纯色是 `nullptr`、图片是它自己那张纹理；**纹理变了就刷上一批**（不重排、不跨笔合并），
所以顺序永远与调用顺序一致。

三条纪律（改这一层的人先读）：

1. **保持顺序**：只合并"连续"的同种提交，绝不重排、绝不交换节点先后——重排会造出
   "画的是一个、点的是另一个"那类只在特定叠放顺序下出现的 bug；
2. **顶点数据与渲染器无关**（`SDL_RenderGeometry` 吃 CPU 数组），所以顶点缓存不必跟着
   渲染器销毁（纹理才需要）；
3. **计数是一等公民**：`BatchStats` 是自检能断言的观察口——"功能全对、只是白干活"
   只有计数抓得住。

>**VertexCacheSlot**

```C++
struct VertexCacheSlot {
    std::vector<SDL_Vertex> vertices;
    std::vector<int> indices;
    InkingShapeSpec shape{};        // ↓ 这几项构成"这批顶点是按什么算出来的"
    int width = 0, height = 0;
    float magnification = 0.0f;
    InkingTransformSpec transform{};
    std::uint32_t color = 0u; // 颜色也算键（顶点里带逐顶点颜色）
    bool valid = false;
};
```

顶点存的是**本地设备坐标**（相对组件左上角），平移在追加时做——这样"节点挪个位置"
不会让缓存作废，只有形状 / 尺寸 / 倍率 / 变换 / 颜色变了才重算。

>**BatchStats**

```C++
struct BatchStats {
    std::uint64_t geometrySubmissions = 0;  // 真的调了几次 SDL_RenderGeometry
    std::uint64_t vertexRebuilds = 0;       // 重算顶点（轮廓采样 + 变换）
    std::uint64_t vertexReuses = 0;         // 复用顶点（只做平移）
    std::uint64_t textureSubmissions = 0;   // 绕开批次的直接绘制（文字 / contain 图片）
    std::uint64_t batchFlushes = 0;
    std::uint64_t batchSplits = 0;          // 因为顶点上限切过几次批
    std::uint64_t textureDraws = 0;         // 其中带纹理的提交次数（同纹理连续 N 笔只 +1）
};
```

都是**累计值**——自检取差值，别假设从 0 开始。

>**函数**

```C++
BatchStats BatchStatsSnapshot() noexcept;
void SetCurrentNode(VertexCacheSlot* slot) noexcept;
VertexCacheSlot* CurrentNode() noexcept;
void BeginFrame(SDL_Renderer* renderer) noexcept;
void EndFrame() noexcept;
void FlushGeometry() noexcept;
void AppendLocalGeometry(const SDL_Vertex* v, std::size_t vn,
                         const int* idx, std::size_t in,
                         float pixelX, float pixelY, bool reused);
void AppendTexturedGeometry(SDL_Texture* texture, const SDL_Vertex* v, std::size_t vn,
                            const int* idx, std::size_t in, float pixelX, float pixelY);
void ApplyAncestorAffineToVertices(SDL_Vertex* v, std::size_t n,
                                   float pixelX, float pixelY) noexcept;
void NoteImmediateDraw() noexcept;
void SetImageBatchingEnabled(bool enabled) noexcept;   // 矩形 + Fill 的图片并进批次（默认开）
bool ImageBatchingEnabled() noexcept;
void SetClipRect(SDL_Renderer* renderer, const InkingShapeBounds& deviceRect) noexcept;
void ClearClipRect(SDL_Renderer* renderer) noexcept;
```

`NoteImmediateDraw()` 是那条唯一的规矩：**任何直接调 SDL 绘制的地方，前面必须调它**。
漏了不会报错——只是那一笔被后面刷出去的批次盖住（踩过一次）。

**矩形 + `Fill` 的图片默认也走批次**（0.6.2）：50 个同图集 sprite 的一帧是
**1 次** `SDL_RenderGeometry`、0 次单笔绘制（关掉开关是 50 次）。
这块原来是条 `SDL_RenderTexture` 快路，理由是"几何批不吃纹理 alpha"——0.6.2 的探针
在 SDL 3.4.16 的 software / direct3d11 两个后端上证明**那条不成立了**（透明区、50%
半透明、放大采样都与单笔逐位相同）。代价是矩形图片现在也吃**形状网格的 1px 羽化**：
边缘那一圈从硬边变成抗锯齿，和矩形按钮一致；渐变图上表现为"采样点偏了半个纹素"
（实测最大通道差 3，纯色图逐像素相等）。

开关只影响 `rect + Fill` 这一种形态，关掉即回到 0.6.1 的行为（含那处既有缺陷：
快路不看 `transform`，旋转 / 缩放的矩形图片会画成没变换）。

裁剪两条纪律：**坐标系与顶点相同**（传设备像素）、**设置前必须先刷缓冲**
（否则先前攒下的顶点会在"别人的裁剪"下被刷出去，表现是"有的组件被剪、有的没被剪"，
而且和叠放顺序有关）。取整用**包含式**（floor 起点、ceil 终点）：宁可多画半像素，
也不要因为取整把内容削掉一条。

## 四、文字与图片

>**InkingTextRequest**（`dataStruct/InkingTextRequest.h`，纯数据）

```C++
struct InkingTextRequest {
    std::string text;              // UTF-8
    float fontSize = 16.0f;        // 设计坐标（SDL3_ttf 的 DPI 是 72，"点"= 像素）
    std::string fontPath;          // 空 = 机器默认字体（按候选表找）
    std::vector<std::string> fontPaths;  // 回退链：fontPath 之后按顺序试（0.3.0）
    std::uint32_t color = 0xFFFFFFFFu;
    float offsetX = 0.0f;          // 文字块左上角，组件本地设计坐标
    float offsetY = 0.0f;
    bool Empty() const noexcept;
};

struct InkingTextMetrics { float width = 0.0f; float height = 0.0f; };
```

`InkingMeasureText` **不看** `offsetX/Y`。拿不到字体时（没编 SDL3_ttf / 找不到字体 / 字号 ≤ 0）
返回**估算值**（按字号的方块：宽 = 字号 × 码点数，高 = 字号），与绘制的退化画法一致。
不依赖渲染器，可以单独调。

>**InkingMeasureText / PaintText**

```C++
InkingTextMetrics InkingMeasureText(const InkingTextRequest& request);

void PaintText(SDL_Renderer* renderer, const InkingTextRequest& request,
               int width, int height, float magnification,
               float pixelX, float pixelY, const InkingTransformSpec& transform);
```

`PaintText` 是文字**唯一的绘制入口**（所有组件都走它）。三件事值得记住：

- **按 `fontSize × magnification` 设备像素栅格化**（不然放大就糊），所以缓存键里带的是
  这个乘积；
- **颜色走纹理调制**：纹理烘成白字，画的时候 `SDL_SetTextureColorMod` 上色 ⇒
  同一句话换颜色**不用重烘纹理**（颜色不进缓存键）；
- **变换绕组件中心**，与形状同一套语义，所以文字跟着按钮一起转。

>**PaintImage / InkingImageBytes**

```C++
struct InkingImageBytes {
    const std::uint8_t* data = nullptr;   // 借用：指向生成代码里那个静态数组
    std::size_t size = 0;
    std::string name;                     // 配置里的路径（缓存键 + 日志）
    bool Empty() const noexcept;
};

bool PaintImage(SDL_Renderer* renderer, const InkingImageBytes& image,
                const InkingShapeSpec& shape, int width, int height,
                float magnification, float pixelX, float pixelY,
                const InkingTransformSpec& transform, InkingImageFit fit,
                std::uint32_t tintColor, const InkingShapeBounds& srcRect = {});
```

四个已定的决定：**首次绘制时解码**（进程启动什么都不做）、**按组件形状裁剪**（走形状层
同一条网格生成，所以圆角按钮配图片时四角一起被剪）、**适配模式**（`fill` / `contain` /
`cover`，几何在 `InkingImageFitSpec` 的纯函数里）、**变换同源**。
`tintColor` 只取 **alpha**（过渡时淡入淡出，图片本身不换色）。解码不出来返回 false，
调用方可以退回"按颜色填形状"。

`srcRect`（0.6.1 T-04）是**这一帧在纹理里的像素矩形**，全零 = 整张纹理（既有图片的默认，
行为逐像素不变）。它只决定"取哪块"，绘制矩形仍由 `fit` 决定；而且 `Contain` / `Cover`
的等比基准是**这块的宽高**，不是整张纹理——图集里的 40×40 一格不该被当成 4:1 的宽图。

>**SetImageSampling / GetImageSampling**（0.6.4 T-12）

```C++
enum class ImageSampling { Linear, Nearest };   // 默认 Linear
void SetImageSampling(ImageSampling mode) noexcept;
ImageSampling GetImageSampling() noexcept;
```

**进程级**开关：像素画（64×64 那类素材）放大后必须是硬边，`Linear` 会把它糊成渐变。
切换**立即生效**——遍历纹理表把已建好的**图片**纹理重设一遍（字形纹理不动，
不然一开最近邻所有字都变毛边）。

三条边界：

- **不支持混用**：同进程里"像素素材 + 高清图"只能二选一。逐图模式要并进纹理缓存键
  （否则同名换模式会命中旧纹理），留到真有人要时再做。
- **software 后端的几何批不认采样模式**：实测（`build/probe_sampling.cpp`，2 色棋盘
  放大）四个组合的颜色种数——software 下"并批 `Linear`"与"并批 `Nearest`"都是 **2 色**
  （`SDL_RenderGeometry` 在软件后端恒最近邻），而 direct3d11 下是 **12 色 / 2 色**。
  像素素材不受影响（它本来就要最近邻），受影响的只有"软件后端 + 高清图平滑缩放"。
- **切换只改纹理状态，不重建纹理**：`texturesCreated` 不会因为这个开关变化。

>**ProbeImageSize**

```C++
bool ProbeImageSize(const InkingImageBytes& image, int* width, int* height);
```

量一张图片的**固有像素尺寸**：不建纹理、不需要渲染器。只有一个用途——图集构造期
校验切格几何（`InkingSprite`，0.6.1 T-05）。数据层看不到图片多大，而"格数与图宽不符"
能通过 `InkingSpriteData::Validate()`，所以必须真的解一次。

代价按 `(名字, 字节数)` 缓存（与纹理表同一套键口径）：同屏 N 个 sprite 共用一张图集时
只解一次。解不出来（没接 SDL3_image / 字节坏了 / SVG）返回 false，调用方跳过校验。

>**ReleaseTextCache**

```C++
void ReleaseTextCache() noexcept;
```

放掉这一层持有的全部纹理与字体。**渲染器销毁之前必须调**：纹理属于渲染器，
渲染器一销毁句柄就成了野指针，那时再 `SDL_DestroyTexture` 是 use-after-free。
顺序永远是"先 `ReleaseTextCache`，再 `SDL_DestroyRenderer`"。幂等，可重复调，
同时会 `TTF_Quit`（与内部那次 `TTF_Init` 配对）。

>**SetTextRasterCap / GetTextRasterCap**（0.7.1 T-03）

```C++
bool SetTextRasterCap(float cap) noexcept;   // 夹在 [1, 8]，默认 3
float GetTextRasterCap() noexcept;
```

文字按**设备像素**烘（设计字号 × R），纹理面积按 `ratio²` 涨，所以一直夹在 3 倍。
问题在"设计 640 + 4K 屏"（R = 6）这种配置：文字按 3× 烘、屏幕上再放大 2× ⇒ 糊，
而**日志里一个字都没有**（静默夹取）。

两个动作拆开：

- **可配**：`SetTextRasterCap(6)` 之后按 6× 烘（代价：单字纹理面积 ×4）；
- **触顶发声**：夹取时 `TextCacheStats::rasterCapped` +1，并报**一条** WARN
  （只报一次；**每次调用 setter 都会重新武装**这次告警，所以自检能确定性复现）。

不调 setter 的进程与 0.7.0 逐像素相同。上限不重烘已缓存的字形：缓存键里含设备字号，
下一次按新比例自然分档。

>**WarnIfImageUndersampled**（0.7.1 T-04，**只在 `INK_ISDEBUG` 下存在**）

```C++
void WarnIfImageUndersampled(const InkingImageBytes& image, int designWidth,
                             int designHeight, int srcWidth = 0,
                             int srcHeight = 0) noexcept;
```

判"素材像素够不够它占的屏幕像素"：需要宽 = 组件设计宽 × `R_max`，`σ < 0.75` 就 WARN
（带素材 / 需要 / σ 三个数），同名只报一次。`R_max` 用**主显示器桌面 ÷ 设计画布**
近似（构造期可能还没有窗口，而窗口尺寸随拖拽变；桌面对"这台机器最好的屏"是稳定上界）。

Release 构建里整个函数体被编译掉（`ImageStaticImage.cpp` 的调用点也在 `#if` 里），
所以发布包零成本、零日志。像素风素材（进程级 `Nearest`）与 SVG 直接跳过——它们
"放大"是正确用法。

>**TextCacheStats**

```C++
struct TextCacheStats {
    // 计数（"缓存有没有生效"的观测口）
    std::uint64_t texturesCreated = 0, cacheHits = 0, fontFallbacks = 0;
    std::uint64_t svgRasters = 0, svgSecondPasses = 0, svgCacheHits = 0;
    std::uint64_t svgClamps = 0, svgEvictions = 0, svgFailures = 0;
    std::uint64_t svgUnsupportedElements = 0;
    std::uint64_t rasterCapped = 0, imageUndersampled = 0;
    // 内存账本（0.8.2 T-05）
    std::uint64_t textureBytes = 0;      // 当前占用（**上界**）
    std::uint64_t textureBytesPeak = 0;  // 历史峰值（只增不减）
    std::uint64_t textureCount = 0;      // 纹理条数（字形图集计 1）
    std::uint64_t textureEvictions = 0;  // LRU 真的丢了几张（0.8.4 T-04）
};
TextCacheStats TextCacheStatsSnapshot() noexcept;
```

"缓存有没有生效"功能上看不出来（画出来一模一样，只是白干活），只有计数抓得住。

**`textureBytes` 是上界，不是精确分配量**（0.8.2 T-05）：按 `宽 × 高 × 4`（RGBA8）
算。`SDL_CreateTextureFromSurface` 建出来的纹理拿不到像素格式
（`SDL_GetTextureProperties` 返回 UNKNOWN），精确值无从得知；而对"会不会炸"这个
用途，偏高是安全方向。

四条边界：

- **偏高**：不透明图的实际分配可能是 RGB888，贴图可能有对齐填充；
- **不含渲染器自己的内部缓冲**（后缓冲、批处理顶点缓存）——那部分本库看不到，
  `textureBytes` 只回答"纹理缓存占多少"；
- **只统计这一层**：文字 / 图片 / SVG 纹理 + 字形图集，不含 surface、解码后的字节、
  字体句柄；
- **记账与决策分开**（0.8.4 起）：账本是"现在多少"，淘汰是 `SetTextureBudget`
  的事，而它**默认关闭**。

### 4.1 字节预算与 LRU 淘汰（0.8.4 T-04）

```C++
void SetTextureBudget(std::uint64_t bytes) noexcept;  // 0（默认）= 不淘汰
std::uint64_t GetTextureBudget() noexcept;
void BeginTextFrame(SDL_Renderer* renderer) noexcept;  // 由 RenderScene 每帧调
```

`probe_text_cost` 的实测背景：同样 1334 行，用 3 个不同字时 **1 张 / 4 MB / 221 ms**，
用 2 万个不同字时 **1220 张 / 43.7 MB / 1603 ms**。淘汰之前，全库唯一的回收口是
`ReleaseTextCache()`（全清）。

做法：`InkingSceneLibrary::RenderScene` 在 `scene->Render()` **之前**调 `BeginTextFrame`——
推进帧号；若 `textureBytes > budget`，先 `FlushGeometry()`（批里可能还引用着要丢的
纹理），再从 `lastUsedFrame` 最小的条目丢起，直到回到预算内。

四条必须记住的边界：

- **默认 0 = 不淘汰**，与 0.8.3 逐字节相同（第一次做淘汰，不悄悄改所有人的内存行为）；
- **瞬时上界是"预算 + 本帧新增"**：淘汰只在帧首，本帧刚建的纹理不能丢（丢了当场缺字），
  所以一帧里烘出 40 MB，那一帧照样是 40 MB；
- **字形图集不参与淘汰**（共享资源，丢了要重烘全部字形）；预算设得比图集
  （1024² ≈ 4 MB）还小时**放弃淘汰**——否则每帧清空重烘，比不设预算更糟；
- 计数用 RGBA8 上界，所以按它算预算偏保守（安全方向）。

> 想回到改动前的行为：`SetTextureBudget(0)`；或者不调 `BeginTextFrame`
> （淘汰完全惰性，其余行为不变）。

用法（回答"这套素材吃多少显存"）：

```C++
const auto stats = inking_detail::TextCacheStatsSnapshot();
std::printf("纹理 %llu 张 / 现在 %.1f MB / 峰值 %.1f MB\n",
            (unsigned long long)stats.textureCount,
            stats.textureBytes / 1048576.0,
            stats.textureBytesPeak / 1048576.0);
```

参考量级：`examples/pet` 那套 73 帧 321×900 的素材按 RGBA 算约 **84 MB**。

## 五、字形图集

`InkingGlyphAtlas` 是**一张大纹理上的 shelf packing 分配器**：按行（shelf）往里塞字形位图，
每格留 `kPadding = 1`，避免采样时互相渗色。

```C++
class InkingGlyphAtlas {
public:
    struct Slot { int x, y, width, height; };
    static constexpr int kPadding = 1;

    explicit InkingGlyphAtlas(int size = 1024);
    int Size() const noexcept;
    Slot Insert(int width, int height);     // 塞不下返回空槽
    int SlotCount() const noexcept;
    int ShelfCount() const noexcept;
    int UsedHeight() const noexcept;
    double Occupancy() const noexcept;
    bool OverlapsAny(const Slot& slot) const noexcept;
    void Reset();
    const std::vector<Slot>& Slots() const noexcept;
};
```

`OverlapsAny` 是**自检用的对账口**：把塞进去的每个槽两两比一遍，"没有重叠"这件事
只能这么验。

`detail` 侧（`AcquireGlyph` / `InkingLayOutGlyphs` / `GlyphAtlasSnapshot` /
`SetGlyphAtlasEnabled` / `GlyphAtlasTexture` / `ReleaseGlyphAtlas`）是内部实现：
按需烘一个字形、返回它的 UV 与位置。**图集满了不会整张重建**（0.8.3 之前这里写错了）：
`AcquireGlyph` 返回 `valid == false`、调用方**退回整段渲染**，同时

- 置 `GlyphAtlasStats::full` 并**只在第一次满时发一条 WARN**（带已用格数 / 上限 /
  后果）——这条 WARN 是"文字从此改走整段纹理（内存与首帧耗时都涨）"的唯一声音；
- 之后**整条逐字形路短路**：不再每帧把整段文字烘一遍再失败（实测改动前 19 次/帧）；
- 只有 `ReleaseGlyphAtlas()`（换比例 / 换字号档 / `ReleaseTextCache`）会把 `full`
  复位。图集是**共享资源**：清它的代价是全部字形重烘，所以别为了"腾一格"去清。

> 高清那条路上，图集按 `fontSize × mag × ratio` 烘字形、画出来除回 `ratio`，
> 于是布局零位移。比例变化只丢字形图集（纹理表里还有图片，别一起清）。

## 六、文本排版（纯函数）

```C++
InkingTextLayoutResult InkingLayoutText(const InkingTextLayoutRequest& request,
                            const InkingTextMeasureFn& measure);
float InkingLineOffsetX(float lineWidth, float availableWidth, InkingTextAlignH align);
```

这一层**不含 SDL**：真正的"文字 → 像素宽度"是文字层的事，这里只要一个测量回调。
于是排版逻辑可以用"每个码点固定宽"的假测量做数值自检，不需要字体、不需要渲染器。

`InkingTextLayoutRequest`：`text` / `style`（`fontSize` / `lineSpacing` / `fontPath` /
`fontPaths`（回退链）/ `align` / `alignV` / `padding`）/ `maxWidth` / `maxHeight` / `maxLines` /
`wrap`（`NoWrap` / `Word` / `Anywhere`）/ `overflow`（`Clip` / `Ellipsis`）。

排版规则（与 `CXXCSS.md` §3.8 的字段一一对应）：

1. **显式 `\n` 永远断行**（不管 `wrap`）；`\r\n` 当 `\n`；
2. `Word`：优先在**可断点**断（空白之后、CJK 字与字之间）。英文整词不切开；
   一个词自己就超宽时退化成 `Anywhere` 硬切（不然会溢出去，那是更糟的结果）；
3. `Anywhere`：逐码点硬切；
4. **行首空白丢掉**；
5. 行高 = `该行测量高度 × lineSpacing`；整块高 = 各行高之和（不含 padding）；
6. 宽度 = **最宽的一行**，再加 padding；
7. 受 `maxLines` / `maxWidth` / `maxHeight` 限制而有剩余内容时：`Ellipsis` 在最后一行末尾补 `…`
   （**先回退字符**直到放得下），`Clip` 直接丢掉；两种都置 `truncated`。**三条限制取更严的那个**，
   顺序是"先 `maxLines`、再 `maxHeight`"，两步都在垂直对齐之前；
8. `padding` **先扣**：可排宽度 = `maxWidth − 左右 padding`；扣完 ≤ 0 时**不换行**
   （一行硬断成零宽的行没有意义）——有意行为，不是漏判。

> **框高即上限**（0.8.3 T-01）：`maxHeight` 不只是垂直对齐的参照，它**真的会丢行**——
> 累计行高超过 `maxHeight − 上下 padding` 时尾部那些行整行不排。这就是 `InkingTextOverflow::Clip`
> （默认值）的实现。两条边界：**至少留一行**（框比一行还矮也画一行，宁可溢出几像素也不让
> 标签静默变空白）；**不缩字号**（"自动缩小"是另一件事，不做）。

空串返回 `empty = true`、尺寸只剩 padding。

> `InkingTextLayoutResult::width/height` 就是**文本框该有多大**，`InkingTextLine::begin/end` 是
> 源串里的**字节**区间，`hasEllipsis` 告诉绘制那一段末尾要接 `…`（`InkingTextEllipsis`
> 是 U+2026 三字节，**别用三个点**——宽度不一样，回退算法会算错）。

`InkingLineOffsetX` 单独拎出来，是为了"组件宽度和排版宽度不一样"时重算：
排版按 `maxWidth` 断行，而组件被布局拉得更宽时，用实际宽度再算一次即可——规则只有一份。

## 常见坑

1. **先 `SDL_DestroyRenderer` 再 `ReleaseTextCache`**：use-after-free。
2. **自己调 SDL 绘制前忘了 `NoteImmediateDraw()`**：那一笔被后刷的批次盖住。
3. **在裁剪期间不刷缓冲就改裁剪矩形**：先前攒的顶点会在"别人的裁剪"下被刷出去。
4. **把 `VertexCacheSlot` 当节点状态改**：它只是缓存，键不全就会"状态切了、画面没变"。
5. **缓存字形 UV**：图集满了一次重建，旧槽全失效。
6. **用三个点当省略号**：用 `InkingTextEllipsis`。

---

> 本文档对应版本：**0.9.2（0.x 发布版）**。
