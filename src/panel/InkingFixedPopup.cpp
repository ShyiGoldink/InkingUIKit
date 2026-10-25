// 定点浮窗的实现（`include/panel/InkingFixedPopup.h`）。
//
// 三件事各占一段：落位（纯算术在 detail 里，这里只做换算与写回）、
// 外观（填充 + 阴影走绘制趟、描边走装饰趟，和面板同一套）、
// 行为（点外部关闭 + 焦点取回，都挂在指针变化通知上）。

#include <panel/InkingFixedPopup.h>

#include "PanelPaint.h"

#include <button/ButtonPaint.h>
#include <config/window_config.h>
#include <detail/InkLog.h>
#include <detail/InkingHit.h>
#include <detail/InkingPopupPlacement.h>
#include <render/InkingDraw.h>
#include <scene/InkingScene.h>

namespace inking {
namespace {

/// 模块名：日志用（每个模块一个，见 `PanelData.h` 的 `kPanelModuleName`）。
constexpr const char* kFixedPopupModuleName = "InkingFixedPopup";

/// `PanelData` → `AnchorData`：只搬定位 / 层级要读的那几个字段。
///
/// `visible` 固定写 false——浮窗是"打开才出现"，而 `visible` 是构造期字段；
/// 先关着，`OpenAt` 里再 `SetVisible(true)`。这个字段在这里写比"构造完再调一次
/// `SetVisible(false)`"少标一次脏（后者是一次骨架变化）。
AnchorData ToAnchorData(const PanelData& data) {
    AnchorData out;
    out.selfAnchor = data.selfAnchor;
    out.traceAnchor = data.traceAnchor;
    out.offsetX = data.offsetX;
    out.offsetY = data.offsetY;
    out.width = data.width;
    out.height = data.height;
    out.zIndex = data.zIndex;
    out.visible = false;
    out.color = data.closed.color;
    out.layout = data.layout;
    out.layoutItem = data.layoutItem;
    return out;
}

}  // namespace

InkingFixedPopup::InkingFixedPopup(InkingWidget* parent, const PanelData& data)
    : InkingDynamicWidget(parent, ToAnchorData(data)) {
    // 副本再 Normalize：调用方手写 `PanelData` 时常常不调它，而"两态全透明 ⇒
    // 品红兜底 + 一条 WARN"这条规矩对浮窗同样成立（否则一个看不见的浮窗
    // 会表现为"菜单打不开"）。
    PanelData normalized = data;
    normalized.Normalize();

    _shape = normalized.shape;
    _appearance = normalized.closed;  // 浮窗只有一份外观，`open` / `startOpen` 忽略

    if (!normalized.name.empty()) {
        SetName(normalized.name);
    }

    // 吃掉：浮窗语义固定，**不提供穿透开关**（"点透传到下层"未排期）。
    // 基类默认就是 false，这里写出来是为了让下一个读代码的人不用去查默认值。
    SetHitPassThrough(false);
}

// ---------------------------------------------------------------------------
// 定点
// ---------------------------------------------------------------------------

void InkingFixedPopup::OpenAt(float targetX, float targetY) {
    _targetX = targetX;
    _targetY = targetY;

    // 先重排再落位：内容可能是刚挂上去的，尺寸没定下来时落位全算错
    // （`SolvePopupPlacement` 拿到的是旧宽高）。这两步的顺序不能反。
    Relayout();
    applyPlacement();

    if (_open) {
        return;  // 已经开着：只挪位置，不重发 `opened`
    }

    _open = true;
    _probe.Reset();
    SetVisible(true);

    if (_takeFocusOnOpen) {
        if (InkingScene* scene = GetScene()) {
            scene->SetKeyboardFocus(this);
        } else {
            // 没有场景就拿不到键盘：不报的话表现为"菜单里的快捷键没反应"。
            INK_LOG_WARN(kFixedPopupModuleName,
                         "OpenAt 时不在任何场景下，拿不到键盘焦点：" + GetName());
        }
    }

    opened.Emit();
    onPopupOpened();
}

void InkingFixedPopup::SetTarget(float targetX, float targetY) {
    _targetX = targetX;
    _targetY = targetY;
    if (_open) {
        applyPlacement();
    }
}

void InkingFixedPopup::Close() {
    if (!_open) {
        return;
    }
    _open = false;

    // 焦点是场景级唯一归属：本子树拿着就必须**当场**还回去，不能留悬空。
    if (IsFocusInsideSubtree()) {
        if (InkingScene* scene = GetScene()) {
            scene->SetKeyboardFocus(nullptr);
        }
    }

    _probe.Reset();
    SetVisible(false);

    closed.Emit();
    onPopupClosed();
}

void InkingFixedPopup::applyPlacement() {
    // 自锚点固定从 (0,0) 起算（向右下展开），落位算法负责翻到对侧。
    const AnchorPoint preferred{0.0f, 0.0f};
    const detail::PopupPlacement placement = detail::SolvePopupPlacement(
        _targetX, _targetY, GetWidth(), GetHeight(), effectiveClampBounds(),
        preferred);

    ChangeSelfAnchor(placement.selfAnchor);

    // 落位给的是"相对父级左上角的位置"，这里换成 offset：
    //   GetX = parentW × traceAnchor.x − selfW × selfAnchor.x + offset
    // ⇒ offset = x − parentW × traceAnchor.x + selfW × selfAnchor.x
    //
    // 没有父级时 `GetX()` 退化成 `offset` 本身（锚点不参与），得单独走一条，
    // 否则会凭空多出 `selfW × selfAnchor.x` 这一段。
    const float selfW = static_cast<float>(GetWidth());
    const float selfH = static_cast<float>(GetHeight());
    const InkingWidget* parent = GetParent();

    float offsetX = placement.x;
    float offsetY = placement.y;
    if (parent != nullptr) {
        offsetX += selfW * placement.selfAnchor.x
                 - static_cast<float>(parent->GetWidth()) * GetTraceAnchor().x;
        offsetY += selfH * placement.selfAnchor.y
                 - static_cast<float>(parent->GetHeight()) * GetTraceAnchor().y;
    }

    ChangeOffset(offsetX, offsetY);

    _sideX = placement.sideX;
    _sideY = placement.sideY;
    _clampedX = placement.clampedX;
    _clampedY = placement.clampedY;
}

ShapeBounds InkingFixedPopup::effectiveClampBounds() const {
    if (_hasClampBounds) {
        return _clampBounds;
    }
    if (const InkingWidget* parent = GetParent()) {
        return ShapeBounds{0.0f, 0.0f, static_cast<float>(parent->GetWidth()),
                           static_cast<float>(parent->GetHeight())};
    }
    // 没有父级（理论上不会：浮窗总得挂在场景下）→ 按设计画布兜底。
    return ShapeBounds{0.0f, 0.0f, static_cast<float>(DesignWidth()),
                       static_cast<float>(DesignHeight())};
}

void InkingFixedPopup::SetClampBounds(const ShapeBounds& bounds) {
    _clampBounds = bounds;
    _hasClampBounds = true;
    if (_open) {
        applyPlacement();
    }
}

void InkingFixedPopup::ClearClampBounds() noexcept {
    _hasClampBounds = false;
    if (_open) {
        applyPlacement();
    }
}

// ---------------------------------------------------------------------------
// 内容
// ---------------------------------------------------------------------------

InkingWidget* InkingFixedPopup::AddItem(std::unique_ptr<InkingWidget> item) {
    if (item == nullptr) {
        return nullptr;
    }
    InkingWidget* raw = AddChild(std::move(item));
    if (raw != nullptr) {
        // 内容跟着容器走一层之上：只给容器高 z 会出现"背景在上、内容被盖"
        // （同 `MenuBar::AddItem` / `OptionBar::SetLayerZ`）。
        raw->ChangeZIndex(GetZIndex() + 1);
        // `AddChild` 末尾已经重排过一次，这里再喊一次是幂等的——但"加了一项"
        // 太容易忘，宁可在入口处多喊一次。
        Relayout();
    }
    return raw;
}

// ---------------------------------------------------------------------------
// 外观（和面板同一套：填充 + 阴影在绘制趟，描边在装饰趟）
// ---------------------------------------------------------------------------

bool InkingFixedPopup::HitTest(float worldX, float worldY) const {
    if (!IsVisibleInTree()) {
        return false;
    }
    // 和渲染共用同一份形状定义（`ShapeSpec`），所以"画在哪儿"与"点在哪儿"
    // 永远是同一个答案。浮窗没有显示变换，传单位变换。
    return detail::HitTestShape(*this, _shape, TransformSpec{}, worldX, worldY);
}

ShapeBounds InkingFixedPopup::GetHitEnvelope() const {
    // 整棵子树的并集：子孙可能溢出本体（下拉、提示气泡），包络要盖住它们。
    return detail::PanelHitEnvelope(*this, _shape, GetWidth(), GetHeight(),
                                    _appearance, _appearance);
}

void InkingFixedPopup::onRender(float pixelX, float pixelY) const {
    if (_appearance.shadow.Visible()) {
        detail::PaintSurfaceShadow(detail::currentRenderer(), *this, _shape,
                                   _appearance.shadow, TransformSpec{}, pixelX,
                                   pixelY);
    }
    detail::PaintPanelAppearance(*this, _shape, _appearance.color,
                                 TransformSpec{}, pixelX, pixelY);
}

void InkingFixedPopup::onRenderDecoration(float pixelX, float pixelY) const {
    // 描边压得住子孙（装饰遍在"子树画完、上层兄弟未画"时跑）。
    detail::PaintSurfaceStroke(detail::currentRenderer(), *this, _shape,
                               _appearance.stroke, TransformSpec{}, pixelX,
                               pixelY);
}

// ---------------------------------------------------------------------------
// 行为：点外部关闭 + 焦点取回（都挂在指针变化通知上）
// ---------------------------------------------------------------------------

void InkingFixedPopup::onPointerStateChanged() {
    if (!_open) {
        return;
    }
    InkingScene* scene = GetScene();
    if (scene == nullptr) {
        return;
    }

    // 指针不在窗口里一律不算"在身上"（窗口外不该继续悬停）——与动态按钮同一条。
    const bool hit = scene->IsPointerInside()
                  && HitTest(scene->GetPointerX(), scene->GetPointerY());
    const detail::PointerProbeResult r =
        _probe.Update(hit, scene->GetPointerDown());

    // 先判"外部按下"：它可能把浮窗关掉，关掉之后就不该再抢焦点。
    //
    // 判据是 `anyDownEdge && !hovered`——**不是** `pressed`：后者只表示"按下算在
    // 我身上"（捕获），用在别处按下时永远为假，那样菜单就永远关不掉。
    //
    // 用 `anyDownEdge` 而不是 `primary.downEdge`（0.5.0）：右键点外部也该关菜单，
    // 而且触摸长按在语义上就是次键——**这两种输入在菜单看来必须一样**。
    if (r.anyDownEdge && !r.hovered) {
        onOutsidePressed();
    }
    if (!_open) {
        return;
    }

    // 点内部按钮时焦点会被清空（按钮不想要键盘焦点），所以这里取回来。
    // 只在**指针变化**时做，不在 `onTick` 里每帧抢：鼠标停着就不反复夺焦点，
    // 同场景里别的组件也不会被无谓地打断。
    if (_takeFocusOnOpen && scene->GetKeyboardFocus() != this
        && !IsFocusInsideSubtree()) {
        scene->SetKeyboardFocus(this);
    }
}

void InkingFixedPopup::onOutsidePressed() {
    if (_closeOnOutside) {
        Close();
    }
}

bool InkingFixedPopup::IsFocusInsideSubtree() const {
    const InkingScene* scene = GetScene();
    if (scene == nullptr) {
        return false;
    }
    for (const InkingWidget* node = scene->GetKeyboardFocus(); node != nullptr;
         node = node->GetParent()) {
        if (node == this) {
            return true;
        }
    }
    return false;
}

}  // namespace inking
