#include <field/InkingField.h>

#include <config/window_config.h>
#include <dataStruct/InkingColor.h>
#include <detail/InkLog.h>
#include <render/InkingBatch.h>
#include <render/InkingDraw.h>
#include <render/InkingTextureResolve.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

// 为什么这里可以 include `render/`：**只有 .cpp 可以**。`field/` 的依赖纪律
// （任务书 §1 第 1 条）约束的是**公开头**——头里一旦拖进 SDL，消费者就再也
// 甩不掉它，`field/` 也就不能做"无窗口测试"了。提交本身必须落到 `inking_detail`
// 那条唯一通道上（和 `InkingGeometryLayer` 同一份实现），否则顺序、裁剪、
// 祖先仿射全得各写一份。

namespace {

/// 一批四边形 = 4 顶点 + 6 索引；提交层单批上限 16384 顶点。
constexpr std::size_t kMaxBatchVertices = 16384;
constexpr std::size_t kQuadsPerBatch = 4096;

constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

/// 顶点/索引暂存缓冲（复用容量，避免每帧一次分配）。
/// **UI 线程专用**：绘制全程在提交循环里，与 `image/InkingGeometryLayer.cpp`
/// 的 `geometryScratch()` 同一条约定。
std::vector<SDL_Vertex>& vertexScratch() {
    static std::vector<SDL_Vertex> scratch;
    return scratch;
}

std::vector<int>& indexScratch() {
    static std::vector<int> scratch;
    return scratch;
}

SDL_FColor ToFColor(std::uint32_t color) noexcept {
    constexpr float kInv = 1.0f / 255.0f;
    return SDL_FColor{static_cast<float>(InkingColorRed(color)) * kInv,
                      static_cast<float>(InkingColorGreen(color)) * kInv,
                      static_cast<float>(InkingColorBlue(color)) * kInv,
                      static_cast<float>(InkingColorAlpha(color)) * kInv};
}

/// 一条**实心**的轴对齐矩形（设计坐标，绝对）：天空 / 地面底色走这一条。
///
/// 为什么不复用 `fillShape`：它按 `int` 宽高填、还会画一圈羽化带，用来铺底
/// 是白花钱；而这里只要"四个顶点铺满一块"，抗锯齿没有意义（边界就是屏幕边）。
void appendSolidRect(float x, float y, float width, float height, float magnification,
                     std::uint32_t color, float pixelX, float pixelY) {
    if (!(width > 0.0f) || !(height > 0.0f)) {
        return;
    }
    const float x0 = x * magnification;
    const float y0 = y * magnification;
    const float x1 = (x + width) * magnification;
    const float y1 = (y + height) * magnification;

    SDL_Vertex vertices[4];
    const SDL_FColor tint = ToFColor(color);
    const float xs[4] = {x0, x1, x1, x0};
    const float ys[4] = {y0, y0, y1, y1};
    for (int i = 0; i < 4; ++i) {
        vertices[i].position = SDL_FPoint{xs[i], ys[i]};
        vertices[i].color = tint;
        vertices[i].tex_coord = SDL_FPoint{0.0f, 0.0f};
    }
    const int indices[6] = {0, 1, 2, 0, 2, 3};
    // `reused = false`：这是每帧重算的几何，没有"只做平移"这回事。
    inking_detail::AppendLocalGeometry(vertices, 4, indices, 6, pixelX, pixelY, false);
}

/// 格键：`(i, j)` → 一个 64 位整数。转无符号再拼，负索引也是良定义的。
std::uint64_t CellKey(InkingFieldCell cell) noexcept {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cell.i)) << 32)
         | static_cast<std::uint64_t>(static_cast<std::uint32_t>(cell.j));
}

/// 天空贴图单帧四边形上限（1.5）。取 `kQuadsPerBatch`：一批正好装得下，而
/// `Tile` 用 8×8 的贴图铺 1280×720 会到十万级——没有顶就等于让一次误配吃掉
/// 整帧的提交带宽。
constexpr std::uint32_t kMaxSkyQuads = 4096;

/// 一块"要铺到天空带里"的贴图：设计坐标矩形 + UV。裁剪前 / 后的形状是同一个。
struct SkyQuadRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
};

/**
 * @brief 把一块贴图矩形裁进天空带 `[0,0]–(bandWidth, bandHeight)`，并按比例修 UV。
 *
 * 为什么不用 `SDL_SetRenderClipRect`：那是**渲染器的全局状态**，要打断提交批、
 * 还得记住恢复；而天空带是轴对齐矩形，裁剪落在 UV 上就是四则运算。
 *
 * @return 裁空了返回 false（半个像素都不剩就别提交）。
 */
