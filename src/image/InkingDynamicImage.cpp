#include <image/InkingDynamicImage.h>

// 动态图片的实现（G-01）：静态版那份逐字复制过来，只把"几何写入"换成动态档的
// 写入口、把"场景推 onPointerDrag"换成"自己从 onPointerStateChanged 拉"。
// 头文件里写了两条差别的理由，这里不再重复。

#include <asset/InkingAsset.h>
#include <button/ButtonPaint.h>
#include <detail/InkLog.h>
// `detail::TransformBounds`（AABB 绕中心过一遍变换）与 `detail::HitTestShape` /
// `detail::HitTestLocalRect`：静态版同一个来源、同一套用法（见
// `src/image/InkingStaticImage.cpp` 顶部那段注释——这两个头必须显式包含，
// `InkingHit.h` 刻意只前向声明 `InkingWidget`）。
#include <detail/InkingGeometry.h>
#include <detail/InkingHit.h>
// `detail::WarnIfImageUndersampled`（0.7.1 T-04）：素材像素够不够这块屏幕。
// 只在 DEBUG 下要——Release 里连这个符号都不该出现在本 TU 里。
#if defined(INK_ISDEBUG)
#include <render/InkingText.h>
#endif
#include <scene/InkingScene.h>  // 指针状态 / 按下点：动态档的输入只有这一条来源

#include <utility>

#include <algorithm>
#include <cmath>

