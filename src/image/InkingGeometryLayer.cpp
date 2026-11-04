#include <image/InkingGeometryLayer.h>

#include <image/InkingMask.h>

#include <dataStruct/InkingColor.h>
#include <detail/InkLog.h>
#include <render/InkingBatch.h>
#include <render/InkingDraw.h>
#include <render/InkingTextureResolve.h>

#include <SDL3/SDL.h>

#include <cmath>

namespace {

/// 0xAARRGGBB → 顶点色。纹理本身不换色，只借 alpha 做淡入淡出。
SDL_FColor ToFColor(std::uint32_t color) noexcept {
    constexpr float k = 1.0f / 255.0f;
    return SDL_FColor{static_cast<float>(InkingColorRed(color)) * k,
                      static_cast<float>(InkingColorGreen(color)) * k,
                      static_cast<float>(InkingColorBlue(color)) * k,
                      static_cast<float>(InkingColorAlpha(color)) * k};
}

/// 几何模式的顶点转换缓冲（复用容量，避免每帧一次分配）。
/// **UI 线程专用**：与纹理层同一条约定（绘制全程在提交循环里）。
std::vector<SDL_Vertex>& geometryScratch() {
    static std::vector<SDL_Vertex> scratch;
    return scratch;
}

}  // namespace

InkingGeometryLayer::InkingGeometryLayer(InkingWidget* parent,
                                         const std::string& name)
    : InkingStaticWidget(parent, name) {
    // **不进命中表**：几千个物体各占一条命中记录，正是这条要消掉的东西。
    // 要点击就在业务侧用 `InkingShapeContains` 判（那是公开的，且与渲染同源）。
    SetHitPassThrough(true);
}

void InkingGeometryLayer::SetQuads(const InkingQuad* quads, std::size_t count) {
    // 两套数据互斥（见头文件）：后设的赢。几何那批一并清掉，免得"看着还在、
    // 其实早就不画了"。
    _vertices.clear();
    _indices.clear();
    _geometryTexture = InkingTextureHandle{};
    _useGeometry = false;
    _quads.clear();
    if (quads != nullptr && count > 0) {
        _quads.assign(quads, quads + count);
    }
    MarkRepaintDirty();
}

void InkingGeometryLayer::SetQuads(const std::vector<InkingQuad>& quads) {
    _vertices.clear();
    _indices.clear();
    _geometryTexture = InkingTextureHandle{};
    _useGeometry = false;
    _quads = quads;
    MarkRepaintDirty();
}

void InkingGeometryLayer::AppendQuads(const InkingQuad* quads, std::size_t count) {
    if (quads == nullptr || count == 0) {
        return;
    }
    _quads.insert(_quads.end(), quads, quads + count);
    MarkRepaintDirty();
}

void InkingGeometryLayer::ClearQuads() noexcept {
    _quads.clear();
}

bool InkingGeometryLayer::SetGeometry(InkingTextureHandle texture,
                                      const InkingVertex* vertices,
                                      std::size_t vertexCount,
                                      const std::uint16_t* indices,
                                      std::size_t indexCount) {
    // 三条硬约束都是"整次拒绝"，不部分提交：截断一半的三角形列表会画出
    // "少一块但看着像对的"画面，比整块不画难查得多。
    constexpr std::size_t kMaxVertices = 16384;  // = 提交层的单批上限
    bool ok = texture.Valid() && vertices != nullptr && vertexCount > 0
           && indices != nullptr && indexCount > 0 && indexCount % 3 == 0
           && vertexCount <= kMaxVertices;
    if (ok) {
        for (std::size_t i = 0; i < indexCount; ++i) {
            if (indices[i] >= vertexCount) {
                ok = false;
                break;
            }
        }
    }
    if (!ok) {
        ++_rejectedGeometry;
        INK_LOG_ERROR(InkingGeometryModuleName,
                      "SetGeometry 被拒（顶点数 / 索引越界 / 索引数不是 3 的倍数）");
        return false;
    }

    // 与四边形互斥：后设的赢。
    _quads.clear();
    _useGeometry = true;
    _geometryTexture = texture;
    _vertices.assign(vertices, vertices + vertexCount);
    _indices.resize(indexCount);
    for (std::size_t i = 0; i < indexCount; ++i) {
        _indices[i] = static_cast<int>(indices[i]);
    }
    MarkRepaintDirty();
    return true;
}