bool ClipToSkyBand(SkyQuadRect& quad, float bandWidth, float bandHeight) noexcept {
    if (!(quad.width > 0.0f) || !(quad.height > 0.0f)) {
        return false;
    }
    const float x0 = std::max(quad.x, 0.0f);
    const float y0 = std::max(quad.y, 0.0f);
    const float x1 = std::min(quad.x + quad.width, bandWidth);
    const float y1 = std::min(quad.y + quad.height, bandHeight);
    if (!(x1 > x0) || !(y1 > y0)) {
        return false;
    }
    // 每设计像素跨多少 UV：裁掉多少边，UV 就少多少边——两处用**同一个**比例，
    // 所以 Cover 放大后不会"图被压扁"（分开算就是各算各的、迟早差一点）。
    const float du = (quad.u1 - quad.u0) / quad.width;
    const float dv = (quad.v1 - quad.v0) / quad.height;
    const float newU0 = quad.u0 + (x0 - quad.x) * du;
    const float newV0 = quad.v0 + (y0 - quad.y) * dv;
    const float newU1 = quad.u0 + (x1 - quad.x) * du;
    const float newV1 = quad.v0 + (y1 - quad.y) * dv;

    quad.x = x0;
    quad.y = y0;
    quad.width = x1 - x0;
    quad.height = y1 - y0;
    quad.u0 = newU0;
    quad.v0 = newV0;
    quad.u1 = newU1;
    quad.v1 = newV1;
    return true;
}

/// 把一块（已裁剪的）天空四边形追加进提交缓冲：设计坐标 → 本地设备像素。
void appendSkyQuad(const SkyQuadRect& quad, float magnification) {
    std::vector<SDL_Vertex>& vertices = vertexScratch();
    std::vector<int>& indices = indexScratch();
    const float x0 = quad.x * magnification;
    const float y0 = quad.y * magnification;
    const float x1 = (quad.x + quad.width) * magnification;
    const float y1 = (quad.y + quad.height) * magnification;

    const SDL_FColor white =
        SDL_FColor{1.0f, 1.0f, 1.0f, 1.0f};  // 天空贴图不调制（没有 tint 需求）
    const float xs[4] = {x0, x1, x1, x0};
    const float ys[4] = {y0, y0, y1, y1};
    const float us[4] = {quad.u0, quad.u1, quad.u1, quad.u0};
    const float vs[4] = {quad.v0, quad.v0, quad.v1, quad.v1};

    const std::size_t base = vertices.size();
    for (int i = 0; i < 4; ++i) {
        SDL_Vertex vertex;
        vertex.position = SDL_FPoint{xs[i], ys[i]};
        vertex.color = white;
        vertex.tex_coord = SDL_FPoint{us[i], vs[i]};
        vertices.push_back(vertex);
    }
    const int local[6] = {0, 1, 2, 0, 2, 3};
    for (int value : local) {
        indices.push_back(static_cast<int>(base) + value);
    }
}

}  // namespace

InkingField::InkingField(InkingWidget* parent, const std::string& name)
    : InkingStaticWidget(parent, name) {
    // **不进命中表**：场地铺满全屏，进表就会挡住所有 UI。要点击走
    // `ClickAt`（由上层在 UI 未命中之后调）——"UI 优先"因此天然成立，不需要
    // 把两边的 z 混成一个数（UI 的顺序是"树序 + 兄弟 z"，不是可加的数）。
    SetHitPassThrough(true);

    // 设计尺寸是运行期常量：构造时取一份给布局层用，投影参数每帧重读（见 ensureCache）。
    _width = InkingDesignWidth();
    _height = InkingDesignHeight();
    _projection.designWidth = static_cast<float>(_width);
    _projection.designHeight = static_cast<float>(_height);
    _effective = _projection;
}

// ---------------------------------------------------------------------------
// 配置
// ---------------------------------------------------------------------------

void InkingField::SetProjection(const InkingFieldProjection& projection) {
    const char* why = nullptr;
    if (!InkingFieldValidate(projection, &why)) {
        // 保留旧参数：换成"默认值"会让画面莫名跳一下，而且没人知道是自己填错了。
        _projectionValid = false;
        INK_LOG_WARN(InkingFieldModuleName,
                     std::string("投影参数被拒（保留上一次那份）：")
                         + (why != nullptr ? why : "未说明"));
        return;
    }
    _projectionValid = true;
    _projection = projection;
    _projection.designWidth = static_cast<float>(InkingDesignWidth());
    _projection.designHeight = static_cast<float>(InkingDesignHeight());
    // 地平线覆盖生效时把**反解出来的 `farD` 写回**：`Projection()` 要如实反映
    // （不藏）。编辑器得让用户看见"farD 被地平线位置定住了"，否则用户会以为
    // 自己填的 farD 没生效是 bug（任务书 §5 第 3 条）。
    if (_projection.horizonRatio >= 0.0f) {
        _projection.farD = InkingFieldEffectiveFarD(_projection);
    }
    _width = InkingDesignWidth();
    _height = InkingDesignHeight();
    _cacheDirty = true;
    MarkRepaintDirty();
}

const InkingFieldProjection& InkingField::Projection() const noexcept {
    return _projection;
}

void InkingField::SetObjects(const InkingFieldObject* items, std::size_t count) {
    SetObjects(items, count, sizeof(InkingFieldObject));
}

void InkingField::SetObjects(const InkingFieldObject* items, std::size_t count,
                             std::size_t strideBytes) {
    // **只读视图**，不拷贝：命中回调要把指针还回业务那份实体（见头注释）。
    _objects = (count == 0) ? nullptr : items;
    _objectCount = (items == nullptr) ? 0 : count;
    _objectStride = strideBytes > 0 ? strideBytes : sizeof(InkingFieldObject);
    _cacheDirty = true;
    MarkRepaintDirty();
}

