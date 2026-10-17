#include <text/InkingStaticLabel.h>

// `detail::TransformBounds` 在源项目里住在**那个单体头** `include/ink/basic/InkingAnchor.h`
// 里，本文件是经 `text/InkingStaticLabel.h` -> `ink/basic/InkingAnchor.h` **传递**
// 拿到的。搬过来之后单体头被拆开（`TransformBounds` 落到了 `detail/InkingGeometry.h`），
// 而 `widget/InkingStaticWidget.h` 不再传递它——所以这里必须显式包含。
// **拆单体头时现形的就是这一类依赖；凡是调 `TransformBounds` 的 .cpp 都要自己带。**
#include <detail/InkingGeometry.h>
#include <render/InkingDraw.h>
#include <render/InkingText.h>
#include <text/InkingTextTable.h>

#include <SDL3/SDL.h>

#include <string>
#include <utility>

// 标签（A7）的实现。三处说明：
//
//   1. **数据驱动的构造**和面板 / 按钮走同一条路：基类只拿到名字，
//      尺寸与锚点是这之后从 data 取到的（不给它写进去，标签就是 0×0——
//      画不出来也点不到）。新增字段要记得在这儿搬一次（AGENTS §6 第 83 条）；
//   2. **排版缓存按"所有影响排版的字段"比**：文本、字号、行距、字体、对齐、
//      内缩、可用宽度、行数上限、换行与省略开关。少比一个，就会出现
//      "改了配置、画面没变"（§6 第 72 条那条教训）；
//   3. **测量与绘制共用同一份排版结果**：`MeasureLayout` 报的尺寸和
//      `onRender` 画出来的行必须来自同一次排版，否则"框多大"与"字排多少"
//      会在窄宽度上分叉。

