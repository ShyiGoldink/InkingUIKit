#pragma once

// 提交层（T-6 / B4）：把"每个节点自己 immediate 提交"改成"**一帧成批提交**"。
//
// 这一层做两件事：
//   1. **缓冲**：形状填充不再立刻 `SDL_RenderGeometry`，而是把顶点追加进当前批次；
//   2. **成批提交**：连续的同一种提交合成一次调用。批分的种类是"**纹理句柄**"
//      ——纯色是 `nullptr`、图片是它自己那张纹理；**纹理变了就刷上一批**
//      （不重排、不跨笔合并），所以顺序永远与调用顺序一致。
//
// 文字走 `SDL_RenderTextureRotated`（dstrect + 旋转，另一种提交），它仍然是
// **批次边界**：画它之前先把几何批刷出去。
//
// 三条纪律（改这一层的人先读）：
//   - **保持顺序**：只合并"连续"的同种提交，绝不重排、绝不交换节点先后——
//     重排会造出"画的是一个、点的是另一个"那类只在特定叠放顺序下出现的 bug；
//   - **顶点数据与渲染器无关**（`SDL_RenderGeometry` 吃 CPU 数组）：所以顶点缓存
//     不必跟着渲染器销毁（纹理才需要，那是纹理层的事）；
//   - **计数是一等公民**：`BatchStats` 是自检能断言的观察口——"功能全对、只是
//     白干活"只有计数抓得住（`AGENTS.md` §6 第 40 条）。
//
// 与 `InkingDraw.h` 一样：这是框架内部头（含 SDL），不进公开的组件头。