const InkingFieldObject& InkingField::objectAt(std::size_t index) const noexcept {
    const char* bytes =
        reinterpret_cast<const char*>(_objects) + index * _objectStride;
    return *reinterpret_cast<const InkingFieldObject*>(bytes);
}

void InkingField::SetTiles(const InkingFieldTile* tiles, std::size_t count) {
    _tiles = (count == 0) ? nullptr : tiles;
    _tileCount = (tiles == nullptr) ? 0 : count;
    _cacheDirty = true;
    MarkRepaintDirty();
}

void InkingField::SetSkyColor(std::uint32_t argb) {
    _skyColor = argb;
    MarkRepaintDirty();  // 颜色不标命中脏（标脏口径见 `InkingWidget` 文件头）
}

void InkingField::SetGroundColor(std::uint32_t argb) {
    _groundColor = argb;
    MarkRepaintDirty();
}

std::uint32_t InkingField::GetSkyColor() const noexcept {
    return _skyColor;
}

std::uint32_t InkingField::GetGroundColor() const noexcept {
    return _groundColor;
}

void InkingField::SetSkyTexture(InkingTextureHandle handle) {
    _skyTexture = handle;
    MarkRepaintDirty();  // 不影响命中（标脏口径见 `InkingWidget` 文件头）
    if (!handle.Valid()) {
        return;  // 回到纯色：那是回退路径，不算"设了不生效"
    }
    // 已经声明要透视、却仍然没有天空带（地平线在屏幕外）⇒ 现在就说，别等渲染。
    // **只在 `Oblique` 下判**：默认投影是 `Grid`，而"先设贴图、后设投影"是正常
    // 写法——那一步若也喊，等于给一个还没配置完的对象报错，纯噪声。
    // `Grid` 那一半留给 `submitTerrain`（那时投影已经定稿，喊了才准）。
    ensureCache();
    if (_effective.mode == InkingFieldMode::Oblique
        && !(InkingFieldHorizonY(_effective) > 0.0f)) {
        warnSkyTextureInertOnce();
    }
}

InkingShapeBounds InkingField::GetSkyBand() const noexcept {
    ensureCache();
    // 夹进画布：地平线可能在屏幕外（`horizonY < 0` 是"整屏都是地面"）。
    const float horizon = std::clamp(InkingFieldHorizonY(_effective), 0.0f,
                                     _effective.designHeight);
    return InkingShapeBounds{0.0f, 0.0f, _effective.designWidth, horizon};
}

void InkingField::warnSkyTextureInertOnce() const {
    if (_warnedSkyInert) {
        return;
    }
    _warnedSkyInert = true;
    INK_LOG_WARN(InkingFieldModuleName,
                 "天空贴图设了但不生效：当前没有天空带（格子模式 / 平行投影，"
                 "或地平线落在屏幕外）。画面照旧，贴图一个像素都不画"
                 "（见 AGENTS.md §7 field_sky_texture_inert）");
}

void InkingField::SetOnFieldClick(ClickHandler handler) {
    _onClick = std::move(handler);
}

std::size_t InkingField::GetObjectCount() const noexcept {
    return _objectCount;
}

std::size_t InkingField::GetTileCount() const noexcept {
    return _tileCount;
}

std::size_t InkingField::GetDrawnObjectCount() const noexcept {
    ensureCache();
    return _stats.objectsDrawn;
}

InkingField::Stats InkingField::GetStats() const noexcept {
    ensureCache();
    return _stats;
}

// ---------------------------------------------------------------------------
// 每帧缓存：投影 + 排序 + 1px 剔除（绘制与命中共用这一份）
// ---------------------------------------------------------------------------

bool InkingField::ancestorTransformActive() const noexcept {
    // 提交坐标是**绝对设计坐标**，祖先仿射会在提交层再叠一次 ⇒ 整块错位。
    // 只向上看，不向下（自己的子节点不存在）。
    for (InkingWidget* node = GetParent(); node != nullptr; node = node->GetParent()) {
        if (!node->GetSubtreeTransform().IsIdentity()) {
            return true;
        }
    }
    return false;
}

