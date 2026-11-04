#include <dataStruct/InkingScroll.h>

#include <algorithm>


float InkingScrollRange(float contentSize, float viewportSize) noexcept {
    return std::max(0.0f, contentSize - viewportSize);
}

float InkingClampScroll(float offset, float range) noexcept {
    if (range <= 0.0f) {
        return 0.0f;  // 没得滚：永远回到起点（别留一个"滚到一半"的假状态）
    }
    return std::clamp(offset, 0.0f, range);
}

void InkingApplyWheelToScroll(const InkingScrollSpec& spec, float deltaX,
                              float deltaY, float& offsetX,
                              float& offsetY) noexcept {
    // 方向：滚轮向上（deltaY > 0）看更上面的内容 ⇒ 滚动量减小。
    // **减号不能翻**：翻了两档一起反，而且"反着滚"看起来像系统设置问题。
    if (spec.horizontal) {
        offsetX -= deltaX * spec.wheelStep;
    }
    if (spec.vertical) {
        offsetY -= deltaY * spec.wheelStep;
    }
    // 刻意**不夹紧**：夹紧只在 `SetScroll` 里做一次（见头文件）。
}

InkingScrollBarMetrics InkingComputeScrollBar(bool vertical, float viewportWidth,
                                  float viewportHeight, float contentSize,
                                  float offset, float range,
                                  const InkingScrollSpec& spec) noexcept {
    InkingScrollBarMetrics bar;

    const InkingScrollAxisMode mode =
        vertical ? spec.verticalBar : spec.horizontalBar;
    if (mode == InkingScrollAxisMode::Never) {
        return bar;  // 不画（但那条轴照样能滚）
    }
    if (mode == InkingScrollAxisMode::Auto && range <= 0.0f) {
        return bar;  // 内容没超出：自动模式不出条
    }

    const float viewportLength = vertical ? viewportHeight : viewportWidth;
    const float trackLength = viewportLength - spec.barInset * 2.0f;
    if (trackLength <= 0.0f || spec.barWidth <= 0.0f) {
        return bar;  // 容器太小 / 条太细：没有可画的轨道
    }

    // 滑块长度 = 轨道长 ×（视口 / 内容），并夹在 [最短, 轨道长] 之间。
    // 内容为 0 时按"视口占满"算，免得除零得出个怪值。
    const float ratioOfViewport =
        viewportLength / std::max(contentSize, 1.0f);
    const float minThumb = std::min(spec.minThumbLength, trackLength);
    const float thumbLength =
        std::clamp(trackLength * ratioOfViewport, minThumb, trackLength);

    // 滑块位置：可滚范围里走到哪，滑块就在剩余行程里走到哪。
    const float travel = trackLength - thumbLength;
    const float ratio = range > 0.0f ? InkingClampScroll(offset, range) / range : 0.0f;
    const float thumbOffset = travel * ratio;

    if (vertical) {
        bar.trackX = viewportWidth - spec.barInset - spec.barWidth;
        bar.trackY = spec.barInset;
        bar.trackWidth = spec.barWidth;
        bar.trackHeight = trackLength;

        bar.thumbX = bar.trackX;
        bar.thumbY = bar.trackY + thumbOffset;
        bar.thumbWidth = spec.barWidth;
        bar.thumbHeight = thumbLength;
    } else {
        bar.trackX = spec.barInset;
        bar.trackY = viewportHeight - spec.barInset - spec.barWidth;
        bar.trackWidth = trackLength;
        bar.trackHeight = spec.barWidth;

        bar.thumbX = bar.trackX + thumbOffset;
        bar.thumbY = bar.trackY;
        bar.thumbWidth = thumbLength;
        bar.thumbHeight = spec.barWidth;
    }

    bar.visible = true;
    return bar;
}

float InkingScrollFromTrackClick(bool vertical, float clickX, float clickY,
                           const InkingScrollBarMetrics& bar, float range) noexcept {
    if (!bar.visible || range <= 0.0f) {
        return 0.0f;
    }
    const float click = vertical ? clickY : clickX;
    const float trackStart = vertical ? bar.trackY : bar.trackX;
    const float trackLength = vertical ? bar.trackHeight : bar.trackWidth;
    const float thumbLength = vertical ? bar.thumbHeight : bar.thumbWidth;
    const float travel = trackLength - thumbLength;
    if (travel <= 0.0f) {
        return 0.0f;  // 滑块占满轨道：没地方可跳
    }
    // "跳过去"= 滑块**中心**落到点击处 ⇒ 反解出比例（夹到 [0,1]）。
    const float ratio = (click - trackStart - thumbLength * 0.5f) / travel;
    return std::clamp(ratio, 0.0f, 1.0f) * range;
}

bool InkingHitsThumb(float x, float y, const InkingScrollBarMetrics& bar) noexcept {
    // 轴不用传：`bar` 里那四个值已经是它自己那条轴的（见头文件）。
    if (!bar.visible) {
        return false;
    }
    return x >= bar.thumbX && x < bar.thumbX + bar.thumbWidth
        && y >= bar.thumbY && y < bar.thumbY + bar.thumbHeight;
}

