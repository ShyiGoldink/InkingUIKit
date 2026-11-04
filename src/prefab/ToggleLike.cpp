#include <prefab/ToggleLike.h>

#include <detail/InkingHit.h>
#include <prefab/PrefabParts.h>
#include <prefab/PrefabTheme.h>
#include <render/InkingDraw.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace inking_prefab {

namespace {

/// 勾的两段线之间的夹角由此处的"归一化端点"决定；这里只放**形状比例**，
/// 具体像素由 `mark` 框算出来——所以改控件尺寸不用改这几个数。
constexpr float kTickStartX = 0.14f;
constexpr float kTickStartY = 0.52f;
constexpr float kTickCornerX = 0.40f;
constexpr float kTickCornerY = 0.78f;
constexpr float kTickEndX = 0.86f;
constexpr float kTickEndY = 0.24f;
/// 勾的线宽（占 mark 短边的比例）。
constexpr float kTickThicknessRatio = 0.16f;

/// 一段**圆头线**：给内置勾用。
///
/// 为什么不写成 `PolygonPath`：路径顶点是**组件本地坐标**、不随框缩放
/// （`InkingShapeSpec::pathPoints` 的约定），于是"换个尺寸就得换一份点表"。
/// 线段 + 旋转没有这个问题：长度 = 两点距离、绕框中心转 `atan2` 那个角
/// （度、顺时针为正，与 `InkingTransformSpec::rotate` 一致）。
void DrawTickSegment(SDL_Renderer* renderer, float fromX, float fromY, float toX,
                     float toY, float thickness, std::uint32_t color,
                     float magnification, float pixelX, float pixelY) {
    const float dx = toX - fromX;
    const float dy = toY - fromY;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (!(length > 0.0f) || !(thickness > 0.0f)) {
        return;
    }
    const float midX = (fromX + toX) * 0.5f;
    const float midY = (fromY + toY) * 0.5f;

    InkingTransformSpec rotate;
    rotate.rotate = std::atan2(dy, dx) * 57.29577951308232f;  // 弧度 → 度

    // 线段形状是"水平的一条"：框宽 = 长度、框高 = 线宽，绕框中心（= 线段中点）转。
    const int width = std::max(1, static_cast<int>(std::lround(length)));
    const int height = std::max(1, static_cast<int>(std::lround(thickness)));
    inking_detail::fillShape(renderer, InkingShapeSpec::Line(thickness), width,
                             height, magnification,
                             pixelX + (midX - length * 0.5f) * magnification,
                             pixelY + (midY - thickness * 0.5f) * magnification,
                             rotate, color);
}

}  // namespace

// ---------------------------------------------------------------------------
// 数据与默认外观
// ---------------------------------------------------------------------------

ToggleData::ToggleData() noexcept {
    // 默认就是"能用的那一个"：18×18 的方框。外观那一份是黑的，
    // 用下面三个工厂才拿到主题色。
    width = 18;
    height = 18;
}

ToggleData MakeCheckboxData() {
    ToggleData data;
    data.name = "checkbox";
    data.width = 18;
    data.height = 18;
    data.baseShape = InkingShapeSpec::RoundedRect(4.0f);
    data.base = InkingButtonAppearance(InkingAppearanceType::Color, theme::kSurface);
    // 亮色场景里白底压在浅底上要靠 1px 描边站住（`PrefabTheme` 那条同款理由）。
    data.baseStroke.width = 1.0f;
    data.baseStroke.color = theme::kOutline;
    data.mark = InkingButtonAppearance(InkingAppearanceType::Color,
                                       theme::kAccentStrong);
    data.markInset = 0.18f;
    return data;
}

ToggleData MakeRadioData() {
    ToggleData data;
    data.name = "radio";
    data.width = 18;
    data.height = 18;
    data.baseShape = InkingShapeSpec::Circle();
    data.base = InkingButtonAppearance(InkingAppearanceType::Color, theme::kSurface);
    data.baseStroke.width = 1.0f;
    data.baseStroke.color = theme::kOutline;
    // 圆点比勾小一圈：`markInset` 0.30 ⇒ 点直径 = 40% 外径，是常见的那一档。
    data.markShape = InkingShapeSpec::Circle();
    data.mark = InkingButtonAppearance(InkingAppearanceType::Color, theme::kAccent);
    data.markInset = 0.30f;
    return data;
}

