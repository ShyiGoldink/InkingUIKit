#include <prefab/ProgressBar.h>

#include <detail/InkLog.h>
#include <prefab/PrefabParts.h>
#include <prefab/PrefabTheme.h>
#include <render/InkingDraw.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace inking_prefab {

namespace {

constexpr const char* kModuleName = "ProgressBar";

}  // namespace

ProgressBarData::ProgressBarData() noexcept {
    // 默认值集中在这里（成员默认初始化器只写"语义上的 0"，尺寸要有值才画得出来）。
    width = 200;
    height = 8;
}

ProgressBarData ProgressBarData::MakeDefault() {
    ProgressBarData data;
    // 与滑块**同一套**（沉一档的轨道 + 主色填充）：进度条与滑块并排放在一张
    // 面板上是常态，两个部件表各自挑色就会"一个是紫、一个是蓝"。
    data.track = InkingButtonAppearance(InkingAppearanceType::Color, theme::kSurfaceSunken);
    data.fill = InkingButtonAppearance(InkingAppearanceType::Color, theme::kAccent);
    return data;
}

ProgressBar::ProgressBar(InkingWidget* parent, const ProgressBarData& data)
    : InkingStaticWidget(parent, static_cast<const InkingAnchorData&>(data)),
      _data(data) {
    // 几何走基类那条锚点通道（宽高 / 锚点 / zIndex / visible 一次装好）。

    // 进度的夹取在这里做一次（构造成员初始化列表里没法调成员函数）。
    _progress = _data.progress;
    if (!(_progress > 0.0f)) {
        _progress = 0.0f;  // 含 NaN
    } else if (_progress > 1.0f) {
        _progress = 1.0f;
    }

    const bool wantsImage = _data.track.type == InkingAppearanceType::Image ||
                            _data.fill.type == InkingAppearanceType::Image;
    if (wantsImage) {
        INK_LOG_WARN(std::string(kModuleName),
                     "ProgressBar 的部件外观是 Image，但部件级纹理这一版没接："
                     "这两笔会按各自 appearance.color 画成纯色。控件名：" +
                     _data.name);
    }

    layoutParts();
}

bool ProgressBar::SetProgress(float progress) {
    float next = progress;
    if (!(next > 0.0f)) {
        next = 0.0f;  // NaN 也落到 0：夹取是"不越界"，不是"报错"
    } else if (next > 1.0f) {
        next = 1.0f;
    }
    if (next == _progress) {
        return false;  // 幂等：每帧喂同一个值 ⇒ 不标脏、零额外重绘
    }
    _progress = next;
    // 只标**重绘脏**：进度变化不改 `GetWidth()` / `GetHeight()`，容器不重排，
    // 命中表也不用重烘（几何一个字节没动）。
    layoutParts();
    MarkRepaintDirty();
    return true;
}

void ProgressBar::layoutParts() const noexcept {
    const float width = static_cast<float>(GetWidth());
    const float height = static_cast<float>(GetHeight());

    _parts.track = InkingShapeBounds{0.0f, 0.0f, width, height};
    // 与 `Slider` 共用同一个纯函数（§9.1 批次 b 的门槛）。
    _parts.fill = FillBoundsInTrack(_parts.track, _progress);
}

const ProgressBarParts& ProgressBar::GetParts() const noexcept {
    layoutParts();
    return _parts;
}

void ProgressBar::onRender(float pixelX, float pixelY) const {
    SDL_Renderer* renderer = inking_detail::currentRenderer();
    if (renderer == nullptr) {
        return;
    }
    const float magnification =
        GetMagnification() > 0.0f ? GetMagnification() : 1.0f;
    const ProgressBarParts& parts = GetParts();

    // ① 轨道总是画。
    FillPart(renderer, _data.shape, parts.track, magnification, pixelX, pixelY,
             _data.track.color);

    // ② 填充块：`progress == 0` 时**不画**（不是画一个 0 宽块——0 宽的三角化
    //    没有意义，而且会多一条提交记录）。
    FillPart(renderer, _data.shape, parts.fill, magnification, pixelX, pixelY,
             _data.fill.color);
}

}  // namespace inking_prefab