void InkingField::ensureCache() const {
    // 设计尺寸**每帧重读**（组件不自己存一份，两份尺寸不同步 = "位置差了一截"）。
    // 它放在脏判断之前是有意的：命中走的是 `_effective`，而命中随时可能发生，
    // 不该等"有人改了物体"才刷新。
    _effective = _projection;
    const int designWidth = InkingDesignWidth();
    const int designHeight = InkingDesignHeight();
    if (designWidth > 0) {
        _effective.designWidth = static_cast<float>(designWidth);
    }
    if (designHeight > 0) {
        _effective.designHeight = static_cast<float>(designHeight);
    }
    // **`horizonRatio` 不在这里覆盖，也绝不能被顺手覆盖**（任务书 T-04）：
    // 它是**比例**，不是像素——跟"画布多大"无关。一起覆盖的症状是"编辑器里对、
    // 跑起来不对"，而且看不出是哪一步改的。
    //
    // 反过来，`farD` 的**反解**必须发生在这个覆盖之后：地平线位置要落在这块
    // **运行期**画布上，`HorizonY` / `DepthAt` / `VisibleCells` 里都通过
    // `InkingFieldEffectiveFarD(_effective)` 现算，所以这里不需要写回 `farD`
    // （写回反而会在设计尺寸变化后留一个过期值）。

    if (!_cacheDirty) {
        return;
    }
    _cacheDirty = false;

    _stats.objectCount = static_cast<std::uint32_t>(_objectCount);
    _stats.tileCount = static_cast<std::uint32_t>(_tileCount);
    _stats.transformBlocked = 0;
    _cullCount = 0;
    _stats.projectionValid = _projectionValid ? 1u : 0u;
    _projected.clear();

    // `visibleCells` 不在这里清：它归 `submitTerrain`（枚举发生在提交时），
    // 在这里清会让"没物体但有瓦片"的那一帧读到 0。
    if (!_projectionValid || _objects == nullptr || _objectCount == 0) {
        _stats.objectsDrawn = 0;
        return;
    }

    // 排序排的是**索引数组**，不动数据本身；提交与命中共用这个序列（同源）。
    //
    // 键先抄到一段**连续**内存里再排：比较器直接读 `objectAt()` 时，每次比较都
    // 要按跨距算一次地址（派生结构的跨距还不是编译期常量），5000 个物体实测
    // 差出近 1 ms。"键"是纯数据，抄一遍不心疼。
    _keys.resize(_objectCount);
    for (std::size_t i = 0; i < _objectCount; ++i) {
        const InkingFieldObject& object = objectAt(i);
        _keys[i] = SortKey{object.d, object.lift, static_cast<std::uint32_t>(i)};
    }
    std::sort(_keys.begin(), _keys.end(), [](const SortKey& a, const SortKey& b) {
        if (a.d != b.d) {
            return a.d > b.d;  // 远的先画
        }
        if (a.lift != b.lift) {
            return a.lift < b.lift;  // 同深度：低处先画
        }
        // 末键必须是提交序号：否则同帧同输入的排序结果不可复现（"既会闪、又查
        // 不出来"）。
        return a.index < b.index;
    });

    _projected.reserve(_objectCount);
    const float zoom = _effective.camera.zoom;
    // 每点常量（三角函数 / 透视系数）整帧只算一次。
    const InkingFieldPrecomputed pre = InkingFieldPrecompute(_effective);
    for (const SortKey& key : _keys) {
        const std::uint32_t index = key.index;
        const InkingFieldObject& object = objectAt(index);
        if (!object.texture.Valid() || InkingColorAlpha(object.tint) == 0u) {
            // 看不见的不该能点，也不该白占提交带宽（句柄失效另有
            // `texture_handle_stale` 那条账）。
            continue;
        }

        // 锚点是**起点角**：世界 `(min x, min d)`，向 `+x / +d / +y` 展开
        // （与 `InkingFieldCellOf` 的 floor 约定同源）。
        float minX = object.x;
        float minD = object.d;
        if (object.anchor == InkingFieldAnchor::Center) {
            minX -= object.width * 0.5f;
            minD -= object.depth * 0.5f;
        }

        // 位置走**正向投影**（唯一那份算式）；尺寸乘缩放系数。
        const float projectedScale = InkingFieldScaleAt(_effective, minD);
        const float scale = object.followPerspective ? projectedScale : zoom;
        InkingDesignPos foot = InkingFieldProjectPrecomputed(
            _effective, pre, InkingFieldPos{minX, minD, object.lift});
        if (!object.followPerspective) {
            // 位置那一算里 `lift` 已经乘过 `s`，这里退回 1（"整个物体等比"那条：
            // 跟随与否全有或全无，不做"宽度跟随、高度不跟随"的半套）。
            foot.y += object.lift * (projectedScale - scale);
        }

        const float width = object.width * scale;
        const float height = object.height * scale;
        if (!(width >= 1.0f) || !(height >= 1.0f) || !std::isfinite(foot.x)
            || !std::isfinite(foot.y)) {
            // 1px 规则：亚像素继续画不是"看不出"，是几何在抖（闪烁、摩尔纹）。
            // 裁掉，但**要记账**（§7 `field_subpixel_culled`）。
            ++_cullCount;
            continue;
        }

        Projected entry;
        entry.rect = InkingShapeBounds{foot.x, foot.y - height, width, height};
        entry.centerX = foot.x + width * 0.5f;
        entry.centerY = foot.y - height * 0.5f;
        entry.rotation = object.rotation;
        entry.index = index;
        entry.visible = true;
        _projected.push_back(entry);
    }

    _stats.objectsDrawn = static_cast<std::uint32_t>(_projected.size());
    // 剔了几个只在**这一帧重建过**时才算"这一帧的账"，交给 `onRender` 落账。
    _cullFresh = true;
}

// ---------------------------------------------------------------------------
// 命中
// ---------------------------------------------------------------------------

