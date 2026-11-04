#include <prefab/ToastHost.h>

#include <button/ButtonPaint.h>
#include <config/window_config.h>
#include <dataStruct/InkingTextRequest.h>
#include <prefab/PrefabParts.h>
#include <prefab/PrefabTheme.h>
#include <render/InkingDraw.h>
#include <render/InkingText.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace inking_prefab {

namespace {

/// 一条通知：动态面板（会被重排 ⇒ 几何会变 ⇒ 必须走动态档）。
///
/// 放在 `.cpp` 的匿名命名空间里：它是**内部件**，外面拿不到它的类型，
/// 于是"调用方绕过宿主自己改某一条"这条路根本不存在。
class ToastItem : public InkingDynamicPanel {
public:
    ToastItem(ToastData style, std::string text, double expireAt, int id)
        : InkingDynamicPanel(nullptr, MakePanelData(style)),
          _style(std::move(style)),
          _text(std::move(text)),
          _expireAt(expireAt),
          _id(id) {}

    double ExpireAt() const noexcept { return _expireAt; }
    int Id() const noexcept { return _id; }

protected:
    void onRender(float pixelX, float pixelY) const override {
        InkingDynamicPanel::onRender(pixelX, pixelY);  // 底（形状 / 描边 / 阴影）
        SDL_Renderer* renderer = inking_detail::currentRenderer();
        if (renderer == nullptr || _text.empty()) {
            return;
        }
        const float magnification =
            GetMagnification() > 0.0f ? GetMagnification() : 1.0f;
        InkingTextRequest request;
        request.text = _text;
        request.fontSize = _style.fontSize;
        request.fontPath =
            _style.fontPath.empty() ? theme::DefaultFontPath() : _style.fontPath;
        request.color = _style.textColor;
        request.offsetX = _style.paddingX;
        request.offsetY = _style.paddingY;
        inking_detail::PaintText(renderer, request, GetWidth(), GetHeight(),
                                 magnification, pixelX, pixelY,
                                 GetDisplayTransform());
    }

private:
    static InkingPanelData MakePanelData(const ToastData& style) {
        InkingPanelData panel;
        panel.name = "toastItem";
        panel.width = std::max(1, static_cast<int>(std::lround(style.width)));
        panel.height = 1;  // 高度由宿主按文字量过再 `Resize`
        panel.shape = InkingShapeSpec::RoundedRect(style.radius);
        panel.closed.color = style.panel.color;
        panel.open.color = style.panel.color;
        panel.openInheritsClosed = false;
        return panel;
    }

    ToastData _style{};
    std::string _text;
    double _expireAt = 0.0;
    int _id = 0;
};

/// 宿主面板数据：铺满设计画布、**不画**（`onRender` 是空实现，这里只是几何）。
InkingPanelData MakeHostData(const ToastData& data) {
    InkingPanelData host;
    host.name = data.name.empty() ? "toastHost" : data.name;
    host.width = std::max(1, InkingDesignWidth());
    host.height = std::max(1, InkingDesignHeight());
    host.shape = InkingShapeSpec::Rect();
    // 颜色给一个不透明的值只是为了让 `Normalize()` 不把它兜成黑方块——
    // 反正 `onRender` 空实现，这一份一个像素也不会画。
    host.closed.color = 0xFF000000u;
    host.open.color = 0xFF000000u;
    host.openInheritsClosed = false;
    return host;
}

}  // namespace

ToastData ToastData::MakeDefault() {
    ToastData data;
    // 深色底 + 白字，与 Tooltip 同一套语言（两者都是"轻量提示"）。
    data.panel = InkingButtonAppearance(InkingAppearanceType::Color, 0xF0201F27u);
    data.textColor = 0xFFFFFFFFu;
    return data;
}

ToastHost::ToastHost(InkingWidget* parent, const ToastData& data)
    : InkingDynamicPanel(parent, MakeHostData(data)), _data(data) {
    // 一直可见（见头文件"宿主常驻"）：不可见就收不到每帧钩子，最后一条没人删。
    SetVisible(true);
    SetHitPassThrough(true);
}

