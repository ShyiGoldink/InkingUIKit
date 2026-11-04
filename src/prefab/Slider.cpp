#include <prefab/Slider.h>

#include <detail/InkLog.h>
#include <theme/InkingTheme.h>
#include <detail/InkingHit.h>
#include <input/KeyboardInput.h>
#include <prefab/PrefabParts.h>
#include <prefab/PrefabTheme.h>
#include <render/InkingDraw.h>
#include <scene/InkingScene.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

namespace inking_prefab {

namespace {

constexpr const char* kModuleName = "Slider";

/// 浮点比较的容差。只在"判断跨度是不是 0"这类地方用，不参与值的比较
/// ——值的比较是**量化之后的逐位相等**（两边走同一个 `clampQuantize`）。
constexpr float kSpanEpsilon = 1e-6f;

/// 外观 → 填充色。
///
/// 第一版只走到颜色这一档（见头文件"第一版不做"）：`Svg` 在生成期就已经把
/// 几何转成形状、只留填充色，所以和 `Color` 同路；`Image` 需要部件级纹理层，
/// 这一版没接——构造时会**明确喊一条 WARN**（不静默画错）。
std::uint32_t FillColorOf(const InkingButtonAppearance& appearance) noexcept {
    // 配了 `themeColor` 就按当前主题重查——**不能只返回 `color`**：那样运行期
    // 换主题时滑块会留着旧色（"别的地方都变了，就这一条轨道没变"，不报错）。
    return InkingResolveThemeColor(appearance.themeColor, appearance.color);
}

}  // namespace

SliderData::SliderData() noexcept {
    // 默认值集中在这里（成员默认初始化器只写"语义上的 0"，尺寸要有值才画得出来）。
    width = 200;
    height = 28;
}

SliderData SliderData::MakeDefault() {
    SliderData data;
    // 亮色场景：沉一档的紫灰轨道 + 紫的已填充 + 白盘 + 1px 浅描边
    // （`PrefabTheme` 里那几个值就是"菜单 / 列表 / 滑块同一套设计语言"的落点）。
    data.track = InkingButtonAppearance(InkingAppearanceType::Color, theme::kSurfaceSunken);
    data.fill = InkingButtonAppearance(InkingAppearanceType::Color, theme::kAccent);
    data.thumb = InkingButtonAppearance(InkingAppearanceType::Color, theme::kSurface);
    data.thumbStroke.width = 1.0f;
    data.thumbStroke.color = theme::kOutline;
    return data;
}

Slider::Slider(InkingWidget* parent, const SliderData& data)
    : InkingStaticWidget(parent, static_cast<const InkingAnchorData&>(data)),
      _data(data),
      _value(data.value) {
    // 几何**走基类那条锚点通道**（宽高 / 锚点 / zIndex / visible 一次装好），
    // 不在这里手抄一遍字段——抄一遍就是"配置里写了、运行期没生效"的来源
    // （`AGENTS.md` §6 第 83 条）。

    normalizeRange();
    _value = clampQuantize(_data.value);

    // 部件级贴图没接线：**当场喊一声**，别等到"界面上是块纯色"才发现。
    // 每帧喊会刷屏，所以放在构造这一个时刻。
    const bool wantsImage = _data.track.type == InkingAppearanceType::Image ||
                            _data.fill.type == InkingAppearanceType::Image ||
                            _data.thumb.type == InkingAppearanceType::Image;
    if (wantsImage) {
        INK_LOG_WARN(std::string(kModuleName),
                     "Slider 的部件外观是 Image，但部件级纹理这一版没接：这三笔"
                     "会按各自 appearance.color 画成纯色。要贴图请在外面叠一个"
                     "InkingStaticImage，或等 SetPainter 第三档。控件名：" +
                     _data.name);
    }

    layoutParts();
}

void Slider::normalizeRange() noexcept {
    if (_data.max < _data.min) {
        std::swap(_data.min, _data.max);
    }
    if (_data.step < 0.0f) {
        _data.step = 0.0f;  // 负步长没有语义：当"连续"处理，不报错
    }
    // 轨道高 / 圆盘直径给 0 或负数时按组件高兜底：零高的轨道画不出来，
    // 而"什么都不显示"是最难查的那种失败（§6 第 30 / 43 条那一族）。
    if (!(_data.trackHeight > 0.0f)) {
        _data.trackHeight = static_cast<float>(_height) * 0.25f;
    }
    if (!(_data.thumbDiameter > 0.0f)) {
        _data.thumbDiameter = static_cast<float>(_height);
    }
}

float Slider::clampQuantize(float value) const noexcept {
    float v = value;
    if (v < _data.min) {
        v = _data.min;
    }
    if (v > _data.max) {
        v = _data.max;
    }
    if (_data.step > 0.0f) {
        // 量化**相对 min**，不是相对 0：`[-1, 1]` 步长 `0.5` 才能落在
        // -1 / -0.5 / 0 / 0.5 / 1 上；相对 0 取整的话 `min` 那个端点永远取不到。
        const float steps = std::round((v - _data.min) / _data.step);
        v = _data.min + steps * _data.step;
        // 量化可能把值顶出上界（例如 `max` 不是步长的整数倍）：再夹一次。
        if (v < _data.min) {
            v = _data.min;
        }
        if (v > _data.max) {
            v = _data.max;
        }
    }
    return v;
}

float Slider::GetRatio() const noexcept {
    const float span = _data.max - _data.min;
    if (!(span > kSpanEpsilon)) {
        return 0.0f;  // 退化区间：不给 NaN
    }
    float ratio = (_value - _data.min) / span;
    if (ratio < 0.0f) {
        ratio = 0.0f;
    }
    if (ratio > 1.0f) {
        ratio = 1.0f;
    }
    return ratio;
}

bool Slider::SetValue(float value) {
    const float next = clampQuantize(value);
    if (next == _value) {
        return false;  // 幂等：值没变就一个脏都不标、一条事件都不发
    }
    const float before = _value;
    _value = next;
    layoutParts();      // 填充段与圆盘的位置跟着变（轨道没变 ⇒ 命中表不用重烘）
    MarkRepaintDirty();  // 只标重绘脏：几何（组件框）一个字节没动
    changed.Emit(_value, before);
    return true;
}

float Slider::valueAtLocalX(float localX) const noexcept {
    const SliderParts& parts = GetParts();
    if (!(parts.track.width > 0.0f)) {
        return _data.min;
    }
    // 线性映射走**整条轨道**（任务书 §4 T-02 的"点轨道 25% 处 = 25%"这条判据：
    // 映射跨度若改成"扣掉两个圆盘半径"的内跨，25% 处会读出 22.5%，差两个 step）。
    const float ratio = (localX - parts.track.x) / parts.track.width;
    return _data.min + ratio * (_data.max - _data.min);
}

void Slider::layoutParts() const noexcept {
    const float width = static_cast<float>(GetWidth());
    const float height = static_cast<float>(GetHeight());
    const float trackHeight = _data.trackHeight;
    const float thumbDiameter = _data.thumbDiameter;
    const float ratio = GetRatio();

    // 轨道：横向铺满组件框、纵向居中。
    _parts.track = InkingShapeBounds{0.0f, (height - trackHeight) * 0.5f, width,
                                     trackHeight};

    // 填充段：与 `ProgressBar` 共用**同一个纯函数**（`PrefabParts.h`）。
    _parts.fill = FillBoundsInTrack(_parts.track, ratio);

    // 圆盘：圆心落在轨道的 `ratio` 处，再把它**水平**夹进轨道。
    // 垂直方向溢出是设计（直径 18 > 轨道高 6，见头文件 ①）。
    const float radius = thumbDiameter * 0.5f;
    const float centerX = _parts.track.x + _parts.track.width * ratio;
    float thumbX = centerX - radius;
    const float maxX = _parts.track.x + _parts.track.width - thumbDiameter;
    if (thumbX < _parts.track.x || maxX < _parts.track.x) {
        thumbX = _parts.track.x;  // 左端贴边 / 轨道比圆盘还窄时也不能越界
    } else if (thumbX > maxX) {
        thumbX = maxX;
    }
    _parts.thumb = InkingShapeBounds{thumbX, (height - thumbDiameter) * 0.5f,
                                     thumbDiameter, thumbDiameter};
}

const SliderParts& Slider::GetParts() const noexcept {
    // 每次现算：组件被布局通道拉伸之后（`GetWidth()` 变了）不会留一份过期几何。
    // 代价是几次浮点运算，比"值和几何对不上"便宜得多。
    layoutParts();
    return _parts;
}

bool Slider::HitTest(float worldX, float worldY) const {
    // 热区 = **整块组件框**（不是那 6 像素高的轨道条）：6px 在 1080p 下几乎点不中，
    // 而"圆盘不单独进命中"这条真意图由"没有第二个热区"满足（见文件头 ②）。
    // `HitTestLocalRect` 会先撤祖先变换、再减绝对位置、最后过自己变换的逆
    // ——与绘制读的是同一套坐标，所以"看着在框里、点却点不中"不会发生。
    const InkingShapeBounds box{0.0f, 0.0f, static_cast<float>(GetWidth()),
                                static_cast<float>(GetHeight())};
    return inking_detail::HitTestLocalRect(*this, box, GetDisplayTransform(), worldX,
                                           worldY);
}

void Slider::onRender(float pixelX, float pixelY) const {
    SDL_Renderer* renderer = inking_detail::currentRenderer();
    if (renderer == nullptr) {
        return;
    }
    // 展示倍率为 0 时用 1：除零会得到 inf，形状层会提交一堆退化顶点，
    // 表现是"这一帧什么都没画"（`InkingTextInput::onRender` 同一处理）。
    const float magnification =
        GetMagnification() > 0.0f ? GetMagnification() : 1.0f;
    const SliderParts& parts = GetParts();

    // ① 轨道（always）
    FillPart(renderer, _data.trackShape, parts.track, magnification, pixelX, pixelY,
             FillColorOf(_data.track));

    // ② 已填充段：**宽度为 0 时不画**（0 宽的三角化没有意义，还会多一条提交记录）。
    //    段与轨道同形状（填充是轨道的一段，形状必须同族，否则窄宽度下会露边）。
    FillPart(renderer, _data.trackShape, parts.fill, magnification, pixelX, pixelY,
             FillColorOf(_data.fill));

    // ③ 圆盘：填充 + 描边（描边压在填充上，顺序不能反）。
    FillPart(renderer, _data.thumbShape, parts.thumb, magnification, pixelX, pixelY,
             FillColorOf(_data.thumb));
    StrokePart(renderer, _data.thumbShape, parts.thumb, magnification, pixelX,
               pixelY, _data.thumbStroke);
}

void Slider::onPointerPress(InkingPointerButton button, bool press) {
    // 只有主键驱动滑块：右键抬起绝不能把正在进行的拖动收尾（同 `InkingStaticImage`
    // 那句），否则"右键看着什么都没做，滑块却不跟手了"。
    if (button != InkingPointerButton::Primary) {
        return;
    }
    if (press) {
        InkingScene* scene = GetScene();
        if (scene == nullptr) {
            return;  // 没有场景 = 拿不到按下点；等第一次拖动再补锚点
        }
        const float pointerX = scene->GetPointerX();
        // **按下当场跳过去**（点哪跳哪）：等第一次拖动再动的话，"点一下轨道"
        // 这件事就完全没有反馈。
        SetValue(valueAtLocalX(pointerX - GetAbsX()));
        _pressX = pointerX;
        _pressValue = _value;
        _dragging = true;
        return;
    }
    // 抬手（或指针移出窗口 / 失焦时框架补的那一次）：这次交互结束。
    if (_dragging) {
        _dragging = false;
        released.Emit();
    }
}

void Slider::onPointerDrag(float designX, float /*designY*/, float deltaX,
                           float /*deltaY*/) {
    if (!_dragging) {
        // 没收到按下（按下那一刻还没有场景）：按框架约定"第一次拖动的位移相对
        // **按下点**"反推锚点，否则第一帧就跳一大截（`InkingStaticImage` 同一处理）。
        _pressX = designX - deltaX;
        _pressValue = _value;
        _dragging = true;
    }
    const float trackWidth = GetParts().track.width;
    if (!(trackWidth > 0.0f)) {
        return;
    }
    // 累计位移（当前点 − 按下点），**不是**本次事件的增量（§6 第 96 条）：
    // 用增量的话除第一帧外每次都被算回原位，手感是"拉开就弹回去"。
    const float span = _data.max - _data.min;
    SetValue(_pressValue + (designX - _pressX) / trackWidth * span);
}

void Slider::onKeyDown(const InkingKeyEvent& key) {
    // 步长：配了 `step` 就用它；连续滑块按"全量程的 1%"走——没有它，键盘
    // 一次只能挪一个像素级的值，用户会觉得"按了没反应"。
    const float stepValue = _data.step > 0.0f
                                ? _data.step
                                : (_data.max - _data.min) / 100.0f;
    switch (key.key) {
        case SDLK_LEFT:
        case SDLK_DOWN:
            SetValue(_value - stepValue);
            break;
        case SDLK_RIGHT:
        case SDLK_UP:
            SetValue(_value + stepValue);
            break;
        case SDLK_HOME:
            SetValue(_data.min);
            break;
        case SDLK_END:
            SetValue(_data.max);
            break;
        default:
            break;  // 别的键不管：滑块不该吃掉焦点外的按键
    }
}

}  // namespace inking_prefab