ToggleData MakeSwitchData() {
    ToggleData data;
    data.name = "switch";
    data.width = 40;
    data.height = 22;
    data.baseShape = InkingShapeSpec::Capsule();
    data.base = InkingButtonAppearance(InkingAppearanceType::Color,
                                       theme::kSurfaceSunken);
    data.markShape = InkingShapeSpec::Circle();
    data.mark = InkingButtonAppearance(InkingAppearanceType::Color, theme::kSurface);
    data.knobInset = 3.0f;
    return data;
}

// ---------------------------------------------------------------------------
// ToggleLike
// ---------------------------------------------------------------------------

ToggleLike::ToggleLike(InkingWidget* parent, const ToggleData& data)
    : InkingStaticWidget(parent, static_cast<const InkingAnchorData&>(data)),
      _data(data),
      _checked(data.checked) {
    // 几何走基类那条锚点通道（宽高 / 锚点 / zIndex / visible 一次装好）——
    // 与 `Slider` / `ProgressBar` 同一口径，不手抄字段（§6 第 83 条）。
    layoutParts();
}

bool ToggleLike::SetChecked(bool checked) {
    if (_checked == checked) {
        return false;  // 幂等：一个脏都不标、一条事件都不发
    }
    _checked = checked;
    layoutParts();      // Switch 的滑块位置跟着走（组件框本身没变）
    MarkRepaintDirty();  // 只标重绘脏：命中表与父容器布局都不该动
    // 子类钩子排在"已标脏"之后、"发事件"之前：Switch 在这里起过渡，
    // 订阅者在事件里读到的已经是最终那个 bool。
    onCheckedChanged(_checked);
    if (_checked) {
        clicked.Emit();
    } else {
        cancelled.Emit();
    }
    return true;
}

void ToggleLike::onCheckedChanged(bool /*checked*/) {
    // 默认什么都不做：只有 Switch 需要在这里起动画。
}

void ToggleLike::layoutParts() const noexcept {
    _parts.base = InkingShapeBounds{0.0f, 0.0f, static_cast<float>(GetWidth()),
                                    static_cast<float>(GetHeight())};
    layoutMark(_parts.base, _parts);
}

void ToggleLike::layoutMark(const InkingShapeBounds& base,
                            ToggleParts& parts) const {
    // 按比例内缩：于是"mark 落在 base 之内"是构造上成立的，不靠调用方配得对。
    float inset = _data.markInset;
    if (!(inset >= 0.0f)) {
        inset = 0.0f;  // 含 NaN
    }
    if (inset > 0.49f) {
        inset = 0.49f;  // 再大 mark 就退化成零尺寸了
    }
    const float smaller = std::min(base.width, base.height);
    const float size = std::max(0.0f, smaller * (1.0f - 2.0f * inset));
    parts.mark = InkingShapeBounds{base.x + (base.width - size) * 0.5f,
                                   base.y + (base.height - size) * 0.5f, size,
                                   size};
}

const ToggleParts& ToggleLike::GetParts() const noexcept {
    // 每次现算：Switch 的滑块位置会随动画变、组件也可能被布局拉伸。
    layoutParts();
    return _parts;
}

bool ToggleLike::HitTest(float worldX, float worldY) const {
    // 整块 base（不是只认勾）：勾只有几像素，靠它命中等于点不中。
    const InkingShapeBounds box{0.0f, 0.0f, static_cast<float>(GetWidth()),
                                static_cast<float>(GetHeight())};
    return inking_detail::HitTestLocalRect(*this, box, GetDisplayTransform(), worldX,
                                           worldY);
}

void ToggleLike::onPointerClick(InkingPointerButton button, float /*designX*/,
                                float /*designY*/) {
    if (button != InkingPointerButton::Primary) {
        return;  // 右键不改选中态
    }
    if (!AllowsCheckedChange()) {
        return;  // Radio：已选中就什么都不做（不翻转、不发事件）
    }
    SetChecked(!_checked);
}

