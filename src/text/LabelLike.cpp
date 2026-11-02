#include <text/LabelLike.h>

// 标签排版与取色的唯一实现（G-06 从 `InkingStaticLabel.cpp` 原样搬来）。
//
// 搬的时候只做了四处**机械替换**（没有任何语义改动，机械判据见任务书 G-06 的
// "证据"一节）：
//
//   1. 类名 `InkingStaticLabel::` → `InkingLabelLike::`；
//   2. `MeasureLayout` → `MeasureLabel`（那个名字是 `InkingWidget` 的虚函数，
//      覆盖必须留在组件里）；
//   3. `onRender` → `paintLabelText`，并把 `TextScrollX()` 换成传进来的 `scrollX`
//      （`InkingLabelLike` 不能有虚函数，问不到组件的那个钩子）；
//   4. `_width` / `_height` → `_host->GetWidth()` / `_host->GetHeight()`，
//      `GetMagnification()` / `GetExternalTransform()` → 经 `_host` 读。
//
// 为什么尺寸走宿主而不是自己存一份：排版与绘制都按**当前实际几何**排，而动态档的
// `Resize` 改的正是宿主几何；Like 里再存一份就是第二份真相（`InkingImageLike` 里那条
// "几何仍由两档各自装"的同一条纪律）。

#include <render/InkingDraw.h>
#include <render/InkingText.h>
#include <text/InkingTextTable.h>
#include <theme/InkingTheme.h>
#include <widget/InkingWidget.h>

#include <SDL3/SDL.h>

#include <string>
#include <utility>

namespace {

/// 真测量：把文字层包成排版层要的回调（文字层含 SDL，所以只在 .cpp 里出现）。
InkingTextExtent MeasureWithTextLayer(const std::string& text,
                                const InkingTextStyle& style) {
    InkingTextRequest request;
    request.text = text;
    request.fontSize = style.fontSize > 0.0f ? style.fontSize : 16.0f;
    request.fontPath = style.fontPath;
    request.fontPaths = style.fontPaths;
    const InkingTextMetrics metrics = InkingMeasureText(request);
    return InkingTextExtent{metrics.width, metrics.height};
}

}  // namespace

// ---------------------------------------------------------------------------
// 构造
// ---------------------------------------------------------------------------

InkingLabelLike::InkingLabelLike(InkingWidget& host, const InkingLabelData& data) noexcept
    : _data(data), _host(&host) {}

InkingLabelLike::InkingLabelLike(InkingWidget& host) noexcept : _host(&host) {}

// ---------------------------------------------------------------------------
// 数据手术（脏与重排留在两档的薄转发里）
// ---------------------------------------------------------------------------

bool InkingLabelLike::assignText(std::string text) noexcept {
    if (_data.text == text) {
        return false;
    }
    _data.text = std::move(text);
    _cache.valid = false;
    return true;
}

bool InkingLabelLike::assignTextColor(std::uint32_t color) noexcept {
    if (_data.color == color) {
        return false;
    }
    _data.color = color;
    // 手动设色 = 这个颜色**不再是语义色**：不清掉 token 的话，下一帧主题版本的
    // 比较会把用户刚设的颜色盖回去（界面表现为"设了没用"）。
    _data.colorToken = nullptr;
    _themeColor = color;
    _themeVersion = InkingThemeVersion();
    _themeColorValid = true;
    // 颜色走纹理调制（纹理烘成白字），所以**不用**让排版缓存失效。
    return true;
}

// ---------------------------------------------------------------------------
// 排版
// ---------------------------------------------------------------------------

std::uint32_t InkingLabelLike::EffectiveTextColor() const noexcept {
    const std::uint64_t version = InkingThemeVersion();
    if (!_themeColorValid || _themeVersion != version) {
        _themeVersion = version;
        _themeColor = InkingResolveThemeColor(_data.colorToken, _data.color);
        _themeColorValid = true;
    }
    return _themeColor;
}

bool InkingLabelLike::CacheMatches(float available,
                             float availableHeight) const noexcept {
    const LayoutCache& cache = _cache;
    return cache.valid && cache.text == _data.text
        && cache.fontPath == _data.style.fontPath
        && cache.fontPaths == _data.style.fontPaths
        && cache.fontSize == _data.style.fontSize
        && cache.lineSpacing == _data.style.lineSpacing
        && cache.align == _data.style.align
        && cache.alignV == _data.style.alignV
        && cache.padding.left == _data.style.padding.left
        && cache.padding.top == _data.style.padding.top
        && cache.padding.right == _data.style.padding.right
        && cache.padding.bottom == _data.style.padding.bottom
        && cache.availableWidth == available
        // 高度进缓存键（T-01）：它现在能改变"排几行"，不进键就是 §6 第 72 条
        // 那类"改了框高、画面没变"的静默失效。
        && cache.availableHeight == availableHeight
        && cache.maxLines == _data.maxLines && cache.wrap == _data.wrap
        && cache.overflow == _data.overflow
        // 语言版本（A7）：切语言 ⇒ 这里不匹配 ⇒ 重查串 + 重排。
        && cache.languageVersion == InkingTextLanguageVersion();
}