int ToastHost::Show(const std::string& text, float seconds) {
    const float life = seconds > 0.0f ? seconds : 2.0f;
    auto item = std::make_unique<ToastItem>(_data, text, _now + life, _nextId);
    ToastItem* raw = static_cast<ToastItem*>(AddChild(std::move(item)));
    if (raw == nullptr) {
        return 0;  // `AddChild` 理论上不会失败；真失败就返回 0（不是合法 id）
    }
    const int id = _nextId;
    ++_nextId;
    relayoutToasts();
    return id;
}

std::size_t ToastHost::GetToastCount() const noexcept { return GetOwnedChildCount(); }

void ToastHost::Clear() {
    // 先把指针抄出来再删：`RemoveChild` 会改拥有表，边迭代边删是 UB。
    std::vector<InkingWidget*> items(GetChildren().begin(), GetChildren().end());
    for (InkingWidget* node : items) {
        if (node != nullptr) {
            RemoveChild(node);  // 返回值不接 = 立刻析构（拥有型语义）
        }
    }
    relayoutToasts();
}

void ToastHost::onAnimationTick(float deltaSeconds) {
    // 负 delta 不倒着走（与 `InkingTimeline::Advance` 同一规矩）。
    if (deltaSeconds > 0.0f) {
        _now += static_cast<double>(deltaSeconds);
    }

    // ① 先收集到期的那几条 —— **不在遍历中途删**（§6 第 24 条）。
    std::vector<ToastItem*> expired;
    for (InkingWidget* node : GetChildren()) {
        auto* item = static_cast<ToastItem*>(node);  // 子项只可能是 ToastItem
        if (item != nullptr && _now >= item->ExpireAt()) {
            expired.push_back(item);
        }
    }
    if (expired.empty()) {
        return;
    }

    // ② 记下 id（删完就读不到了），再统一删。
    std::vector<int> ids;
    ids.reserve(expired.size());
    for (ToastItem* item : expired) {
        ids.push_back(item->Id());
        RemoveChild(item);  // 不接返回值 = 让它当场析构
    }

    // ③ 重排剩下的（不留空位），再逐条发 `dismissed`。
    relayoutToasts();
    for (int id : ids) {
        dismissed.Emit(id);
    }
}

void ToastHost::relayoutToasts() {
    const float hostWidth = static_cast<float>(GetWidth());
    const float hostHeight = static_cast<float>(GetHeight());
    const float width = std::max(1.0f, _data.width);
    const float x = std::max(0.0f, hostWidth - _data.marginRight - width);

    // 文字高 → 单条高（量一遍就够：同一条文案不会变）。
    //
    // ⚠️ **必须给一个样本串**：`InkingMeasureText` 量的是"这段文字排出来多高"，
    // 空串量出来是 0，于是条高 = 0 + 上下留白 = 16 —— 文字被裁掉大半。
    // 行高只跟字号 / 字体有关、跟字数无关，所以固定样本就够（0.9.0 修）。
    InkingTextRequest request;
    request.text = "国";  // 任取一个会真的走排版的样本
    request.fontSize = _data.fontSize;
    request.fontPath =
        _data.fontPath.empty() ? theme::DefaultFontPath() : _data.fontPath;
    const InkingTextMetrics metrics = InkingMeasureText(request);
    const float height =
        std::max(1.0f, metrics.height + _data.paddingY * 2.0f);

    // 从下往上堆：**新的一律贴在最下面**（`GetChildren()` 是先挂的在前，
    // 所以倒过来走，让最后挂的那条拿到最低的位置）。
    float cursorY = hostHeight - _data.marginBottom;
    const std::vector<InkingWidget*>& children = GetChildren();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        auto* item = static_cast<ToastItem*>(*it);
        if (item == nullptr) {
            continue;
        }
        cursorY -= height;
        item->Resize(static_cast<int>(std::lround(width)),
                     static_cast<int>(std::lround(height)));
        item->ChangeOffset(x, std::max(0.0f, cursorY));
        cursorY -= _data.gap;
    }
    MarkRepaintDirty();
}

}  // namespace inking_prefab