bool InkingField::HitObject(float designX, float designY,
                            const InkingFieldObject** out) const {
    ensureCache();
    if (_objects == nullptr) {
        return false;
    }
    // **逆序扫**：提交序是远 → 近，反过来第一个命中的就是最上面那个。
    for (std::size_t k = _projected.size(); k > 0; --k) {
        const Projected& entry = _projected[k - 1];
        const InkingFieldObject& object = objectAt(entry.index);
        if (!entry.visible || !object.hitEnabled) {
            continue;
        }

        float localX = designX;
        float localY = designY;
        if (entry.rotation != 0.0f) {
            // 含旋转时绕中心**逆旋转一次**，再判轴对齐矩形。不用"世界盒再投影"
            // ——`followPerspective = false` 时两者会分叉。
            const float radians = -entry.rotation * kDegToRad;
            const float cosA = std::cos(radians);
            const float sinA = std::sin(radians);
            const float dx = designX - entry.centerX;
            const float dy = designY - entry.centerY;
            localX = entry.centerX + dx * cosA - dy * sinA;
            localY = entry.centerY + dx * sinA + dy * cosA;
        }
        const InkingShapeBounds& rect = entry.rect;
        if (localX >= rect.x && localX < rect.x + rect.width && localY >= rect.y
            && localY < rect.y + rect.height) {
            if (out != nullptr) {
                *out = &object;
            }
            return true;
        }
    }
    return false;
}

bool InkingField::HitCell(float designX, float designY, InkingFieldCell* out) const {
    ensureCache();
    // 设计画布之外没有地面可点：那里用户看不见，也就不该点得到。
    if (!(designX >= 0.0f) || designX >= _effective.designWidth || !(designY >= 0.0f)
        || designY >= _effective.designHeight) {
        return false;
    }
    InkingFieldPos ground;
    if (!InkingFieldGroundHit(_effective, InkingDesignPos{designX, designY}, &ground)) {
        return false;
    }
    // 1px 界之外的"格子"没有意义（那里几何本身在抖），不给点。
    const float limit = InkingFieldDepthLimitForPixel(_effective);
    if (std::isfinite(limit) && (ground.d - _effective.camera.d) > limit) {
        return false;
    }
    const InkingFieldCell cell = InkingFieldCellOf(_effective, ground);
    if (!InkingFieldCellInMap(_effective, cell)) {
        return false;
    }
    if (out != nullptr) {
        *out = cell;
    }
    return true;
}

bool InkingField::ClickAt(float designX, float designY) {
    const InkingFieldObject* hit = nullptr;
    if (!HitObject(designX, designY, &hit) || hit == nullptr) {
        return false;
    }
    InkingFieldCell cell;
    if (!HitCell(designX, designY, &cell)) {
        cell = InkingFieldCell{};
    }
    const InkingDesignPos at{designX, designY};
    if (_onClick) {
        _onClick(*hit, cell, at);
    }
    OnFieldClick(*hit, cell, at);
    return true;
}

// ---------------------------------------------------------------------------
// 提交
// ---------------------------------------------------------------------------

void InkingField::onRender(float pixelX, float pixelY) const {
    SDL_Renderer* renderer = inking_detail::currentRenderer();
    if (renderer == nullptr) {
        return;
    }
    ensureCache();

    // 「上一帧」口径：这一帧没重建投影 ⇒ 这一帧一个都没剔（不是"沿用上次的数"）。
    _stats.culledSubPixel = _cullFresh ? _cullCount : 0u;
    _cullFresh = false;
    _stats.terrainBatches = 0;
    _stats.objectBatches = 0;
    _stats.truncatedTiles = 0;
    _stats.skyQuads = 0;

    if (!_projectionValid) {
        return;
    }

    // 祖先带变换 ⇒ **整块不画**。画在错的地方比不画难查得多（整块错位会被
    // 当成"投影算错了"查半天），而这条护栏在 §7 有名字。
    if (ancestorTransformActive()) {
        _stats.transformBlocked = 1;
        if (!_warnedTransform) {
            _warnedTransform = true;
            INK_LOG_WARN(InkingFieldModuleName,
                         "场地的祖先链上有变换：提交坐标是绝对设计坐标，会让整块错位。"
                         "本帧不提交（见 AGENTS.md §7 field_parent_transform_drifts）");
        }
        return;
    }
    _stats.transformBlocked = 0;

    const float rawMagnification = GetMagnification();
    const float magnification = rawMagnification > 0.0f ? rawMagnification : 1.0f;

    submitTerrain(pixelX, pixelY, magnification, _stats);
    submitObjects(pixelX, pixelY, magnification, _stats);
}