#include <dataStruct/InkingShapeSpec.h>
#include <dataStruct/InkingTransform.h>

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace inking::detail {

/**
 * @brief 一个组件的**顶点缓存槽**（PIMPL：锚点持有一个 `unique_ptr`）。
 *
 * 顶点存的是**本地设备坐标**（相对组件左上角），平移在追加时做。这样"节点挪个
 * 位置"不会让缓存作废——只有形状 / 尺寸 / 倍率 / 变换变了才重算。
 */
struct VertexCacheSlot {
    std::vector<SDL_Vertex> vertices;
    std::vector<int> indices;

    /// 这批顶点是按什么算出来的：任何一项变了就重算（其余情况只做平移）。
    ShapeSpec shape{};
    int width = 0;
    int height = 0;
    float magnification = 0.0f;
    TransformSpec transform{};
    /// **颜色也算键**：顶点里带的是逐顶点颜色（`SDL_Vertex.color`），
    /// 换色不复用就会"状态切了、画面没变"——实测踩过一次。
    std::uint32_t color = 0u;
    bool valid = false;
};

/// 提交层的观察口。都是**累计值**——自检取差值，别假设从 0 开始。
struct BatchStats {
    std::uint64_t geometrySubmissions = 0;  ///< 真的调了几次 SDL_RenderGeometry
    std::uint64_t vertexRebuilds = 0;       ///< 重算顶点（轮廓采样 + 变换）的次数
    std::uint64_t vertexReuses = 0;         ///< 复用顶点（只做平移）的次数
    std::uint64_t textureSubmissions = 0;   ///< **绕开批次的**直接绘制次数（文字、contain 图片）
    std::uint64_t batchFlushes = 0;         ///< 批次被刷出去几次
    std::uint64_t batchSplits = 0;          ///< 因为顶点上限切过几次批
    /**
     * 其中**带纹理**的提交次数（B4）：同一张纹理连续画 N 笔只会 +1。
     *
     * "同批能合并"这件事功能上完全看不出来（画出来一模一样，只是多几次 draw
     * call），只有这个数抓得住——`AGENTS.md` §6 第 40 条那条老规矩。
     */
    std::uint64_t textureDraws = 0;
};

BatchStats BatchStatsSnapshot() noexcept;

/// 这一帧轮到哪个节点在画（`SubmitToRenderer` 设置 / 清除）。
/// 有了它，`fillShape` 才知道该用哪个缓存槽——**组件侧一个字都不用改**。
void SetCurrentNode(VertexCacheSlot* slot) noexcept;
VertexCacheSlot* CurrentNode() noexcept;

/// 一帧的开始 / 结束：`BeginFrame` 清空缓冲，`EndFrame` 把剩下的刷出去。
/// 场景的 `Render` 负责调（见 `InkingScene.cpp`）。
void BeginFrame(SDL_Renderer* renderer) noexcept;
void EndFrame() noexcept;

/// 把待提交的几何批立刻刷出去（纹理绘制之前，以及需要强制同步时）。
void FlushGeometry() noexcept;

/**
 * 追加一块**已经算好的本地顶点**（相对组件左上角）：提交层负责平移到目标位置。
 *
 * 这是 `fillShape` 与提交层之间的唯一接口——组件、形状采样都不知道批的存在。
 */
void AppendLocalGeometry(const SDL_Vertex* vertices, std::size_t vertexCount,
                         const int* indices, std::size_t indexCount,
                         float pixelX, float pixelY, bool reused);

/**
 * 追加一块**带纹理**的本地顶点（B4）：`texture` 就是这一批提交时绑的那张。
 *
 * 与 `AppendLocalGeometry` 的区别只有一条：**批是按纹理分的**。纹理句柄与当前
 * 批不同就先把上一批刷出去再开新的，于是"同一张纹理连续画 N 笔"合并成
 * **一次** `SDL_RenderGeometry`（看 `BatchStats::textureDraws`），而顺序仍旧
 * 与调用顺序一致（只合并连续的，不重排）。
 *
 * 顶点里的颜色是**调制色**：图片路径传全白（不换色）、alpha 已经进了顶点色，
 * 所以这一层不需要碰 `SDL_SetTextureColorMod`（那是纹理对象上的状态）。
 */
void AppendTexturedGeometry(SDL_Texture* texture, const SDL_Vertex* vertices,
                            std::size_t vertexCount, const int* indices,
                            std::size_t indexCount, float pixelX,
                            float pixelY);

/**
 * 把"本地设备像素"顶点搬到世界位置：加绝对左上角，再过一次祖先子树变换。
 *
 * 单独导出是因为**绕开批次的直接提交**也要走同一套换算（B1 的带纹理填充走
 * `SDL_RenderGeometry` + 纹理，不进几何批）。两份各写一遍就会出现"无纹理那条
 * 跟着容器动、纹理那条留在原地"——那种错只在容器动起来时才看得见。
 */
void ApplyAncestorAffineToVertices(SDL_Vertex* vertices, std::size_t vertexCount,
                                   float pixelX, float pixelY) noexcept;

/**
 * @brief 矩形 + `Fill` 的图片**是否并进几何批**（0.6.2 T-08，默认开）。
 *
 * 0.2.0 时代那条 `SDL_RenderTexture` 快路是为了绕开"几何批不吃纹理 alpha"的坑；
 * 0.6.2 的探针（`build/probe_batch.cpp`）在 SDL 3.4.16 的 **software 与
 * direct3d11** 两个后端上实测：几何批的透明区 / 50% 半透明 / 放大采样与单笔
 * **逐位相同**。所以默认合并——同纹理连续 N 笔 = **一次** `SDL_RenderGeometry`。
 *
 * 关掉它是**回退预案**，同时也是那条快路的"像素基准"：关掉时行为与 0.6.1
 * 完全一致（含它的既有缺陷：快路不看 `transform`，见 T-07 记录）。
 *
 * 只有 `rect + Fill` 这一种形态受影响；圆角 / `contain` / `cover` 本来就批次里。
 */
void SetImageBatchingEnabled(bool enabled) noexcept;

/// 现在是不是把矩形图片并进批次（见上）。
bool ImageBatchingEnabled() noexcept;

/// 记一次**绕开批次的直接绘制**（纹理、占位方块、将来自定义的绘制……）：
/// 它会先把几何批刷出去，顺序因此永远正确。
///
/// 规矩只有一条：**任何直接调 SDL 绘制的地方，前面必须调它**。
/// 漏了就不会报错——只是那一笔被后面刷出去的批次盖住（踩过一次）。
void NoteImmediateDraw() noexcept;

// ---------------------------------------------------------------------------
// 裁剪（A5）：SDD 的裁剪矩形是"绘制那一刻"生效的，所以它**天然是批次边界**
// ---------------------------------------------------------------------------
//
// 两条纪律：
//   1. **坐标系与顶点相同**：传进来的矩形是"设备像素"（和
//      `AppendLocalGeometry` 里那份顶点一致）。SDL 在 backbuffer 上会把 clip
//      当逻辑坐标再缩放一次，而顶点也吃同一套变换——两者同源，裁剪相对图形
//      永远是对的（源项目 `tests/test_clip.cpp` 的探针段实测过；该套件未搬入）。
//   2. **设置前必须先刷缓冲**：否则先前攒下的顶点会在"别人的裁剪"下被刷出去，
//      表现是"有的组件被剪、有的没被剪"，而且和叠放顺序有关。

/**
 * 设置裁剪矩形（**设备像素**）。会先把待提交的几何刷出去。
 *
 * 取整用**包含式**（floor 起点、ceil 终点）：宁可多画半像素，也不要因为取整
 * 把内容削掉一条——"少画一像素"比"多画一像素"难查得多。
 */
void SetClipRect(SDL_Renderer* renderer, const ShapeBounds& deviceRect) noexcept;

/// 关掉裁剪（恢复成整个渲染目标）。同样先刷缓冲（在旧裁剪下刷完再关）。
void ClearClipRect(SDL_Renderer* renderer) noexcept;

}  // namespace inking::detail