void ToggleLike::onRender(float pixelX, float pixelY) const {
    SDL_Renderer* renderer = inking_detail::currentRenderer();
    if (renderer == nullptr) {
        return;
    }
    const float magnification =
        GetMagnification() > 0.0f ? GetMagnification() : 1.0f;
    const ToggleParts& parts = GetParts();

    // ① 底（框 / 圆 / 轨道）：填充 → 描边（顺序反了描边会被填充盖住）。
    FillPart(renderer, _data.baseShape, parts.base, magnification, pixelX, pixelY,
             _data.base.color);
    StrokePart(renderer, _data.baseShape, parts.base, magnification, pixelX,
               pixelY, _data.baseStroke);

    // ② mark（勾 / 圆点 / 滑块）：子类决定长什么样。**画不画由选中态决定**——
    // Checkbox / Radio 的"状态"就是勾 / 点在不在，不看 `_checked` 的话点一下
    // 只有事件记账、画面不动（0.9.0 修的就是这一条）。Switch 的滑块关着也在，
    // 靠 `PaintsMarkWhenUnchecked()` 覆写成真。
    if (_checked || PaintsMarkWhenUnchecked()) {
        paintMark(renderer, parts, magnification, pixelX, pixelY);
    }
}

void ToggleLike::paintMark(SDL_Renderer* renderer, const ToggleParts& parts,
                           float magnification, float pixelX,
                           float pixelY) const {
    FillPart(renderer, _data.markShape, parts.mark, magnification, pixelX, pixelY,
             _data.mark.color);
}

// ---------------------------------------------------------------------------
// Checkbox
// ---------------------------------------------------------------------------

Checkbox::Checkbox(InkingWidget* parent, const ToggleData& data)
    : ToggleLike(parent, data) {}

void Checkbox::paintMark(SDL_Renderer* renderer, const ToggleParts& parts,
                         float magnification, float pixelX,
                         float pixelY) const {
    const ToggleData& data = GetData();
    // `markShape` 不是默认哨兵（Rect）时按调用方给的那个形状画——"换勾的样式"
    // 于是只是换一个形状，不用等库支持新的勾法。
    if (data.markShape.kind != InkingShapeKind::Rect) {
        ToggleLike::paintMark(renderer, parts, magnification, pixelX, pixelY);
        return;
    }
    if (!(parts.mark.width > 0.0f) || !(parts.mark.height > 0.0f)) {
        return;
    }

    // 内置勾：两段圆头线，端点按 mark 框的比例取（改尺寸不用改这里）。
    const float smaller = std::min(parts.mark.width, parts.mark.height);
    const float thickness =
        std::max(1.0f, smaller * kTickThicknessRatio);  // 设计坐标
    const auto x = [&parts](float ratio) {
        return parts.mark.x + parts.mark.width * ratio;
    };
    const auto y = [&parts](float ratio) {
        return parts.mark.y + parts.mark.height * ratio;
    };

    // 短捺：左下 → 谷底；长捺：谷底 → 右上。
    DrawTickSegment(renderer, x(kTickStartX), y(kTickStartY), x(kTickCornerX),
                    y(kTickCornerY), thickness, data.mark.color, magnification,
                    pixelX, pixelY);
    DrawTickSegment(renderer, x(kTickCornerX), y(kTickCornerY), x(kTickEndX),
                    y(kTickEndY), thickness, data.mark.color, magnification,
                    pixelX, pixelY);
}

// ---------------------------------------------------------------------------
// Radio / RadioGroup
// ---------------------------------------------------------------------------

Radio::Radio(InkingWidget* parent, const ToggleData& data)
    : ToggleLike(parent, data) {}

Radio::~Radio() {
    // 组**不拥有**成员：成员死的时候自己退出，于是"先死谁"都安全
    // （组比成员先死那条路由 `~RadioGroup` 清 `_group`）。
    if (_group != nullptr) {
        _group->Remove(this);
        _group = nullptr;
    }
}

void Radio::SetGroup(RadioGroup* group) noexcept {
    if (_group == group) {
        return;  // 幂等
    }
    if (_group != nullptr) {
        _group->Remove(this);
    }
    _group = group;
    if (_group != nullptr) {
        _group->Add(this);
    }
}

void Radio::onPointerClick(InkingPointerButton button, float /*designX*/,
                           float /*designY*/) {
    if (button != InkingPointerButton::Primary) {
        return;
    }
    if (IsChecked()) {
        return;  // 单选不可反选：点了没反应，也不发事件
    }
    if (_group != nullptr) {
        _group->Select(this);  // 互斥 + 事件（本条的 clicked、别人的 cancelled）
        return;
    }
    // 没挂组：单用也安全——只是没有互斥。
    SetChecked(true);
}

