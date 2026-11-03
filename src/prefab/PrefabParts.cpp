#include <prefab/PrefabParts.h>

#include <render/InkingDraw.h>

#include <algorithm>
#include <cmath>

namespace inking_prefab {

namespace {

/// 设计坐标 → 设备像素的**同一个**取整口径（填与描边必须一致，否则描边会
/// 比填充大一个像素、看起来像"描边偏了"）。
int RoundSize(float value) {
    return std::max(1, static_cast<int>(std::lround(value)));
}

}  // namespace

void FillPart(SDL_Renderer* renderer, const InkingShapeSpec& shape,
              const InkingShapeBounds& box, float magnification, float pixelX,
              float pixelY, std::uint32_t color) {
    if (renderer == nullptr || color == 0u || !(box.width > 0.0f) ||
        !(box.height > 0.0f)) {
        return;
    }
    inking_detail::fillShape(renderer, shape, RoundSize(box.width),
                             RoundSize(box.height), magnification,
                             pixelX + box.x * magnification,
                             pixelY + box.y * magnification,
                             InkingTransformSpec{}, color);
}

void StrokePart(SDL_Renderer* renderer, const InkingShapeSpec& shape,
                const InkingShapeBounds& box, float magnification, float pixelX,
                float pixelY, const InkingStrokeSpec& stroke) {
    if (renderer == nullptr || !stroke.Visible() || !(box.width > 0.0f) ||
        !(box.height > 0.0f)) {
        return;
    }
    inking_detail::strokeShape(renderer, shape, RoundSize(box.width),
                               RoundSize(box.height), magnification,
                               pixelX + box.x * magnification,
                               pixelY + box.y * magnification,
                               InkingTransformSpec{}, stroke.width, stroke.color);
}

}  // namespace inking_prefab