bool InkingGeometryLayer::SetNinePatch(InkingTextureHandle texture,
                                       const InkingQuad& quad,
                                       const InkingNinePatchBorder& border) {
    InkingNinePatchTile tiles[9];
    const std::size_t count =
        InkingExpandNinePatch(quad.dst, quad.src, border, tiles);
    if (count == 0) {
        ++_rejectedGeometry;
        INK_LOG_ERROR(InkingGeometryModuleName,
                      "SetNinePatch：目标或源矩形退化（宽高必须 > 0）");
        return false;
    }

    std::vector<InkingQuad> expanded(count);
    for (std::size_t i = 0; i < count; ++i) {
        InkingQuad& piece = expanded[i];
        piece.texture = texture;
        piece.src = tiles[i].src;
        piece.dst = tiles[i].dst;
        piece.tint = quad.tint;
        // 九宫格与旋转互斥：先展开再整体转，边框会被拧歪。
        piece.rotation = quad.rotation;
    }
    SetQuads(expanded);
    return true;
}

bool InkingGeometryLayer::SetMaskedQuad(InkingTextureHandle texture,
                                        const InkingQuad& quad,
                                        const InkingShapeSpec& mask,
                                        float maskWidth, float maskHeight,
                                        int density) {
    // 纹理尺寸在这一层问（`InkingBuildMaskedQuad` 只做纯几何，不碰渲染器）。
    int textureWidth = 0;
    int textureHeight = 0;
    if (!InkingTextureSize(texture, &textureWidth, &textureHeight)) {
        // 句柄还没解析过（没建纹理）也别拒：UV 退回"整张图"，第一次提交时再建。
        textureWidth = 0;
        textureHeight = 0;
    }
    InkingMaskedQuad masked = InkingBuildMaskedQuad(
        quad, mask, textureWidth, textureHeight, maskWidth, maskHeight, density);
    if (masked.Empty()) {
        ++_rejectedGeometry;
        INK_LOG_ERROR(InkingGeometryModuleName, "SetMaskedQuad：目标矩形退化，遮罩网格为空");
        return false;
    }
    if (masked.clamped) {
        INK_LOG_WARN(InkingGeometryModuleName,
                     "SetMaskedQuad：细分密度超过上限，已夹到 "
                         + std::to_string(InkingMaskMaxDensity) + "（边缘会略糙）");
    }
    // 顶点数由密度决定：64 格是 4225，仍在 `SetGeometry` 的 16384 之内。
    return SetGeometry(texture, masked.vertices.data(), masked.vertices.size(),
                       masked.indices.data(), masked.indices.size());
}

std::size_t InkingGeometryLayer::GetVertexCount() const noexcept {
    return _vertices.size();
}

std::uint64_t InkingGeometryLayer::GetRejectedGeometryCount() const noexcept {
    return _rejectedGeometry;
}

bool InkingGeometryLayer::IsGeometryMode() const noexcept {
    return _useGeometry;
}

std::size_t InkingGeometryLayer::GetQuadCount() const noexcept {
    return _quads.size();
}

std::uint64_t InkingGeometryLayer::GetSkippedQuadCount() const noexcept {
    return _skippedQuads;
}

const std::vector<InkingQuad>& InkingGeometryLayer::GetQuads() const noexcept {
    return _quads;
}

void InkingGeometryLayer::onRender(float pixelX, float pixelY) const {
    SDL_Renderer* renderer = inking_detail::currentRenderer();
    if (renderer == nullptr) {
        return;
    }
    if (_useGeometry) {
        renderGeometry(pixelX, pixelY);
        return;
    }
    if (_quads.empty()) {
        return;
    }
    renderQuads(pixelX, pixelY);
}

void InkingGeometryLayer::renderGeometry(float pixelX, float pixelY) const {
    if (_vertices.empty() || _indices.empty()) {
        return;
    }
    int textureWidth = 0;
    int textureHeight = 0;
    SDL_Texture* texture = inking_detail::ResolveTextureHandle(
        _geometryTexture.id, inking_detail::currentRenderer(), &textureWidth,
        &textureHeight);
    if (texture == nullptr) {
        ++_rejectedGeometry;
        return;
    }

    const float mag = GetMagnification() > 0.0f ? GetMagnification() : 1.0f;

    // 顶点要的是**本地设备坐标**：设计坐标乘倍率，平移交给提交层。
    // 一块临时缓冲（`_vertices` 是调用方给的原始数据，不能就地改）。
    std::vector<SDL_Vertex>& scratch = geometryScratch();
    scratch.resize(_vertices.size());
    for (std::size_t i = 0; i < _vertices.size(); ++i) {
        const InkingVertex& source = _vertices[i];
        SDL_Vertex& target = scratch[i];
        target.position = SDL_FPoint{source.x * mag, source.y * mag};
        target.color = ToFColor(source.color);
        target.tex_coord = SDL_FPoint{source.u, source.v};
    }

    // 先刷一次：`AppendTexturedGeometry` 是"整块加入"，不刷的话这一批可能
    // 与前面的顶点合到 16384 以上（那正是单批上限要挡的事）。
    inking_detail::FlushGeometry();
    inking_detail::AppendTexturedGeometry(texture, scratch.data(), scratch.size(),
                                          _indices.data(), _indices.size(),
                                          pixelX, pixelY);
}