namespace inking {

#if defined(INK_ISDEBUG)
namespace {

/// DEBUG-only：素材够不够这块屏幕（与静态版逐字相同，0.7.1 T-04）。
/// 挂在"字节到手的三个入口"（构造 / `SetImageBytes` / `LoadAsset`）而不是逐帧。
void NoticeUndersampled(const ButtonAppearance& appearance, int designWidth,
                        int designHeight, const ShapeBounds& srcRect) {
    ImageBytes bytes;
    bytes.data = appearance.imageBytes;
    bytes.size = appearance.imageSize;
    bytes.name = appearance.source;
    // 图集那一帧的像素宽（全零 = 整张图）：比的是**这一段**，不是整张图集。
    const int frameWidth = srcRect.width > 0.0f
        ? static_cast<int>(std::lround(srcRect.width))
        : 0;
    const int frameHeight = srcRect.height > 0.0f
        ? static_cast<int>(std::lround(srcRect.height))
        : 0;
    detail::WarnIfImageUndersampled(bytes, designWidth, designHeight, frameWidth,
                                    frameHeight);
}

}  // namespace
#endif  // INK_ISDEBUG

// ---------------------------------------------------------------------------
// 拖拽 / 缩放用到的常量（与静态版同一组数）
// ---------------------------------------------------------------------------

namespace {

/// 右下角把手的边长（设计坐标）：按在这块里算"抓把手"，否则算"抓整块"。
constexpr float kImageHandleSize = 20.0f;

/// 缩放的夹取范围：防止拖成 0（看不见了）或者无限大（手感与内存都不好）。
/// 与静态版共用同一组值——两档的"手感"不该有差别。
constexpr float kImageMinScale = 0.2f;
constexpr float kImageMaxScale = 5.0f;

}  // namespace

// ---------------------------------------------------------------------------
// 构造
// ---------------------------------------------------------------------------

InkingDynamicImage::InkingDynamicImage(InkingWidget* parent,
                                       const ImageData& data)
    : InkingDynamicWidget(parent, data.name) {
    applyData(data);
}

void InkingDynamicImage::applyData(const ImageData& data) {
    // 拷一份再 Normalize：本类要求 `_data` 永远处于补齐状态（绘制那边按它取外观）。
    _data = data;
    _data.Normalize();

    // -----------------------------------------------------------------------
    // 几何：走**动态档的写入口**，而不是像静态版那样直接写基类的受保护成员
    //
    // 这正是两档最本质的差别：写入口自己会标脏、自己会通知场景（走 T-19 的分流），
    // 所以"改了几何忘了通知"这条路在动态档上根本不存在。
    //
    // 在构造函数体里调它们是安全的：写入口只标脏、只通知场景，不读派生类状态。
    // -----------------------------------------------------------------------
    Resize(_data.width > 0 ? _data.width : 0,
           _data.height > 0 ? _data.height : 0);
    ChangeSelfAnchor(_data.selfAnchor);
    ChangeTraceAnchor(_data.traceAnchor);
    ChangeOffset(_data.offsetX, _data.offsetY);

    // 布局**只搬 `_layoutItem`，不搬 `_layout`**——图片不当容器（与静态版同一条，
    // 判据是"它能不能当容器"，不是"字段有没有"）。
    _layoutItem = _data.layoutItem;

    // 可见性与层级走现成的写入口：它们自己会夹取、自己判"真的变了没有"，
    // 也就不可能和基类的规则分叉。
    SetVisible(_data.visible);
    ChangeZIndex(_data.zIndex);
    // 命中策略不是几何：它不标脏、不进命中表。
    SetHitPassThrough(_data.hitPassThrough);

    _imageName = _data.normal.source;
    refreshRenderAppearance();

    if (_data.runtimeAsset) {
        // 运行期那条路：字节不进 exe，构造时读一次。
        // 读不到**不中止构造**：给一张"只有底色"的图 + 一条错误日志。
        if (!LoadAsset(_data.normal.source)) {
            INK_LOG_ERROR(std::string(kImageModuleName),
                          "动态图运行期读图失败：" + _data.normal.source
                              + "（资源根 " + AssetRoot()
                              + "）；这张图暂时只有底色");
        }
    }
#if defined(INK_ISDEBUG)
    else {
        NoticeUndersampled(_renderAppearance, GetWidth(), GetHeight(),
                           _renderSourceRect);
    }
#endif

    // 构造期这批几何写入已经通知过场景了，没有"等着被消费"的改动。
    ClearDirty();
}

void InkingDynamicImage::refreshRenderAppearance() {
    _renderAppearance = _data.normal;
    if (!_assetBytes.empty()) {
        // 运行期读进来的字节：把视图指过去。**不能**改 `_data.normal`——
        // 那份是配置，改了就把"配置"和"当前显示"混在一起。
        _renderAppearance.type = AppearanceType::Image;
        _renderAppearance.imageBytes = _assetBytes.data();
        _renderAppearance.imageSize = _assetBytes.size();
        // **纹理缓存键必须跟着当前字节走**：不动它的话，同尺寸换图会一直命中
        // 第一张的纹理（帧动画表现为"每一帧都画第一帧"），而且不报错。
        _renderAppearance.source = _imageName;
    }
    // 切格信息**总是**从这里回填：上面整份拷贝的是 `normal`，而 `srcRect` 是
    // 运行期状态（换帧），不在配置里。
    _renderAppearance.srcRect = _renderSourceRect;
}

void InkingDynamicImage::SetRenderSourceRect(const ShapeBounds& srcRect) {
    _renderSourceRect = srcRect;
    _renderAppearance.srcRect = srcRect;
    MarkRepaintDirty();  // 画面变了；几何没动，所以不标命中脏
}

const ShapeBounds& InkingDynamicImage::GetRenderSourceRect() const noexcept {
    return _renderSourceRect;
}

// ---------------------------------------------------------------------------
// 查询（与静态版逐字相同：同一个形状、同一套变换语义）
// ---------------------------------------------------------------------------

bool InkingDynamicImage::HitTest(float worldX, float worldY) const {
    // 世界坐标 → 本地 → 形状判定，三步和绘制共用同一套定义。
    // 读的是**生效的显示变换**（A13：可能含外部拖动 / 缩放），不是配置里那一份。
    return detail::HitTestShape(*this, GetShape(), GetDisplayTransform(),
                                worldX, worldY);
}

const ShapeSpec& InkingDynamicImage::GetShape() const noexcept {
    return _data.shape;
}

const ButtonAppearance& InkingDynamicImage::GetAppearance() const noexcept {
    return _renderAppearance;
}

std::uint32_t InkingDynamicImage::GetDisplayColor() const noexcept {
    return _data.normal.color;
}

TransformSpec InkingDynamicImage::GetDisplayTransform() const noexcept {
    // A13：外部显示变换**叠在**配置那份之上。返回值而不是引用——合成结果不再
    // 指向 `_data` 里的字段。
    return CombineWithExternal(_data.normal.transform);
}

TransformSpec InkingDynamicImage::GetSubtreeTransform() const noexcept {
    // 和按钮 / 面板同一条约定：变换作用于整棵子树（图片自己不动，但可能被放在
    // 一个会动的容器里；这条让"图片里再挂东西"也画得对）。
    return CombineWithExternal(_data.normal.transform);
}

ShapeBounds InkingDynamicImage::GetHitEnvelope() const {
    // 复用按钮那份算法：它对图片自然退化成"这一态 + 它的变换"。
    const ShapeBounds base =
        detail::ButtonHitEnvelope(_data, GetShape(), GetWidth(), GetHeight());
    // A13：再套一层**外部变换**——不然图片被外部拖走之后，洞还留在老位置，
    // 拖过去就点不到。绕的是组件中心，和 `ApplyTransform` 的语义一致。
    return detail::TransformBounds(
        base, GetExternalTransform(), static_cast<float>(GetWidth()) * 0.5f,
        static_cast<float>(GetHeight()) * 0.5f);
}

const ImageData& InkingDynamicImage::GetData() const noexcept { return _data; }

const std::string& InkingDynamicImage::ImageName() const noexcept {
    return _imageName;
}

std::size_t InkingDynamicImage::AssetByteCount() const noexcept {
    return _assetBytes.size();
}

bool InkingDynamicImage::UsingAssetBytes() const noexcept {
    return !_assetBytes.empty();
}

int InkingDynamicImage::AssetLoadCount() const noexcept { return _assetLoadCount; }

// ---------------------------------------------------------------------------
// 运行期换图（与静态版同一条：读不到保留旧图，几何一个字不动）
// ---------------------------------------------------------------------------

bool InkingDynamicImage::LoadAsset(const std::string& relative) {
    ++_assetLoadCount;
    std::vector<std::uint8_t> bytes = AssetLoad(relative);
    if (bytes.empty()) {
        // 读不到：**保留旧图**。把它清空会让画面变成一个洞，
        // 而"缺一张图"和"这块本来就什么都没有"是两件事。
        return false;
    }
    _assetBytes = std::move(bytes);
    _imageName = relative;
    refreshRenderAppearance();
    MarkRepaintDirty();  // 画面变了；几何没动，所以不标命中脏
#if defined(INK_ISDEBUG)
    NoticeUndersampled(_renderAppearance, GetWidth(), GetHeight(),
                       _renderSourceRect);
#endif
    return true;
}

void InkingDynamicImage::SetImageBytes(const std::uint8_t* data,
                                       std::size_t size,
                                       const std::string& name) {
    if (data == nullptr || size == 0) {
        _assetBytes.clear();
        _imageName.clear();
    } else {
        // 拷一份自己拥有：调用方的缓冲可能是临时的（解码结果、栈上的数组）。
        _assetBytes.assign(data, data + size);
        _imageName = name;
    }
    refreshRenderAppearance();
    MarkRepaintDirty();
#if defined(INK_ISDEBUG)
    NoticeUndersampled(_renderAppearance, GetWidth(), GetHeight(),
                       _renderSourceRect);
#endif
}

// ---------------------------------------------------------------------------
// 拖拽 / 缩放（写**几何**，不是 A13）
// ---------------------------------------------------------------------------

void InkingDynamicImage::SetDraggable(bool draggable) noexcept {
    _draggable = draggable;
}

void InkingDynamicImage::SetResizable(bool resizable) noexcept {
    _resizable = resizable;
}

bool InkingDynamicImage::IsDraggable() const noexcept { return _draggable; }
bool InkingDynamicImage::IsResizable() const noexcept { return _resizable; }
bool InkingDynamicImage::IsDragging() const noexcept {
    return _dragArmed && _dragMoved;
}
bool InkingDynamicImage::IsScalingDrag() const noexcept { return _dragScaling; }

void InkingDynamicImage::resolveDragMode(float worldX, float worldY) noexcept {
    if (!_resizable) {
        _dragScaling = false;  // 没开缩放：一律当移动
        return;
    }
    const float width = static_cast<float>(GetWidth());
    const float height = static_cast<float>(GetHeight());
    const ShapeBounds handle{width - kImageHandleSize,
                             height - kImageHandleSize, kImageHandleSize,
                             kImageHandleSize};
    // 与**命中**同源：`HitTestLocalRect` 把世界坐标还原成本地坐标再问这个矩形
    // （撤祖先变换 → 减绝对位置 → 过**自己变换的逆**）。少最后那一步，
    // 缩放之后"看到的把手"与"算出来的把手"就错位（缩小时抓把手被判成移动）。
    _dragScaling = detail::HitTestLocalRect(*this, handle, GetDisplayTransform(),
                                            worldX, worldY);
}

void InkingDynamicImage::applyDrag(float worldX, float worldY) noexcept {
    // 累计位移（相对**按下点**，不是本次事件的增量）：增量式每次都要从基准
    // 重算，除了第一帧以外全被算回原位，表现是"稍微拉开一点就弹回去"。
    const float totalDx = worldX - _dragPressX;
    const float totalDy = worldY - _dragPressY;

    if (_dragScaling) {
        // 绕**左上角**均匀缩放：比例挂在"按下那一刻的尺寸"上，夹在 [0.2, 5]。
        const float startWidth =
            _dragStartWidth > 0 ? static_cast<float>(_dragStartWidth) : 1.0f;
        const float startHeight =
            _dragStartHeight > 0 ? static_cast<float>(_dragStartHeight) : 1.0f;
        float scale = 1.0f + totalDx / startWidth;
        scale = std::clamp(scale, kImageMinScale, kImageMaxScale);

        const int newWidth = static_cast<int>(std::lround(startWidth * scale));
        const int newHeight = static_cast<int>(std::lround(startHeight * scale));

        // 左上角不动是**算出来的**：`selfAnchor` 不是 (0,0) 时，尺寸变大会把
        // 左上角往负方向挪，所以补一段 offset —— `Δoffset = Δsize × selfAnchor`。
        // 少了这段，图片会"从两边一起长"，手感就不是"抓住右下角往外拉"。
        const int deltaWidth = newWidth - _dragStartWidth;
        const int deltaHeight = newHeight - _dragStartHeight;

        Resize(newWidth, newHeight);
        ChangeOffset(_dragStartOffsetX + static_cast<float>(deltaWidth) * _selfAnchor.x,
                     _dragStartOffsetY + static_cast<float>(deltaHeight) * _selfAnchor.y);
        _dragMoved = true;
        return;
    }

    if (_draggable) {
        // 整块移动：几何偏移 = 起点偏移 + 累计位移。写入口自己标脏。
        ChangeOffset(_dragStartOffsetX + totalDx, _dragStartOffsetY + totalDy);
        _dragMoved = true;
    }
}

// ---------------------------------------------------------------------------
// 输入：自己从场景拉（静态版由场景推 `onPointerDrag`）
// ---------------------------------------------------------------------------

void InkingDynamicImage::onPointerStateChanged() {
    InkingScene* scene = GetScene();
    if (scene == nullptr) {
        // 不在任何场景下就没有指针可读：退回初始，别带着一份过期的"按着"状态
        // 等下次登记回来。
        _probe.Reset();
        _dragArmed = false;
        _dragMoved = false;
        _dragScaling = false;
        return;
    }

    const bool inside = scene->IsPointerInside();
    // 按下用的是**原始按键状态**，不折进 inside：按着不放把指针拖出窗口仍然是
    // "按着"（`DispatchPointer` 那边也是这么用 `_pointerDown` 的）。
    const PointerDownState down = scene->GetPointerDown();
    // 命中自判：指针不在窗口里时一律算"不在图片上"。
    const bool hit =
        inside && HitTest(scene->GetPointerX(), scene->GetPointerY());

    const detail::PointerProbeResult r = _probe.Update(hit, down);

    // 按下那一刻定三件事：这次交互归不归我、是移动还是缩放、几何基准是多少。
    // **当场定格**（不等第一次拖动事件）：否则"按在把手上、手先动了一两像素"
    // 就可能被判成移动，用户会觉得"抓把手要试两次"。
    if (r.primary.pressed) {
        _dragMoved = false;
        _dragArmed = (_draggable || _resizable);
        if (_dragArmed) {
            // 按下点用场景记的**按下那一刻**的坐标，不用反推：反推在"按下之后
            // 同一帧里指针已经动了"的情况下会差一截。
            const MousePoint origin =
                scene->GetPressOrigin(PointerButton::Primary);
            _dragPressX = origin.x;
            _dragPressY = origin.y;
            _dragStartOffsetX = GetOffsetX();
            _dragStartOffsetY = GetOffsetY();
            _dragStartWidth = GetWidth();
            _dragStartHeight = GetHeight();
            resolveDragMode(origin.x, origin.y);
        }
    }

    if (r.primary.released) {
        // 抬手（或指针移出窗口 / 失焦时框架补的那一次）：这次交互结束。
        // 不清的话，下次按下会被当成"上一次还在拖"，模式判定就串了。
        _dragArmed = false;
        _dragMoved = false;
        _dragScaling = false;
        return;
    }

    // 捕获期间指针每一次变化都写一次几何。用**捕获**而不是"此刻在图上"：
    // 按下去之后滑出去仍然算拖（和静态版的手感一致）。
    if (_dragArmed && _probe.IsCaptured(PointerButton::Primary)) {
        applyDrag(scene->GetPointerX(), scene->GetPointerY());
    }
}

// ---------------------------------------------------------------------------
// 绘制（与静态版逐字相同：`PaintButtonAppearance` 那一笔）
// ---------------------------------------------------------------------------

void InkingDynamicImage::onRender(float pixelX, float pixelY) const {
    // 画什么由 `include/button/ButtonPaint.h` 提供——**和按钮同一份实现**。
    // A13：变换用**生效的那一份**（含外部拖动 / 缩放），和命中读的是同一个。
    detail::PaintButtonAppearance(*this, GetShape(), _renderAppearance,
                                  _data.stroke, _data.shadow,
                                  _data.normal.color, GetDisplayTransform(),
                                  pixelX, pixelY);
}

}  // namespace inking