void InkingField::submitTerrain(float pixelX, float pixelY, float magnification,
                                Stats& stats) const {
    const InkingFieldProjection& projection = _effective;
    const float height = projection.designHeight;
    const float width = projection.designWidth;

    // 天空 / 地面：两条**轴对齐**的实心带。地平线在任何投影下都是水平的
    // （`sy` 只跟纵深有关），所以这里不需要平行四边形。
    const float horizon = std::clamp(InkingFieldHorizonY(projection), 0.0f, height);
    if (horizon > 0.0f) {
        // 贴图优先、纯色回退，**两者不混用**（`SetSkyTexture` 头注释第 4 条）：
        // 叠一层半透明底色会让颜色偏掉，而且"偏多少"没人算得出来。
        const bool painted =
            _skyTexture.Valid()
            && submitSkyTexture(pixelX, pixelY, magnification, width, horizon, stats);
        if (!painted) {
            appendSolidRect(0.0f, 0.0f, width, horizon, magnification, _skyColor, pixelX,
                            pixelY);
        }
    } else if (_skyTexture.Valid()) {
        // 设了却一个像素都画不出来 ⇒ 必须发声（这个仓反复吃亏的那一类）。
        warnSkyTextureInertOnce();
    }
    if (horizon < height) {
        appendSolidRect(0.0f, horizon, width, height - horizon, magnification,
                        _groundColor, pixelX, pixelY);
    }

    const InkingFieldCellRange range = InkingFieldVisibleCells(projection);
    stats.visibleCells = range.valid
                             ? static_cast<std::uint32_t>(
                                   (static_cast<long long>(range.i1) - range.i0 + 1)
                                   * (static_cast<long long>(range.j1) - range.j0 + 1))
                             : 0u;
    if (!range.valid || _tiles == nullptr || _tileCount == 0) {
        return;
    }

    // 格 → 瓦片索引：每帧按可见格查表。线性扫 5000 块 × 240 格 = 120 万次比较，
    // 建一张表只是几十微秒，而且是**建一次查一帧**。
    std::unordered_map<std::uint64_t, std::uint32_t> index;
    index.reserve(_tileCount * 2);
    for (std::size_t t = 0; t < _tileCount; ++t) {
        const InkingFieldTile& tile = _tiles[t];
        if (!tile.texture.Valid() || InkingColorAlpha(tile.tint) == 0u) {
            continue;
        }
        // 一个格被声明两次时**后者赢**（与"后提交的在上"同一条直觉）。
        index[CellKey(tile.cell)] = static_cast<std::uint32_t>(t);
    }
    if (index.empty()) {
        return;
    }

    std::vector<SDL_Vertex>& vertices = vertexScratch();
    std::vector<int>& indices = indexScratch();
    vertices.clear();
    indices.clear();

    std::uint32_t quads = 0;
    std::uint32_t truncated = 0;
    const std::uint32_t capacity = kTerrainBatchPool * kQuadsPerBatch;

    // 纹理按句柄分段：同纹理连续的格子并成一次 draw（提交层按纹理切批）。
    std::uint32_t lastTextureId = 0;
    SDL_Texture* texture = nullptr;
    int textureWidth = 0;
    int textureHeight = 0;

    const auto flushChunk = [&]() {
        if (vertices.empty()) {
            return;
        }
        if (vertices.size() > kMaxBatchVertices) {
            // 分批算术写错才会到这里：**整块拒收并记账**，不做部分提交
            // （截断一半的三角形列表会画出"少一块但看着像对的"画面）。
            ++stats.rejectedBatchesTotal;
            vertices.clear();
            indices.clear();
            return;
        }
        if (texture == nullptr) {
            vertices.clear();
            indices.clear();
            return;
        }
        inking_detail::AppendTexturedGeometry(texture, vertices.data(), vertices.size(),
                                              indices.data(), indices.size(), pixelX,
                                              pixelY);
        ++stats.terrainBatches;
        vertices.clear();
        indices.clear();
    };

    for (std::int32_t j = range.j0; j <= range.j1; ++j) {
        for (std::int32_t i = range.i0; i <= range.i1; ++i) {
            const InkingFieldCell cell{i, j};
            const auto found = index.find(CellKey(cell));
            if (found == index.end()) {
                continue;
            }
            if (quads >= capacity) {
                ++truncated;
                continue;
            }
            const InkingFieldTile& tile = _tiles[found->second];

            if (tile.texture.id != lastTextureId || texture == nullptr) {
                flushChunk();  // 换纹理 = 批次边界
                lastTextureId = tile.texture.id;
                texture = inking_detail::ResolveTextureHandle(
                    tile.texture.id, inking_detail::currentRenderer(), &textureWidth,
                    &textureHeight);
                if (texture == nullptr || textureWidth <= 0 || textureHeight <= 0) {
                    continue;  // 坏句柄：跳过这一块，不连累别的
                }
            }

            // 四角：格四边形（斜投影下是平行四边形）。
            InkingDesignPos corners[4];
            InkingFieldCellQuad(projection, cell, corners);

            float u0 = 0.0f;
            float v0 = 0.0f;
            float u1 = 1.0f;
            float v1 = 1.0f;
            if (tile.src.width > 0.0f && tile.src.height > 0.0f) {
                const float invW = 1.0f / static_cast<float>(textureWidth);
                const float invH = 1.0f / static_cast<float>(textureHeight);
                u0 = tile.src.x * invW;
                v0 = tile.src.y * invH;
                u1 = (tile.src.x + tile.src.width) * invW;
                v1 = (tile.src.y + tile.src.height) * invH;
            }
            if (tile.flipX) {
                const float swap = u0;
                u0 = u1;
                u1 = swap;
            }
            if (tile.flipY) {
                const float swap = v0;
                v0 = v1;
                v1 = swap;
            }

            SDL_FPoint points[4];
            for (int corner = 0; corner < 4; ++corner) {
                points[corner] = SDL_FPoint{corners[corner].x * magnification,
                                            corners[corner].y * magnification};
            }
            const SDL_FPoint uvs[4] = {SDL_FPoint{u0, v0}, SDL_FPoint{u1, v0},
                                       SDL_FPoint{u1, v1}, SDL_FPoint{u0, v1}};

            const SDL_FColor color = ToFColor(tile.tint);
            const std::size_t base = vertices.size();
            for (int corner = 0; corner < 4; ++corner) {
                SDL_Vertex vertex;
                vertex.position = points[corner];
                vertex.color = color;
                vertex.tex_coord = uvs[corner];
                vertices.push_back(vertex);
            }
            const int local[6] = {0, 1, 2, 0, 2, 3};
            for (int value : local) {
                indices.push_back(static_cast<int>(base) + value);
            }
            ++quads;
            if (vertices.size() >= kMaxBatchVertices) {
                flushChunk();
            }
        }
    }
    flushChunk();

    stats.truncatedTiles = truncated;
    if (truncated > 0) {
        INK_LOG_WARN(InkingFieldModuleName,
                     "地形面层池满（" + std::to_string(kTerrainBatchPool) + " 批 × "
                         + std::to_string(kQuadsPerBatch) + " 四边形），截掉 "
                         + std::to_string(truncated) + " 块瓦片");
    }
}

