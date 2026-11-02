#include <image/InkingDynamicImage.h>

// 动态图片的实现（G-01；G-02 抽取纯重构）：数据、资源与观测转发到 InkingImageLike；
// 几何写入口与"自己从 onPointerStateChanged 拉输入"仍留在本档。
// 头文件里写了两条差别的理由，这里不再重复。

#include <asset/InkingAsset.h>
#include <button/ButtonPaint.h>
#include <detail/InkLog.h>
// 命中与拖动把手共用同一套本地坐标还原；资源与包络的共用实现已归 InkingImageLike（G-02）。
#include <detail/InkingHit.h>
#include <scene/InkingScene.h>  // 指针状态 / 按下点：动态档的输入只有这一条来源

#include <algorithm>
#include <cmath>

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
                                       const InkingImageData& data)
    : InkingDynamicWidget(parent, data.name),
      InkingImageLike(static_cast<InkingWidget&>(*this)) {
    applyData(data);
}

void InkingDynamicImage::applyData(const InkingImageData& data) {
    // G-02：配置与资源状态归 InkingImageLike；几何仍走动态档的公开写入口。
    resetImageData(data);
    const InkingImageData& config = GetData();

    // -----------------------------------------------------------------------
    // 几何：走**动态档的写入口**，而不是像静态版那样直接写基类的受保护成员
    //
    // 这正是两档最本质的差别：写入口自己会标脏、自己会通知场景（走 T-19 的分流），
    // 所以"改了几何忘了通知"这条路在动态档上根本不存在。
    //
    // 在构造函数体里调它们是安全的：写入口只标脏、只通知场景，不读派生类状态。
    // -----------------------------------------------------------------------
    Resize(config.width > 0 ? config.width : 0,
           config.height > 0 ? config.height : 0);
    ChangeSelfAnchor(config.selfAnchor);
    ChangeTraceAnchor(config.traceAnchor);
    ChangeOffset(config.offsetX, config.offsetY);

    // 布局**只搬 `_layoutItem`，不搬 `_layout`**——图片不当容器（与静态版同一条，
    // 判据是"它能不能当容器"，不是"字段有没有"）。
    _layoutItem = config.layoutItem;

    // 可见性与层级走现成的写入口：它们自己会夹取、自己判"真的变了没有"，
    // 也就不可能和基类的规则分叉。
    SetVisible(config.visible);
    ChangeZIndex(config.zIndex);
    // 命中策略不是几何：它不标脏、不进命中表。
    SetHitPassThrough(config.hitPassThrough);

    if (config.runtimeAsset) {
        // 运行期那条路：字节不进 exe，构造时读一次。
        // 读不到**不中止构造**：给一张"只有底色"的图 + 一条错误日志。
        if (!LoadAsset(config.normal.source)) {
            INK_LOG_ERROR(std::string(InkingImageModuleName),
                          "动态图运行期读图失败：" + config.normal.source
                              + "（资源根 " + InkingAssetRoot()
                              + "）；这张图暂时只有底色");
        }
    }
#if defined(INK_ISDEBUG)
    else {
        noticeUndersampled();
    }
#endif

    // 构造期这批几何写入已经通知过场景了，没有"等着被消费"的改动。
    ClearDirty();
}

void InkingDynamicImage::refreshRenderAppearance() {
    InkingImageLike::refreshRenderAppearance();
}

void InkingDynamicImage::SetRenderSourceRect(const InkingShapeBounds& srcRect) {
    InkingImageLike::SetRenderSourceRect(srcRect);
    MarkRepaintDirty();  // 画面变了；几何没动，所以不标命中脏
}

const InkingShapeBounds& InkingDynamicImage::GetRenderSourceRect() const noexcept {
    return InkingImageLike::GetRenderSourceRect();
}

// ---------------------------------------------------------------------------
// 查询（G-02：数据与包络转发到 InkingImageLike，变换仍由动态档自己合成）
// ---------------------------------------------------------------------------

bool InkingDynamicImage::HitTest(float worldX, float worldY) const {
    // 世界坐标 → 本地 → 形状判定，三步和绘制共用同一套定义。
    // 读的是**生效的显示变换**（A13：可能含外部拖动 / 缩放），不是配置里那一份。
    return inking_detail::HitTestShape(*this, GetShape(), GetDisplayTransform(),
                                worldX, worldY);
}

const InkingShapeSpec& InkingDynamicImage::GetShape() const noexcept {
    return InkingImageLike::GetShape();
}

const InkingButtonAppearance& InkingDynamicImage::GetAppearance() const noexcept {
    return InkingImageLike::GetAppearance();
}

std::uint32_t InkingDynamicImage::GetDisplayColor() const noexcept {
    return InkingImageLike::GetDisplayColor();
}

