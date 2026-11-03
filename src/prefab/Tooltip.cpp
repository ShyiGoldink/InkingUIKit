#include <prefab/Tooltip.h>

#include <config/window_config.h>
#include <dataStruct/InkingTextRequest.h>
#include <detail/InkingGeometry.h>
#include <detail/InkingPopupPlacement.h>
#include <prefab/PrefabTheme.h>
#include <render/InkingDraw.h>
#include <render/InkingText.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

namespace inking_prefab {

namespace {

/// 面板数据：只用 `closed` 那一份（Tooltip 没有"开合"两态）。
InkingPanelData MakePanelData(const TooltipData& data) {
    InkingPanelData panel;
    panel.name = data.name.empty() ? "tooltip" : data.name;
    // 尺寸由文字量出来（`applyTextLayout` 会 `Resize`），这里给 1 只是为了
    // 不在构造期出现 0×0（那会触发零尺寸护栏，白白多一条 WARN）。
    panel.width = 1;
    panel.height = 1;
    panel.shape = InkingShapeSpec::RoundedRect(data.radius);
    panel.closed.color = data.panel.color;
    // Tooltip 没有开合态：两份写成同一个值，`openInheritsClosed` 也就无所谓了
    // （`Normalize()` 的"两态全透明兜成不透明黑"那条护栏因此不会误触发）。
    panel.open.color = data.panel.color;
    panel.openInheritsClosed = false;
    return panel;
}

}  // namespace

TooltipData TooltipData::MakeDefault() {
    TooltipData data;
    // 深色半透明底 + 白字：亮色界面里最省事、也最不容易和内容混的搭配。
    data.panel = InkingButtonAppearance(InkingAppearanceType::Color, 0xF0201F27u);
    data.textColor = 0xFFFFFFFFu;
    return data;
}

Tooltip::Tooltip(InkingWidget* parent, const TooltipData& data)
    : InkingDynamicPanel(parent, MakePanelData(data)), _data(data) {
    // 没人的时候不占绘制列表（也不画）。`ShowFor` 会把它变可见。
    SetVisible(false);
    // 兜底一道：本类从不参与命中，但把这条策略也写下来，免得以后有人只看
    // `HitTest` 就以为"还有 passThrough 这条路没配"。
    SetHitPassThrough(true);
}

bool Tooltip::ShowFor(InkingWidget* anchor, const std::string& text) {
    if (anchor == nullptr) {
        return false;  // 没锚点就没有"挂在谁下面"这回事（不猜）
    }
    _anchor = anchor;
    _text = text;
    _elapsed = 0.0f;
    _pending = true;
    _showing = false;
    // **先变可见**：不可见的节点收不到 `onAnimationTick`，计时就永远不走
    // （见头文件那段"计时期间必须可见"）。
    SetVisible(true);
    applyTextLayout();
    MarkRepaintDirty();
    return true;
}

void Tooltip::Hide() {
    if (!_pending && !_showing && !IsVisible()) {
        return;  // 幂等
    }
    _pending = false;
    _showing = false;
    _elapsed = 0.0f;
    _anchor = nullptr;
    SetVisible(false);
}

void Tooltip::applyTextLayout() {
    // ① 量文字（`InkingMeasureText` 不依赖渲染器：拿不到字体时给估算值，
    //    于是"没放字体"不会让提示变成 0 尺寸）。
    InkingTextRequest request;
    request.text = _text;
    request.fontSize = _data.fontSize;
    request.fontPath = _data.fontPath.empty() ? theme::DefaultFontPath() : _data.fontPath;
    const InkingTextMetrics metrics = InkingMeasureText(request);

    // ② 面板 = 文字 + 内边距；`maxWidth` **只夹面板**（这一版不换行）。
    const float maxPanelWidth = _data.maxWidth > 0.0f
                                    ? _data.maxWidth + _data.paddingX * 2.0f
                                    : metrics.width + _data.paddingX * 2.0f;
    const float width = std::min(metrics.width + _data.paddingX * 2.0f, maxPanelWidth);
    const float height = metrics.height + _data.paddingY * 2.0f;
    Resize(std::max(1, static_cast<int>(std::lround(width))),
           std::max(1, static_cast<int>(std::lround(height))));

    // ③ 落位：定点 = 锚点矩形**底部中点**，期望"向下展开、水平居中"。
    const float targetX = _anchor->GetAbsX() +
                          static_cast<float>(_anchor->GetWidth()) * 0.5f;
    const float targetY = _anchor->GetAbsY() +
                          static_cast<float>(_anchor->GetHeight()) + _data.gap;

    // 可用范围 = 父级矩形（挂在场景下时就是设计画布）。父级拿不到时退化成
    // 设计画布尺寸——两者在常规用法下是同一个矩形。
    InkingShapeBounds bounds{0.0f, 0.0f, static_cast<float>(InkingDesignWidth()),
                             static_cast<float>(InkingDesignHeight())};
    if (const InkingWidget* parent = GetParent()) {
        if (parent->GetWidth() > 0 && parent->GetHeight() > 0) {
            bounds.width = static_cast<float>(parent->GetWidth());
            bounds.height = static_cast<float>(parent->GetHeight());
        }
    }

    // 期望自锚点 `(0.5, 0)`：水平居中、顶边贴住定点（于是"向下展开"）。
    // 下方放不下时落位算术会翻到 `(0.5, 1)` = 挂在锚点上方，再不够就夹紧。
    const inking_detail::PopupPlacement placement = inking_detail::SolvePopupPlacement(
        targetX, targetY, GetWidth(), GetHeight(), bounds, InkingAnchorPoint{0.5f, 0.0f});

    ChangeSelfAnchor(placement.selfAnchor);

    // 落位给的是"相对父级左上角的位置"，这里换成 offset（与 `InkingFixedPopup`
    // 同一套换算）：GetX = parentW × traceAnchor.x − selfW × selfAnchor.x + offset
    const float selfW = static_cast<float>(GetWidth());
    const float selfH = static_cast<float>(GetHeight());
    float offsetX = placement.x;
    float offsetY = placement.y;
    if (const InkingWidget* parent = GetParent()) {
        offsetX += selfW * placement.selfAnchor.x -
                   static_cast<float>(parent->GetWidth()) * GetTraceAnchor().x;
        offsetY += selfH * placement.selfAnchor.y -
                   static_cast<float>(parent->GetHeight()) * GetTraceAnchor().y;
    }
    ChangeOffset(offsetX, offsetY);
}

void Tooltip::onAnimationTick(float deltaSeconds) {
    if (!_pending) {
        return;  // 空闲：本帧只付一次 bool 判断
    }
    // 负 delta（时钟回拨 / 第一帧）不倒着走，与 `InkingTimeline::Advance` 同一规矩。
    if (deltaSeconds > 0.0f) {
        _elapsed += deltaSeconds;
    }
    if (_elapsed < _data.delay) {
        return;
    }
    _pending = false;
    _showing = true;
    // 真正显示这一刻才落位：`ShowFor` 之后锚点可能被布局挪过（那时尺寸已定）。
    if (_anchor != nullptr) {
        applyTextLayout();
    }
    MarkRepaintDirty();
}

void Tooltip::paintText(float pixelX, float pixelY) const {
    SDL_Renderer* renderer = inking_detail::currentRenderer();
    if (renderer == nullptr || _text.empty()) {
        return;
    }
    const float magnification =
        GetMagnification() > 0.0f ? GetMagnification() : 1.0f;

    InkingTextRequest request;
    request.text = _text;
    request.fontSize = _data.fontSize;
    request.fontPath = _data.fontPath.empty() ? theme::DefaultFontPath() : _data.fontPath;
    request.color = _data.textColor;
    // 文字块位置 = 内边距（设计坐标，相对组件左上角）。
    request.offsetX = _data.paddingX;
    request.offsetY = _data.paddingY;

    inking_detail::PaintText(renderer, request, GetWidth(), GetHeight(), magnification,
                             pixelX, pixelY, GetDisplayTransform());
}

void Tooltip::onRender(float pixelX, float pixelY) const {
    if (!_showing) {
        // 计时期间**什么都不画**（但节点是可见的，见头文件"计时期间必须可见"）。
        return;
    }
    // 面板底（形状 / 描边 / 阴影）走动态面板那一份，不另写。
    InkingDynamicPanel::onRender(pixelX, pixelY);
    paintText(pixelX, pixelY);
}

}  // namespace inking_prefab
