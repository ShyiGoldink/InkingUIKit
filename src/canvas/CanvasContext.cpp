#include <canvas/CanvasContext.h>

#include "CanvasRecorder.h"

#include <render/InkingDraw.h>

#include <cmath>

namespace inking {

CanvasContext::CanvasContext(detail::CanvasRecorder& recorder) noexcept
    : _recorder(&recorder) {}

float CanvasContext::Width() const noexcept {
    return _recorder != nullptr ? static_cast<float>(_recorder->NodeWidth())
                                : 0.0f;
}

float CanvasContext::Height() const noexcept {
    return _recorder != nullptr ? static_cast<float>(_recorder->NodeHeight())
                                : 0.0f;
}

void CanvasContext::FillShape(const ShapeSpec& shape, float x, float y, float w,
                              float h, std::uint32_t color,
                              const TransformSpec& transform) {
    if (_recorder == nullptr) {
        return;
    }
    _recorder->AddFillShape(shape, x, y, w, h, color, transform);
}

void CanvasContext::StrokeShape(const ShapeSpec& shape, float x, float y,
                                float w, float h, float strokeWidth,
                                std::uint32_t color,
                                const TransformSpec& transform) {
    // 宽度为 0 / 负的描边没有面积可言（形状层同样是"不画"，不是报错）。
    if (_recorder == nullptr || strokeWidth <= 0.0f) {
        return;
    }
    _recorder->AddStrokeShape(shape, x, y, w, h, strokeWidth, color, transform);
}

void CanvasContext::FillRect(float x, float y, float w, float h,
                             std::uint32_t color,
                             const TransformSpec& transform) {
    FillShape(ShapeSpec::Rect(), x, y, w, h, color, transform);
}

void CanvasContext::FillCircle(float centerX, float centerY, float diameter,
                               std::uint32_t color,
                               const TransformSpec& transform) {
    if (diameter <= 0.0f) {
        return;
    }
    // 形状层的 circle 是"框里居中、直径取 min(w,h)"，所以把框摆成直径见方、
    // 让中心落在调用方给的位置上。
    const float half = diameter * 0.5f;
    FillShape(ShapeSpec::Circle(), centerX - half, centerY - half, diameter,
              diameter, color, transform);
}

void CanvasContext::DrawLine(float x0, float y0, float x1, float y1,
                             float thickness, std::uint32_t color, LineCap cap) {
    // 线宽 <= 0 时 `ShapeSpec::Line` 会断言（形状自己的数学要求），这里先挡掉：
    // "不画"是对的语义，不该让它在 Debug 构建里炸。
    if (thickness <= 0.0f) {
        return;
    }
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.0f) {
        return;  // 零长线段：一个点，形状层没有对应语义
    }

    // 形状层的 `line` 是**水平**的（纵向居中、线宽 thickness）。所以这里把它
    // 摆成"长度 × 线宽"的框（框中心 = 线段中点），再旋转到目标角度——
    // 旋转绕框中心，正是 `TransformSpec::rotate` 的语义（度、顺时针为正，
    // 屏幕 y 向下所以 `atan2` 的结果直接用）。
    const float boxX = (x0 + x1) * 0.5f - length * 0.5f;
    const float boxY = (y0 + y1) * 0.5f - thickness * 0.5f;

    TransformSpec transform{};
    transform.rotate =
        static_cast<float>(std::atan2(static_cast<double>(dy),
                                      static_cast<double>(dx))
                           * 180.0 / 3.14159265358979323846);

    FillShape(ShapeSpec::Line(thickness, cap), boxX, boxY, length, thickness,
              color, transform);
}

void CanvasContext::DrawText(const TextRequest& request) {
    if (_recorder == nullptr || request.Empty()) {
        return;
    }
    _recorder->AddDrawText(request);
}

void CanvasContext::DrawText(const std::string& text, float x, float y,
                             float fontSize, std::uint32_t color) {
    TextRequest request;
    request.text = text;
    request.fontSize = fontSize;
    request.color = color;
    request.offsetX = x;
    request.offsetY = y;
    DrawText(request);
}

void CanvasContext::PushClip(float x, float y, float w, float h) {
    if (_recorder == nullptr) {
        return;
    }
    // 尺寸为 0 的裁剪区照样压入：语义是"这一段的视口是空的"，于是之后什么都不画
    // （比"忽略这次调用"更可预测——忽略会让调用方以为自己在剪）。
    _recorder->AddPushClip(x, y, w, h);
}

void CanvasContext::PopClip() {
    if (_recorder == nullptr) {
        return;
    }
    _recorder->AddPopClip();
}

void CanvasContext::ImmediateDraw(std::function<void(SDL_Renderer*)> draw) {
    if (_recorder == nullptr) {
        return;
    }
    _recorder->AddImmediate(std::move(draw));
}

SDL_Renderer* CanvasContext::Renderer() const noexcept {
    return detail::currentRenderer();
}

}  // namespace inking
