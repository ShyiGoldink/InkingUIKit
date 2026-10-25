# 渲染层

**回到API目录：**
[API index](../API.md)

渲染层是**框架内部**：这一层的头文件要么含 SDL，要么含 `detail::` 内部工具。
业务代码不需要 include 它们——组件实现里才会用到。放在公开 `include/` 下只是因为
`include/` 是唯一的头文件搜索根。

本页覆盖：

| 头文件 | 内容 | 含 SDL |
| --- | --- | --- |
| `dataStruct/InkingTextRequest.h` | `TextRequest` / `TextMetrics`（**纯数据，公开**） | 否 |
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
            ├─ detail::SetCurrentNode(_vertexCache.get())        // 这个节点用哪个缓存槽
            ├─ currentRendererSlot() = renderer                  // onRender 里从槽里取
            └─ onRender(pixelX, pixelY)
                 ├─ detail::fillShape(...)          → AppendContour → AppendEarClipIndices
                 │                                     → AppendLocalGeometry → SDL_RenderGeometry
                 ├─ detail::fillShapeTextured(...)  → 同样网格 + UV → SDL_RenderGeometry(带纹理)
                 ├─ detail::strokeShape(...)        → 两条轮廓之间的环带
                 └─ detail::PaintText(...)          → 纹理表（缓存）→ SDL_RenderTextureRotated
  └─ detail::BeginFrame / EndFrame（场景 Render 的首尾）
```

**为什么 `onRender` 只有坐标、没有渲染器**：不希望每个组件实现都被迫多接一个参数。
提交前把渲染器挂到"当前渲染器"槽上，`onRender` 期间从槽里取。

**槽位定义在 `.cpp` 里**（`src/render/InkingDraw.cpp`）：头内 inline 函数里的函数内静态量
在动态库下会 exe / DLL 各一份，而且**不报错**。所以 `currentRendererSlot()` /
`currentAncestorAffineSlot()` 只有声明在头里。

## 二、形状同源：渲染 / 命中 / AABB 一份定义

`InkingMesh.h` 是渲染层里**唯一不碰 SDL** 的那一半。它回答的和 `ShapeContains`
（[InkingShapeSpec.md](InkingShapeSpec.md)）是**同一个几何问题**：`AppendContour` 采出来的
轮廓必须与 `ShapeContains` 判的内外同源，否则就是"画的圆角、点的是直角"那种只在角落
露头的错。

```C++
inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr int kMinCircleSteps = 24;
inline constexpr int kMaxCircleSteps = 512;
inline constexpr int kMinArcSteps = 4;
inline constexpr int kMaxArcSteps = 64;

int  StepsForLength(float pixels, int minSteps, int maxSteps);
int  ContourSteps(const ShapeSpec& scaled, float width, float height);
void AppendContour(std::vector<OutlinePoint>& out, const ShapeSpec& shape, ...);
bool SameShape(const ShapeSpec& a, const ShapeSpec& b);
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
    ShapeSpec shape{};        // ↓ 这几项构成"这批顶点是按什么算出来的"
    int width = 0, height = 0;
    float magnification = 0.0f;
    TransformSpec transform{};
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
void SetClipRect(SDL_Renderer* renderer, const ShapeBounds& deviceRect) noexcept;
void ClearClipRect(SDL_Renderer* renderer) noexcept;
```

`NoteImmediateDraw()` 是那条唯一的规矩：**任何直接调 SDL 绘制的地方，前面必须调它**。
漏了不会报错——只是那一笔被后面刷出去的批次盖住（踩过一次）。

裁剪两条纪律：**坐标系与顶点相同**（传设备像素）、**设置前必须先刷缓冲**
（否则先前攒下的顶点会在"别人的裁剪"下被刷出去，表现是"有的组件被剪、有的没被剪"，
而且和叠放顺序有关）。取整用**包含式**（floor 起点、ceil 终点）：宁可多画半像素，
也不要因为取整把内容削掉一条。

## 四、文字与图片

>**TextRequest**（`dataStruct/InkingTextRequest.h`，纯数据）

```C++
struct TextRequest {
    std::string text;              // UTF-8
    float fontSize = 16.0f;        // 设计坐标（SDL3_ttf 的 DPI 是 72，"点"= 像素）
    std::string fontPath;          // 空 = 机器默认字体（按候选表找）
    std::vector<std::string> fontPaths;  // 回退链：fontPath 之后按顺序试（0.3.0）
    std::uint32_t color = 0xFFFFFFFFu;
    float offsetX = 0.0f;          // 文字块左上角，组件本地设计坐标
    float offsetY = 0.0f;
    bool Empty() const noexcept;
};

struct TextMetrics { float width = 0.0f; float height = 0.0f; };
```

`MeasureText` **不看** `offsetX/Y`。拿不到字体时（没编 SDL3_ttf / 找不到字体 / 字号 ≤ 0）
返回**估算值**（按字号的方块：宽 = 字号 × 码点数，高 = 字号），与绘制的退化画法一致。
不依赖渲染器，可以单独调。

>**MeasureText / PaintText**

```C++
TextMetrics MeasureText(const TextRequest& request);

void PaintText(SDL_Renderer* renderer, const TextRequest& request,
               int width, int height, float magnification,
               float pixelX, float pixelY, const TransformSpec& transform);