/**
 * @brief 天空贴图：按 `skyFit` 铺进天空带（1.5）。
 *
 * 三条与别处不同的判断，都写在代码里而不是文档里：
 *   - **不用纹理的 wrap 模式做平铺**：`Tile` 每块一个独立四边形、UV 各自 0..1。
 *     wrap 是纹理对象的**全局状态**，为铺一块天空去改调用方那张纹理的
 *     addressing mode，等于把副作用留给别人（而且后端支持不一致）；
 *   - **裁剪在 UV 上做**（`ClipToSkyBand`），不碰渲染器的裁剪栈：那是全局状态，
 *     会打断提交批还得记住恢复；
 *   - **块数有顶**（`kMaxSkyQuads`）：穷举贴图铺屏会到十万级，顶到了发一条 WARN
 *     再停——"少画"必须记账，这一层所有静默失效都是这么来的。
 */
bool InkingField::submitSkyTexture(float pixelX, float pixelY, float magnification,
                                   float bandWidth, float bandHeight, Stats& stats) const {
    if (!_skyTexture.Valid() || !(bandWidth > 0.0f) || !(bandHeight > 0.0f)) {
        return false;
    }
    int textureWidth = 0;
    int textureHeight = 0;
    SDL_Texture* texture = inking_detail::ResolveTextureHandle(
        _skyTexture.id, inking_detail::currentRenderer(), &textureWidth, &textureHeight);
    if (texture == nullptr || textureWidth <= 0 || textureHeight <= 0) {
        // 句柄失效（换过渲染器 / 已 Release）：**退回纯色**，画面别开一个洞。
        // `staleSubmits` 已经记着这笔账（见 `ResolveTextureHandle`）。
        return false;
    }

    std::vector<SDL_Vertex>& vertices = vertexScratch();
    std::vector<int>& indices = indexScratch();
    vertices.clear();
    indices.clear();

    const float tw = static_cast<float>(textureWidth);
    const float th = static_cast<float>(textureHeight);
    std::uint32_t quads = 0;

    const auto push = [&](SkyQuadRect quad) {
        if (quads >= kMaxSkyQuads) {
            return;
        }
        if (!ClipToSkyBand(quad, bandWidth, bandHeight)) {
            return;
        }
        appendSkyQuad(quad, magnification);
        ++quads;
    };

    switch (_effective.skyFit) {
        case InkingSkyFit::Stretch:
            push(SkyQuadRect{0.0f, 0.0f, bandWidth, bandHeight, 0.0f, 0.0f, 1.0f, 1.0f});
            break;
        case InkingSkyFit::Tile: {
            // 1:1 平铺：从天空带左上角起，右 / 下边缘是半块就画半块（裁剪在
            // `push` 里做，所以"半块"不需要单独一条分支）。
            const int cols = static_cast<int>(std::ceil(bandWidth / tw));
            const int rows = static_cast<int>(std::ceil(bandHeight / th));
            const double wanted =
                static_cast<double>(cols) * static_cast<double>(rows);
            if (wanted > static_cast<double>(kMaxSkyQuads) && !_warnedSkyTileCap) {
                _warnedSkyTileCap = true;
                INK_LOG_WARN(
                    InkingFieldModuleName,
                    "天空贴图 Tile 需要 " + std::to_string(static_cast<long long>(wanted))
                        + " 块（贴图 " + std::to_string(textureWidth) + "×"
                        + std::to_string(textureHeight) + "，天空带 "
                        + std::to_string(static_cast<int>(bandWidth)) + "×"
                        + std::to_string(static_cast<int>(bandHeight)) + "），超过上限 "
                        + std::to_string(kMaxSkyQuads) + "：只画前 "
                        + std::to_string(kMaxSkyQuads) + " 块。换大一点的贴图或改用 Cover");
            }
            for (int row = 0; row < rows && quads < kMaxSkyQuads; ++row) {
                for (int col = 0; col < cols && quads < kMaxSkyQuads; ++col) {
                    push(SkyQuadRect{static_cast<float>(col) * tw,
                                     static_cast<float>(row) * th, tw, th, 0.0f, 0.0f, 1.0f,
                                     1.0f});
                }
            }
            break;
        }
        case InkingSkyFit::Cover:
        default: {
            // 等比放大到**同时覆盖**带宽与带高，居中：多出来的那一维由裁剪吃掉。
            const float scale = std::max(bandWidth / tw, bandHeight / th);
            const float drawWidth = tw * scale;
            const float drawHeight = th * scale;
            push(SkyQuadRect{(bandWidth - drawWidth) * 0.5f,
                             (bandHeight - drawHeight) * 0.5f, drawWidth, drawHeight, 0.0f,
                             0.0f, 1.0f, 1.0f});
            break;
        }
    }

    if (quads == 0) {
        return false;
    }
    inking_detail::AppendTexturedGeometry(texture, vertices.data(), vertices.size(),
                                          indices.data(), indices.size(), pixelX, pixelY);
    vertices.clear();
    indices.clear();
    stats.skyQuads = quads;
    return true;
}

