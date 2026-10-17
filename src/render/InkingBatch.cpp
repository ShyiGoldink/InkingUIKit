#include <render/InkingBatch.h>

#include <render/InkingDraw.h>

#include <cmath>

// 提交层的实现：一个"当前帧"的缓冲 + 计数。UI 线程使用，不加锁
// （和形状那一层的 scratch 缓冲同一条约定）。

namespace inking::detail {

namespace {

/**
 * 单批顶点上限（**自己设的**，不赌后端文档）：65536 是很多后端 u16 索引的边界，
 * 这里留四倍余量取 16384——超了就切批（`batchSplits` 记一笔），别让它变成
 * 某个驱动上的"画到一半开始乱"。
 */
constexpr std::size_t kMaxBatchVertices = 16384;

struct FrameState {
    SDL_Renderer* renderer = nullptr;
    VertexCacheSlot* node = nullptr;
    std::vector<SDL_Vertex> vertices;
    std::vector<int> indices;
    /// 当前这批顶点绑哪张纹理（B4）：`nullptr` = 纯色几何。
    /// 它变了就把上一批刷出去——批就是按它分的。
    SDL_Texture* texture = nullptr;
    bool active = false;
};

FrameState& state() {
    static FrameState instance;
    return instance;
}

BatchStats& counters() {
    static BatchStats instance;
    return instance;
}

}  // namespace

BatchStats BatchStatsSnapshot() noexcept { return counters(); }

void SetCurrentNode(VertexCacheSlot* slot) noexcept { state().node = slot; }

VertexCacheSlot* CurrentNode() noexcept { return state().node; }

void BeginFrame(SDL_Renderer* renderer) noexcept {
    FrameState& frame = state();
    frame.renderer = renderer;
    frame.active = renderer != nullptr;
    frame.node = nullptr;
    frame.texture = nullptr;
    frame.vertices.clear();
    frame.indices.clear();
}

void EndFrame() noexcept {
    FlushGeometry();
    FrameState& frame = state();
    frame.node = nullptr;
    frame.texture = nullptr;
    frame.renderer = nullptr;
    frame.active = false;
}

void FlushGeometry() noexcept {
    FrameState& frame = state();
    if (frame.renderer == nullptr || frame.vertices.empty()) {
        frame.vertices.clear();
        frame.indices.clear();
        frame.texture = nullptr;
        return;
    }
    ++counters().geometrySubmissions;
    ++counters().batchFlushes;
    if (frame.texture != nullptr) {
        ++counters().textureDraws;
    }
    SDL_RenderGeometry(frame.renderer, frame.texture, frame.vertices.data(),
                       static_cast<int>(frame.vertices.size()),
                       frame.indices.data(),
                       static_cast<int>(frame.indices.size()));
    frame.vertices.clear();
    frame.indices.clear();
    frame.texture = nullptr;
}

namespace {

/**
 * 追加一块顶点：**纯色与带纹理共用**的那一半（换算 + 索引）。
 *
 * 分出来是为了让"批按纹理分"这件事只有一处判断——两份实现迟早会有一份
 * 忘了处理"当前批的纹理与要画的不一样"（那种错的表现是**贴错图**）。
 */
void AppendVertices(const SDL_Vertex* vertices, std::size_t vertexCount,
                    const int* indices, std::size_t indexCount, float pixelX,
                    float pixelY) {
    FrameState& frame = state();
    if (frame.vertices.size() + vertexCount > kMaxBatchVertices) {
        ++counters().batchSplits;
        FlushGeometry();
    }

    const int base = static_cast<int>(frame.vertices.size());
    frame.vertices.reserve(frame.vertices.size() + vertexCount);
    for (std::size_t i = 0; i < vertexCount; ++i) {
        frame.vertices.push_back(vertices[i]);
    }
    // 位置换算与"直接提交"那条路共用一份实现（见上面那个函数）。
    ApplyAncestorAffineToVertices(frame.vertices.data() + base, vertexCount,
                                  pixelX, pixelY);

    frame.indices.reserve(frame.indices.size() + indexCount);
    for (std::size_t i = 0; i < indexCount; ++i) {
        frame.indices.push_back(base + indices[i]);
    }
}

}  // namespace

void ApplyAncestorAffineToVertices(SDL_Vertex* vertices, std::size_t vertexCount,
                                   float pixelX, float pixelY) noexcept {
    if (vertices == nullptr) {
        return;
    }
    // 祖先子树变换（T-7）：顶点缓存里存的是**本地**坐标，容器挪动在这里体现——
    // 于是"容器动了"既不作废子节点的顶点缓存，又能让整棵子树跟着走。
    const Affine ancestor = currentAncestorAffine();
    for (std::size_t i = 0; i < vertexCount; ++i) {
        const float x = vertices[i].position.x + pixelX;
        const float y = vertices[i].position.y + pixelY;
        vertices[i].position.x = ancestor.a * x - ancestor.b * y + ancestor.tx;
        vertices[i].position.y = ancestor.a * y + ancestor.b * x + ancestor.ty;
    }
}

void AppendLocalGeometry(const SDL_Vertex* vertices, std::size_t vertexCount,
                         const int* indices, std::size_t indexCount,
                         float pixelX, float pixelY, bool reused) {
    if (vertices == nullptr || indices == nullptr || vertexCount == 0
        || indexCount == 0) {
        return;
    }
    // 计数：重算了还是复用了——"功能全对、只是白干活"只有这两个数抓得住。
    if (reused) {
        ++counters().vertexReuses;
    } else {
        ++counters().vertexRebuilds;
    }
    FrameState& frame = state();
    // 当前批带着纹理（上一笔是图片）：纯色几何不能并进去，先把那一批刷掉。
    if (frame.texture != nullptr) {
        FlushGeometry();
    }
    AppendVertices(vertices, vertexCount, indices, indexCount, pixelX, pixelY);
}

void AppendTexturedGeometry(SDL_Texture* texture, const SDL_Vertex* vertices,
                            std::size_t vertexCount, const int* indices,
                            std::size_t indexCount, float pixelX,
                            float pixelY) {
    if (texture == nullptr || vertices == nullptr || indices == nullptr
        || vertexCount == 0 || indexCount == 0) {
        return;
    }
    FrameState& frame = state();
    // **批就是按纹理分的**：换了纹理就刷上一批（同一张纹理连续画才会合并）。
    if (frame.texture != texture) {
        FlushGeometry();
        if (frame.renderer != nullptr) {
            frame.texture = texture;
        }
    }
    AppendVertices(vertices, vertexCount, indices, indexCount, pixelX, pixelY);
}

void NoteImmediateDraw() noexcept {
    ++counters().textureSubmissions;
    // 顺序关键：纹理绘制**不能**被并进几何批，也不能插到批的中间去。
    FlushGeometry();
}

void SetClipRect(SDL_Renderer* renderer, const ShapeBounds& deviceRect) noexcept {
    // 先刷：攒着的顶点属于"上一个裁剪"，必须在旧裁剪下画完再换新的。
    FlushGeometry();
    if (renderer == nullptr) {
        return;
    }
    SDL_Rect rect{};
    rect.x = static_cast<int>(std::floor(deviceRect.x));
    rect.y = static_cast<int>(std::floor(deviceRect.y));
    const int right = static_cast<int>(std::ceil(deviceRect.x + deviceRect.width));
    const int bottom = static_cast<int>(std::ceil(deviceRect.y + deviceRect.height));
    rect.w = right - rect.x;
    rect.h = bottom - rect.y;
    if (rect.w < 0) {
        rect.w = 0;
    }
    if (rect.h < 0) {
        rect.h = 0;
    }
    SDL_SetRenderClipRect(renderer, &rect);
}

void ClearClipRect(SDL_Renderer* renderer) noexcept {
    FlushGeometry();
    if (renderer != nullptr) {
        SDL_SetRenderClipRect(renderer, nullptr);
    }
}

}  // namespace inking::detail