const InkingTextLayoutResult& InkingLabelLike::LayoutForWidth(float available,
                                                 float availableHeight) const {
    if (CacheMatches(available, availableHeight)) {
        return _cache.result;
    }

    // 查表放在排版之前：`_data.text` 是**占位符或字面量**，排的是查出来的串。
    std::string resolved = InkingResolveText(_data.text);

    InkingTextLayoutRequest request;
    request.text = resolved;
    request.style = _data.style;
    request.maxWidth = available > 0.0f ? available : 0.0f;
    // 框高就是上限（T-01，0.8.3）：`Clip` 是默认值却一直没人实现，于是 300×40
    // 的标签会把 7 行全画到框外。传 0 = 不限高（`MeasureLayout` 那条撑高的路
    // 必须传 0，否则"框多大"永远等于"一行高"）。
    request.maxHeight = availableHeight > 0.0f ? availableHeight : 0.0f;
    request.maxLines = _data.maxLines;
    request.wrap = _data.wrap;
    request.overflow = _data.overflow;
    _cache.result = InkingLayoutText(request, MeasureWithTextLayer);
    _cache.resolvedText = std::move(resolved);

    _cache.valid = true;
    _cache.text = _data.text;
    _cache.fontPath = _data.style.fontPath;
    _cache.fontPaths = _data.style.fontPaths;
    _cache.fontSize = _data.style.fontSize;
    _cache.lineSpacing = _data.style.lineSpacing;
    _cache.align = _data.style.align;
    _cache.alignV = _data.style.alignV;
    _cache.padding = _data.style.padding;
    _cache.availableWidth = available;
    _cache.availableHeight = availableHeight;
    _cache.maxLines = _data.maxLines;
    _cache.wrap = _data.wrap;
    _cache.overflow = _data.overflow;
    _cache.languageVersion = InkingTextLanguageVersion();
    return _cache.result;
}

const InkingTextLayoutResult& InkingLabelLike::GetTextLayout() const {
    return LayoutForWidth(static_cast<float>(_host->GetWidth()),
                          static_cast<float>(_host->GetHeight()));
}

InkingLayoutSize InkingLabelLike::MeasureLabel(const InkingLayoutConstraints& constraints) const {
    // 有约束就按约束换行（父容器给了我多宽，我就按多宽排）；
    // 没约束就用配置宽度。两者都没有 → 不换行（宽度不限）。
    float available = constraints.maxWidth;
    if (available <= 0.0f) {
        available = static_cast<float>(_host->GetWidth());
    }
    // **高度传 0**（T-01）：测量这条路不能自截，否则"框多高"永远等于"一行高"，
    // 内容驱动的高度就再也撑不开了。框高只约束**绘制**那一次排版。
    const InkingTextLayoutResult& laid = LayoutForWidth(available, 0.0f);

    InkingLayoutSize size;
    size.width = laid.width;
    // 空文本时不把自己撑成 0 高：保持配置的高度（不然一个空标签会消失）。
    size.height = laid.height > 0.0f ? laid.height
                                     : static_cast<float>(_host->GetHeight());
    return size;
}

// ---------------------------------------------------------------------------
// 绘制（文字那一笔）
// ---------------------------------------------------------------------------

void InkingLabelLike::paintLabelText(float pixelX, float pixelY, float scrollX) const {
    SDL_Renderer* renderer = inking_detail::currentRenderer();
    if (renderer == nullptr || _data.text.empty()) {
        return;
    }

    // 用**当前实际宽度**排版（A6 可能给了我一个与配置不同的宽度）；高度也一起
    // 传：装不下的行不画（T-01）——这是 `InkingTextOverflow::Clip` 的默认语义。
    const InkingTextLayoutResult& laid = LayoutForWidth(
        static_cast<float>(_host->GetWidth()),
        static_cast<float>(_host->GetHeight()));
    // 画的是**查表之后**的串：行里的字节区间指向它，不是 `_data.text` 那个占位符。
    const std::string& resolved = _cache.resolvedText;
    if (resolved.empty()) {
        return;
    }
    const float magnification = _host->GetMagnification();
    // 文字色：配了 `$token` 就按当前主题重查（运行期换肤），否则就是烘好的字面量。
    // 一次解析、整段复用——逐行解析只会让主题版本的比较白做几十遍。
    const std::uint32_t textColor = EffectiveTextColor();

    for (const InkingTextLine& line : laid.lines) {
        if (line.begin >= line.end || line.end > resolved.size()) {
            continue;  // 空行（显式换行留下的）不画
        }
        InkingTextRequest request;
        request.text = resolved.substr(line.begin, line.end - line.begin);
        if (line.hasEllipsis) {
            request.text += InkingTextEllipsis;
        }
        request.fontSize = _data.style.fontSize > 0.0f ? _data.style.fontSize
                                                      : 16.0f;
        request.fontPath = _data.style.fontPath;
        request.fontPaths = _data.style.fontPaths;
        request.color = textColor;
        // 水平滚动（T-12）：默认 0；输入框在文本超宽时给一个正值，把文字左移，
        // 好让光标停在本体里。排版与命中不受影响（那是未滚动的坐标）。
        request.offsetX = line.x - scrollX;
        request.offsetY = line.y;
        // A13：外部显示变换（如果有人设过）叠在文字这一笔上。文字这一侧只有
        // 绘制——标签默认命中穿透（`SetHitPassThrough(true)`），所以"跟手"这件事
        // 在这里只体现为画面。
        inking_detail::PaintText(renderer, request, _host->GetWidth(),
                          _host->GetHeight(), magnification, pixelX, pixelY,
                          _host->GetExternalTransform());
    }
}