void InkingField::submitObjects(float pixelX, float pixelY, float magnification,
                                Stats& stats) const {
    if (_projected.empty() || _objects == nullptr) {
        stats.objectBatches = 0;
        return;
    }

    std::vector<SDL_Vertex>& vertices = vertexScratch();
    std::vector<int>& indices = indexScratch();
    vertices.clear();
    indices.clear();

    // 纹理缓存**按段复用**：一批物体绝大多数共用一张图（同一张图集），
    // 每块都查一次表就是几千次哈希。失效的 id 也缓存（缓成 nullptr）。
    std::uint32_t cachedId = 0;
    SDL_Texture* cachedTexture = nullptr;
    int cachedWidth = 0;
    int cachedHeight = 0;
    bool haveCache = false;

    std::uint32_t batches = 0;
    const auto flushChunk = [&]() {
        if (vertices.empty() || cachedTexture == nullptr) {
            vertices.clear();
            indices.clear();
            return;
        }
        if (vertices.size() > kMaxBatchVertices) {
            ++stats.rejectedBatchesTotal;
            vertices.clear();
            indices.clear();
            return;
        }
        inking_detail::AppendTexturedGeometry(cachedTexture, vertices.data(),
                                              vertices.size(), indices.data(),
                                              indices.size(), pixelX, pixelY);
        ++batches;
        vertices.clear();
        indices.clear();
    };

    for (const Projected& entry : _projected) {
        const InkingFieldObject& object = objectAt(entry.index);
        if (!haveCache || object.texture.id != cachedId) {
            flushChunk();
            cachedTexture = inking_detail::ResolveTextureHandle(
                object.texture.id, inking_detail::currentRenderer(), &cachedWidth,
                &cachedHeight);
            cachedId = object.texture.id;
            haveCache = true;
            if (cachedTexture == nullptr || cachedWidth <= 0 || cachedHeight <= 0) {
                continue;  // 句柄失效：跳过这一块（`texture_handle_stale` 那条账）
            }
        }

        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = 1.0f;
        float v1 = 1.0f;
        if (object.src.width > 0.0f && object.src.height > 0.0f) {
            const float invW = 1.0f / static_cast<float>(cachedWidth);
            const float invH = 1.0f / static_cast<float>(cachedHeight);
            u0 = object.src.x * invW;
            v0 = object.src.y * invH;
            u1 = (object.src.x + object.src.width) * invW;
            v1 = (object.src.y + object.src.height) * invH;
        }
        if (object.flipX) {
            // **只翻 UV**，不翻几何（翻 x 会让锚点语义当场变掉）。
            const float swap = u0;
            u0 = u1;
            u1 = swap;
        }

        const InkingShapeBounds& rect = entry.rect;
        float xs[4] = {rect.x, rect.x + rect.width, rect.x + rect.width, rect.x};
        float ys[4] = {rect.y, rect.y, rect.y + rect.height, rect.y + rect.height};
        if (entry.rotation != 0.0f) {
            // 绕屏幕矩形中心转；祖先变换由提交层统一叠加（这里转两次就是转多了）。
            const float radians = entry.rotation * kDegToRad;
            const float cosA = std::cos(radians);
            const float sinA = std::sin(radians);
            for (int corner = 0; corner < 4; ++corner) {
                const float dx = xs[corner] - entry.centerX;
                const float dy = ys[corner] - entry.centerY;
                xs[corner] = entry.centerX + dx * cosA - dy * sinA;
                ys[corner] = entry.centerY + dx * sinA + dy * cosA;
            }
        }

        // 设计坐标 → 本地设备像素（平移交给提交层）。
        SDL_FPoint points[4];
        for (int corner = 0; corner < 4; ++corner) {
            points[corner] = SDL_FPoint{xs[corner] * magnification,
                                        ys[corner] * magnification};
        }
        const SDL_FPoint uvs[4] = {SDL_FPoint{u0, v0}, SDL_FPoint{u1, v0},
                                   SDL_FPoint{u1, v1}, SDL_FPoint{u0, v1}};

        const SDL_FColor color = ToFColor(object.tint);
        const std::size_t base = vertices.size();
        for (int corner = 0; corner < 4; ++corner) {
            SDL_Vertex vertex;
            vertex.position = points[corner];
            vertex.color = color;
            vertex.tex_coord = uvs[corner];
            vertices.push_back(vertex);
        }
        const int local[6] = {0, 1, 2, 0, 2, 3};
        for (int value : local) {
            indices.push_back(static_cast<int>(base) + value);
        }
        if (vertices.size() >= kMaxBatchVertices) {
            flushChunk();
        }
    }
    flushChunk();

    stats.objectBatches = batches;
}