namespace inking {

namespace {

/// 真测量：把文字层包成排版层要的回调（文字层含 SDL，所以只在 .cpp 里出现）。
TextExtent MeasureWithTextLayer(const std::string& text,
                                const TextStyle& style) {
    TextRequest request;
    request.text = text;
    request.fontSize = style.fontSize > 0.0f ? style.fontSize : 16.0f;
    request.fontPath = style.fontPath;
    const TextMetrics metrics = MeasureText(request);
    return TextExtent{metrics.width, metrics.height};
}

}  // namespace

InkingStaticLabel::InkingStaticLabel(InkingWidget* parent, const LabelData& data)
    : InkingStaticWidget(parent, data.name), _data(data) {
    _width = data.width > 0 ? data.width : 0;
    _height = data.height > 0 ? data.height : 0;
    _selfAnchor = data.selfAnchor;
    _traceAnchor = data.traceAnchor;
    _offsetX = data.offsetX;
    _offsetY = data.offsetY;
    _layout = data.layout;
    _layoutItem = data.layoutItem;

    SetVisible(data.visible);
    ChangeZIndex(data.zIndex);

    // 标签不吃点击：它常压在按钮 / 图片上，吃掉点击就会变成"看得见、点不动"。
    SetHitPassThrough(true);

    ClearDirty();
    NotifySceneStructureChanged();
}

InkingStaticLabel::InkingStaticLabel(InkingWidget* parent,
                                     const std::string& name)
    : InkingStaticWidget(parent, name) {
    _data.name = name;
    SetHitPassThrough(true);
    ClearDirty();
    NotifySceneStructureChanged();
}

bool InkingStaticLabel::SetText(std::string text) noexcept {
    if (_data.text == text) {
        return false;
    }
    _data.text = std::move(text);
    _cache.valid = false;

    // 尺寸可能变了 ⇒ 让**所在容器**重排（兄弟要让位）。
    // 不标命中脏：文字不参与命中（AGENTS §6 第 47 条）。
    RelayoutParent();
    MarkRepaintDirty();
    return true;
}

bool InkingStaticLabel::SetTextColor(std::uint32_t color) noexcept {
    if (_data.color == color) {
        return false;
    }
    _data.color = color;
    // 颜色走纹理调制（纹理烘成白字），所以**不用**让排版缓存失效。
    MarkRepaintDirty();
    return true;
}

bool InkingStaticLabel::CacheMatches(float available) const noexcept {
    const LayoutCache& cache = _cache;
    return cache.valid && cache.text == _data.text
        && cache.fontPath == _data.style.fontPath
        && cache.fontSize == _data.style.fontSize
        && cache.lineSpacing == _data.style.lineSpacing
        && cache.align == _data.style.align
        && cache.alignV == _data.style.alignV
        && cache.padding.left == _data.style.padding.left
        && cache.padding.top == _data.style.padding.top
        && cache.padding.right == _data.style.padding.right
        && cache.padding.bottom == _data.style.padding.bottom
        && cache.availableWidth == available
        && cache.maxLines == _data.maxLines && cache.wrap == _data.wrap
        && cache.overflow == _data.overflow
        // 语言版本（A7）：切语言 ⇒ 这里不匹配 ⇒ 重查串 + 重排。
        && cache.languageVersion == TextLanguageVersion();
}

const TextLayoutResult& InkingStaticLabel::LayoutForWidth(float available) const {
    if (CacheMatches(available)) {
        return _cache.result;
    }

    // 查表放在排版之前：`_data.text` 是**占位符或字面量**，排的是查出来的串。
    std::string resolved = ResolveText(_data.text);

    TextLayoutRequest request;
    request.text = resolved;
    request.style = _data.style;
    request.maxWidth = available > 0.0f ? available : 0.0f;
    request.maxLines = _data.maxLines;
    request.wrap = _data.wrap;
    request.overflow = _data.overflow;
    _cache.result = LayoutText(request, MeasureWithTextLayer);
    _cache.resolvedText = std::move(resolved);

    _cache.valid = true;
    _cache.text = _data.text;
    _cache.fontPath = _data.style.fontPath;
    _cache.fontSize = _data.style.fontSize;
    _cache.lineSpacing = _data.style.lineSpacing;
    _cache.align = _data.style.align;
    _cache.alignV = _data.style.alignV;
    _cache.padding = _data.style.padding;
    _cache.availableWidth = available;
    _cache.maxLines = _data.maxLines;
    _cache.wrap = _data.wrap;
    _cache.overflow = _data.overflow;
    _cache.languageVersion = TextLanguageVersion();
    return _cache.result;
}

const TextLayoutResult& InkingStaticLabel::GetTextLayout() const {
    return LayoutForWidth(static_cast<float>(_width));
}

LayoutSize InkingStaticLabel::MeasureLayout(
    const LayoutConstraints& constraints) const {
    // 有约束就按约束换行（父容器给了我多宽，我就按多宽排）；
    // 没约束就用配置宽度。两者都没有 → 不换行（宽度不限）。
    float available = constraints.maxWidth;
    if (available <= 0.0f) {
        available = static_cast<float>(_width);
    }
    const TextLayoutResult& laid = LayoutForWidth(available);

    LayoutSize size;
    size.width = laid.width;
    // 空文本时不把自己撑成 0 高：保持配置的高度（不然一个空标签会消失）。
    size.height = laid.height > 0.0f ? laid.height
                                     : static_cast<float>(_height);
    return size;
}

void InkingStaticLabel::onRender(float pixelX, float pixelY) const {
    SDL_Renderer* renderer = detail::currentRenderer();
    if (renderer == nullptr || _data.text.empty()) {
        return;
    }

    // 用**当前实际宽度**排版（A6 可能给了我一个与配置不同的宽度）。
    const TextLayoutResult& laid = LayoutForWidth(static_cast<float>(_width));
    // 画的是**查表之后**的串：行里的字节区间指向它，不是 `_data.text` 那个占位符。
    const std::string& resolved = _cache.resolvedText;
    if (resolved.empty()) {
        return;
    }
    const float magnification = GetMagnification();

    for (const TextLine& line : laid.lines) {
        if (line.begin >= line.end || line.end > resolved.size()) {
            continue;  // 空行（显式换行留下的）不画
        }
        TextRequest request;
        request.text = resolved.substr(line.begin, line.end - line.begin);
        if (line.hasEllipsis) {
            request.text += kTextEllipsis;
        }
        request.fontSize = _data.style.fontSize > 0.0f ? _data.style.fontSize
                                                      : 16.0f;
        request.fontPath = _data.style.fontPath;
        request.color = _data.color;
        request.offsetX = line.x;
        request.offsetY = line.y;
        // A13：外部显示变换（如果有人设过）叠在文字这一笔上。文字这一侧只有
        // 绘制——标签默认命中穿透（`SetHitPassThrough(true)`），所以"跟手"这件事
        // 在这里只体现为画面。
        detail::PaintText(renderer, request, _width, _height, magnification,
                          pixelX, pixelY, GetExternalTransform());
    }
}

ShapeBounds InkingStaticLabel::GetHitEnvelope() const {
    // 整块矩形（基类默认那一份）+ 外部显示变换：被挪走 / 放大之后，表里登记的
    // 包络必须跟着走，否则哪天标签改成吃点击时会"看着在这儿、点不到"
    // （`AGENTS.md` §6 第 42 条那条"保守包络"的纪律）。
    const ShapeBounds base{0.0f, 0.0f, static_cast<float>(_width),
                           static_cast<float>(_height)};
    return detail::TransformBounds(base, GetExternalTransform(),
                                   static_cast<float>(_width) * 0.5f,
                                   static_cast<float>(_height) * 0.5f);
}

}  // namespace inking