```

`PaintText` 是文字**唯一的绘制入口**（所有组件都走它）。三件事值得记住：

- **按 `fontSize × magnification` 设备像素栅格化**（不然放大就糊），所以缓存键里带的是
  这个乘积；
- **颜色走纹理调制**：纹理烘成白字，画的时候 `SDL_SetTextureColorMod` 上色 ⇒
  同一句话换颜色**不用重烘纹理**（颜色不进缓存键）；
- **变换绕组件中心**，与形状同一套语义，所以文字跟着按钮一起转。

>**PaintImage / ImageBytes**

```C++
struct ImageBytes {
    const std::uint8_t* data = nullptr;   // 借用：指向生成代码里那个静态数组
    std::size_t size = 0;
    std::string name;                     // 配置里的路径（缓存键 + 日志）
    bool Empty() const noexcept;
};

bool PaintImage(SDL_Renderer* renderer, const ImageBytes& image,
                const ShapeSpec& shape, int width, int height,
                float magnification, float pixelX, float pixelY,
                const TransformSpec& transform, ImageFit fit,
                std::uint32_t tintColor);
```

四个已定的决定：**首次绘制时解码**（进程启动什么都不做）、**按组件形状裁剪**（走形状层
同一条网格生成，所以圆角按钮配图片时四角一起被剪）、**适配模式**（`fill` / `contain` /
`cover`，几何在 `ImageFitSpec` 的纯函数里）、**变换同源**。
`tintColor` 只取 **alpha**（过渡时淡入淡出，图片本身不换色）。解码不出来返回 false，
调用方可以退回"按颜色填形状"。

>**ReleaseTextCache**

```C++
void ReleaseTextCache() noexcept;
```

放掉这一层持有的全部纹理与字体。**渲染器销毁之前必须调**：纹理属于渲染器，
渲染器一销毁句柄就成了野指针，那时再 `SDL_DestroyTexture` 是 use-after-free。
顺序永远是"先 `ReleaseTextCache`，再 `SDL_DestroyRenderer`"。幂等，可重复调，
同时会 `TTF_Quit`（与内部那次 `TTF_Init` 配对）。

>**TextCacheStats**

```C++
struct TextCacheStats { std::uint64_t texturesCreated = 0; std::uint64_t cacheHits = 0; };
TextCacheStats TextCacheStatsSnapshot() noexcept;
```

"缓存有没有生效"功能上看不出来（画出来一模一样，只是白干活），只有计数抓得住。

## 五、字形图集

`GlyphAtlas` 是**一张大纹理上的 shelf packing 分配器**：按行（shelf）往里塞字形位图，
每格留 `kPadding = 1`，避免采样时互相渗色。

```C++
class GlyphAtlas {
public:
    struct Slot { int x, y, width, height; };
    static constexpr int kPadding = 1;

    explicit GlyphAtlas(int size = 1024);
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

`detail` 侧（`AcquireGlyph` / `LayOutGlyphs` / `GlyphAtlasSnapshot` /
`SetGlyphAtlasEnabled` / `GlyphAtlasTexture` / `ReleaseGlyphAtlas`）是内部实现：
按需烘一个字形、返回它的 UV 与位置。图集满了会整张重建（旧槽全部失效，所以调用方
每帧都要重新问 UV，不要缓存）。

> 高清那条路上，图集按 `fontSize × mag × ratio` 烘字形、画出来除回 `ratio`，
> 于是布局零位移。比例变化只丢字形图集（纹理表里还有图片，别一起清）。

## 六、文本排版（纯函数）

```C++
TextLayoutResult LayoutText(const TextLayoutRequest& request,
                            const TextMeasureFn& measure);
float LineOffsetX(float lineWidth, float availableWidth, TextAlignH align);
```

这一层**不含 SDL**：真正的"文字 → 像素宽度"是文字层的事，这里只要一个测量回调。
于是排版逻辑可以用"每个码点固定宽"的假测量做数值自检，不需要字体、不需要渲染器。

`TextLayoutRequest`：`text` / `style`（`fontSize` / `lineSpacing` / `fontPath` /
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
7. 受 `maxLines` / `maxWidth` 限制而有剩余内容时：`Ellipsis` 在最后一行末尾补 `…`
   （**先回退字符**直到放得下），`Clip` 直接丢掉；两种都置 `truncated`；
8. `padding` **先扣**：可排宽度 = `maxWidth − 左右 padding`；扣完 ≤ 0 时**不换行**
   （一行硬断成零宽的行没有意义）——有意行为，不是漏判。

空串返回 `empty = true`、尺寸只剩 padding。

> `TextLayoutResult::width/height` 就是**文本框该有多大**，`TextLine::begin/end` 是
> 源串里的**字节**区间，`hasEllipsis` 告诉绘制那一段末尾要接 `…`（`kTextEllipsis`
> 是 U+2026 三字节，**别用三个点**——宽度不一样，回退算法会算错）。

`LineOffsetX` 单独拎出来，是为了"组件宽度和排版宽度不一样"时重算：
排版按 `maxWidth` 断行，而组件被布局拉得更宽时，用实际宽度再算一次即可——规则只有一份。

## 常见坑

1. **先 `SDL_DestroyRenderer` 再 `ReleaseTextCache`**：use-after-free。
2. **自己调 SDL 绘制前忘了 `NoteImmediateDraw()`**：那一笔被后刷的批次盖住。
3. **在裁剪期间不刷缓冲就改裁剪矩形**：先前攒的顶点会在"别人的裁剪"下被刷出去。
4. **把 `VertexCacheSlot` 当节点状态改**：它只是缓存，键不全就会"状态切了、画面没变"。
5. **缓存字形 UV**：图集满了一次重建，旧槽全失效。
6. **用三个点当省略号**：用 `kTextEllipsis`。

---

> 本文档对应版本：**0.5.2（0.x 发布版）**。