void InkingGeometryLayer::renderQuads(float pixelX, float pixelY) const {
    SDL_Renderer* renderer = inking_detail::currentRenderer();
    if (renderer == nullptr) {
        return;
    }

    // 设计坐标 → 设备像素。`pixelX/pixelY` 已经是设备像素（`SubmitToRenderer`
    // 传的就是 `absX * scale`），而顶点要的是**本地设备坐标**，平移由提交层加。
    const float mag = GetMagnification() > 0.0f ? GetMagnification() : 1.0f;

    // 句柄解析**按段复用**：一批 quad 绝大多数共用一张纹理（图集 / 同一张精灵图），
    // 每块都去查一次表就是几千次哈希。只在 id 变化时重查——失效的 id 也缓存
    // （缓成 nullptr），否则一个坏句柄会让它后面每一块都白查一次。
    std::uint32_t cachedId = 0;
    SDL_Texture* cachedTexture = nullptr;
    int cachedTextureWidth = 0;
    int cachedTextureHeight = 0;
    bool haveCache = false;

    for (const InkingQuad& quad : _quads) {
        if (!haveCache || quad.texture.id != cachedId) {
            cachedTexture = inking_detail::ResolveTextureHandle(
                quad.texture.id, renderer, &cachedTextureWidth,
                &cachedTextureHeight);
            cachedId = quad.texture.id;
            haveCache = true;
        }
        SDL_Texture* texture = cachedTexture;
        if (texture == nullptr || cachedTextureWidth <= 0
            || cachedTextureHeight <= 0) {
            // 一个坏句柄不该让整屏黑掉：跳过这一块，累计计数（见 GetSkippedQuadCount）。
            ++_skippedQuads;
            continue;
        }
        const int textureWidth = cachedTextureWidth;
        const int textureHeight = cachedTextureHeight;

        // `src` 全零 = 整张纹理（与 `InkingImageFitSpec::SourceRect` 同一个口径）。
        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = 1.0f;
        float v1 = 1.0f;
        if (quad.src.width > 0.0f && quad.src.height > 0.0f) {
            const float invW = 1.0f / static_cast<float>(textureWidth);
            const float invH = 1.0f / static_cast<float>(textureHeight);
            u0 = quad.src.x * invW;
            v0 = quad.src.y * invH;
            u1 = (quad.src.x + quad.src.width) * invW;
            v1 = (quad.src.y + quad.src.height) * invH;
        }

        const float x0 = quad.dst.x * mag;
        const float y0 = quad.dst.y * mag;
        const float x1 = (quad.dst.x + quad.dst.width) * mag;
        const float y1 = (quad.dst.y + quad.dst.height) * mag;
        if (x1 <= x0 || y1 <= y0) {
            continue;  // 退化矩形没有面积，提交了也只是白算
        }

        float xs[4] = {x0, x1, x1, x0};
        float ys[4] = {y0, y0, y1, y1};
        if (quad.rotation != 0.0f) {
            // 绕 dst 中心转。**只转这一笔**：顶点在本地空间里转完，祖先变换
            // 由提交层统一叠加，两处各转一次就是"转得比预期多一倍"。
            const float cx = (x0 + x1) * 0.5f;
            const float cy = (y0 + y1) * 0.5f;
            const float radians = quad.rotation * 3.14159265358979323846f / 180.0f;
            const float c = std::cos(radians);
            const float s = std::sin(radians);
            for (int i = 0; i < 4; ++i) {
                const float dx = xs[i] - cx;
                const float dy = ys[i] - cy;
                xs[i] = cx + dx * c - dy * s;
                ys[i] = cy + dx * s + dy * c;
            }
        }

        const SDL_FColor color = ToFColor(quad.tint);
        SDL_Vertex vertices[4];
        const float us[4] = {u0, u1, u1, u0};
        const float vs[4] = {v0, v0, v1, v1};
        for (int i = 0; i < 4; ++i) {
            vertices[i].position = SDL_FPoint{xs[i], ys[i]};
            vertices[i].color = color;
            vertices[i].tex_coord = SDL_FPoint{us[i], vs[i]};
        }
        const int indices[6] = {0, 1, 2, 0, 2, 3};

        // 交给提交层：连续同一张纹理的 quad 会并成**一次** SDL_RenderGeometry
        // （`BatchStats::textureDraws` 可断言）。这里绝不直接调 SDL——那会成为
        // 批次边界，而且绕开顺序纪律。
        inking_detail::AppendTexturedGeometry(texture, vertices, 4, indices, 6,
                                              pixelX, pixelY);
    }
}