RadioGroup::~RadioGroup() {
    // 先把成员搬出来再清空：`SetGroup(nullptr)` 会反手调 `Remove(this)`，
    // 直接在 `_radios` 上迭代就是"边迭代边 erase"。
    std::vector<Radio*> members = std::move(_radios);
    _radios.clear();
    for (Radio* radio : members) {
        if (radio != nullptr) {
            radio->SetGroup(nullptr);
        }
    }
}

void RadioGroup::Add(Radio* radio) {
    if (radio == nullptr) {
        return;
    }
    if (std::find(_radios.begin(), _radios.end(), radio) != _radios.end()) {
        return;  // 幂等
    }
    _radios.push_back(radio);
}

void RadioGroup::Remove(Radio* radio) {
    const auto it = std::find(_radios.begin(), _radios.end(), radio);
    if (it != _radios.end()) {
        _radios.erase(it);
    }
}

void RadioGroup::Select(Radio* radio) {
    if (radio == nullptr ||
        std::find(_radios.begin(), _radios.end(), radio) == _radios.end()) {
        return;  // 不在组里：什么都不做（调用点多，容错比拼字符串便宜）
    }
    // **先清其它的、再选目标**：反过来会有一帧"两个同时选中"被画出来
    // （任务书 §5 T-04 第 5 条）。`Remove` 不在这一趟里发生，所以迭代是安全的。
    for (Radio* member : _radios) {
        if (member != nullptr && member != radio && member->IsChecked()) {
            member->SetChecked(false);  // 各发一条 cancelled
        }
    }
    radio->SetChecked(true);  // 发 clicked（如果它本来就是选中的，则什么都不发）
}

Radio* RadioGroup::GetChecked() const noexcept {
    for (Radio* radio : _radios) {
        if (radio != nullptr && radio->IsChecked()) {
            return radio;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Switch
// ---------------------------------------------------------------------------

Switch::Switch(InkingWidget* parent, const ToggleData& data)
    : ToggleLike(parent, data) {
    // 初始位置与初始状态对齐：构造出来的开关不该"先滑一下再到位"。
    _knobRatio = IsChecked() ? 1.0f : 0.0f;
}

void Switch::SetAnimated(bool animated) noexcept {
    if (_animated == animated) {
        return;  // 幂等
    }
    _animated = animated;
    if (!_animated) {
        // 关掉过渡时**立刻摆到目标位置**：不然滑块会停在半路，等下一次
        // `onAnimationTick` 才动——而那条路正是被关掉的那条。
        _animating = false;
        const float target = IsChecked() ? 1.0f : 0.0f;
        if (_knobRatio != target) {
            _knobRatio = target;
            MarkRepaintDirty();
        }
    }
}

void Switch::onCheckedChanged(bool /*checked*/) {
    if (!_animated) {
        // 不带过渡：直接到位（`SetChecked` 已经标过重绘脏了，这里不再标）。
        _knobRatio = IsChecked() ? 1.0f : 0.0f;
        return;
    }
    _animating = true;
}

void Switch::onAnimationTick(float deltaSeconds) {
    // 每渲染帧所有节点都会收到这个钩子，所以第一件事就是**尽快退出**。
    if (!_animating) {
        return;
    }
    const float target = IsChecked() ? 1.0f : 0.0f;
    const float step = (deltaSeconds > 0.0f ? deltaSeconds : 0.0f) /
                       kTransitionSeconds;
    if (_knobRatio < target) {
        _knobRatio = std::min(target, _knobRatio + step);
    } else if (_knobRatio > target) {
        _knobRatio = std::max(target, _knobRatio - step);
    }
    MarkRepaintDirty();  // 只有画面变了（几何一个字节没动）
    if (_knobRatio == target) {
        _animating = false;  // 到位就停：下一帧起这个钩子只剩一次 bool 判断
    }
}

void Switch::layoutMark(const InkingShapeBounds& base, ToggleParts& parts) const {
    const float diameter = std::max(1.0f, base.height - 2.0f * _data.knobInset);
    const float travel = base.width - 2.0f * _data.knobInset - diameter;
    float ratio = _knobRatio;
    if (!(ratio > 0.0f)) {
        ratio = 0.0f;  // 含 NaN
    } else if (ratio > 1.0f) {
        ratio = 1.0f;
    }
    const float x = base.x + _data.knobInset + (travel > 0.0f ? travel * ratio : 0.0f);
    parts.mark = InkingShapeBounds{x, base.y + (base.height - diameter) * 0.5f,
                                   diameter, diameter};
}

}  // namespace inking_prefab