InkingTransformSpec InkingDynamicImage::GetDisplayTransform() const noexcept {
    // A13：外部显示变换**叠在**配置那份之上。返回值而不是引用——合成结果不再
    // 指向配置里的字段。
    return CombineWithExternal(GetData().normal.transform);
}

InkingTransformSpec InkingDynamicImage::GetSubtreeTransform() const noexcept {
    // 和按钮 / 面板同一条约定：变换作用于整棵子树（图片自己不动，但可能被放在
    // 一个会动的容器里；这条让"图片里再挂东西"也画得对）。
    return CombineWithExternal(GetData().normal.transform);
}

InkingShapeBounds InkingDynamicImage::GetHitEnvelope() const {
    return InkingImageLike::GetHitEnvelope();
}

const InkingImageData& InkingDynamicImage::GetData() const noexcept {
    return InkingImageLike::GetData();
}

const std::string& InkingDynamicImage::ImageName() const noexcept {
    return InkingImageLike::ImageName();
}

std::size_t InkingDynamicImage::AssetByteCount() const noexcept {
    return InkingImageLike::AssetByteCount();
}

bool InkingDynamicImage::UsingAssetBytes() const noexcept {
    return InkingImageLike::UsingAssetBytes();
}

int InkingDynamicImage::AssetLoadCount() const noexcept {
    return InkingImageLike::AssetLoadCount();
}

// ---------------------------------------------------------------------------
// 运行期换图（G-02：共用实现不标脏，动态档在这里补重绘脏）
// ---------------------------------------------------------------------------

bool InkingDynamicImage::LoadAsset(const std::string& relative) {
    if (!InkingImageLike::LoadAsset(relative)) {
        return false;
    }
    MarkRepaintDirty();  // 画面变了；几何没动，所以不标命中脏
#if defined(INK_ISDEBUG)
    noticeUndersampled();
#endif
    return true;
}

void InkingDynamicImage::SetImageBytes(const std::uint8_t* data,
                                       std::size_t size,
                                       const std::string& name) {
    InkingImageLike::SetImageBytes(data, size, name);
    MarkRepaintDirty();
#if defined(INK_ISDEBUG)
    noticeUndersampled();
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
    const InkingShapeBounds handle{width - kImageHandleSize,
                             height - kImageHandleSize, kImageHandleSize,
                             kImageHandleSize};
    // 与**命中**同源：`HitTestLocalRect` 把世界坐标还原成本地坐标再问这个矩形
    // （撤祖先变换 → 减绝对位置 → 过**自己变换的逆**）。少最后那一步，
    // 缩放之后"看到的把手"与"算出来的把手"就错位（缩小时抓把手被判成移动）。
    _dragScaling = inking_detail::HitTestLocalRect(*this, handle, GetDisplayTransform(),
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
    const InkingPointerDownState down = scene->GetPointerDown();
    // 命中自判：指针不在窗口里时一律算"不在图片上"。
    const bool hit =
        inside && HitTest(scene->GetPointerX(), scene->GetPointerY());

    const inking_detail::PointerProbeResult r = _probe.Update(hit, down);

    // 按下那一刻定三件事：这次交互归不归我、是移动还是缩放、几何基准是多少。
    // **当场定格**（不等第一次拖动事件）：否则"按在把手上、手先动了一两像素"
    // 就可能被判成移动，用户会觉得"抓把手要试两次"。
    if (r.primary.pressed) {
        _dragMoved = false;
        _dragArmed = (_draggable || _resizable);
        if (_dragArmed) {
            // 按下点用场景记的**按下那一刻**的坐标，不用反推：反推在"按下之后
            // 同一帧里指针已经动了"的情况下会差一截。
            const InkingMousePoint origin =
                scene->GetPressOrigin(InkingPointerButton::Primary);
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
    if (_dragArmed && _probe.IsCaptured(InkingPointerButton::Primary)) {
        applyDrag(scene->GetPointerX(), scene->GetPointerY());
    }
}

// ---------------------------------------------------------------------------
// 绘制（与静态版逐字相同：`PaintButtonAppearance` 那一笔）
// ---------------------------------------------------------------------------

void InkingDynamicImage::onRender(float pixelX, float pixelY) const {
    // 画什么由 `include/button/ButtonPaint.h` 提供——**和按钮同一份实现**。
    // A13：变换用**生效的那一份**（含外部拖动 / 缩放），和命中读的是同一个。
    inking_detail::PaintButtonAppearance(*this, GetShape(), GetAppearance(),
                                  GetData().stroke, GetData().shadow,
                                  GetDisplayColor(), GetDisplayTransform(),
                                  pixelX, pixelY);
}

