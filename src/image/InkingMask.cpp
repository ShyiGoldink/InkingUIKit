#include <image/InkingMask.h>

#include <dataStruct/InkingColor.h>

#include <algorithm>
#include <cmath>

namespace {

/// 默认细分密度：16 格 = 289 顶点 / 512 三角形。边缘过渡带宽约 1 像素，
/// 16 格在 32~128 像素的图元上已经看不出棱角。
constexpr int kDefaultDensity = 16;

}  // namespace

InkingMaskedQuad InkingBuildMaskedQuad(const InkingQuad& quad,
                                       const InkingShapeSpec& mask,
                                       int textureWidth, int textureHeight,
                                       float maskWidth, float maskHeight,
                                       int density) {
    InkingMaskedQuad result;

    const float width = quad.dst.width;
    const float height = quad.dst.height;
    if (width <= 0.0f || height <= 0.0f) {
        return result;  // 退化矩形没有面积
    }

    int steps = density > 0 ? density : kDefaultDensity;
    if (steps > InkingMaskMaxDensity) {
        steps = InkingMaskMaxDensity;
        result.clamped = true;
    }

    // UV 的两端：`src` 全零 = 整张纹理（与 `InkingQuad` 同一口径）。
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
    if (quad.src.width > 0.0f && quad.src.height > 0.0f && textureWidth > 0
        && textureHeight > 0) {
        const float invW = 1.0f / static_cast<float>(textureWidth);
        const float invH = 1.0f / static_cast<float>(textureHeight);
        u0 = quad.src.x * invW;
        v0 = quad.src.y * invH;
        u1 = (quad.src.x + quad.src.width) * invW;
        v1 = (quad.src.y + quad.src.height) * invH;
    }

    // 遮罩自己的坐标系与目标矩形可能不同尺寸（例如用 64×64 的遮罩铺 200×200 的图）。
    const float maskScaleX =
        maskWidth > 0.0f ? maskWidth / width : 1.0f;
    const float maskScaleY =
        maskHeight > 0.0f ? maskHeight / height : 1.0f;

    const std::uint32_t tint = quad.tint;
    const float tintAlpha = static_cast<float>(InkingColorAlpha(tint)) / 255.0f;

    const int side = steps + 1;
    result.vertices.resize(static_cast<std::size_t>(side) * side);
    for (int iy = 0; iy < side; ++iy) {
        const float t = static_cast<float>(iy) / static_cast<float>(steps);
        for (int ix = 0; ix < side; ++ix) {
            const float s = static_cast<float>(ix) / static_cast<float>(steps);

            // 位置：设计坐标（相对图层左上角），与 `InkingQuad::dst` 同一套。
            const float localX = quad.dst.x + width * s;
            const float localY = quad.dst.y + height * t;

            // 遮罩局部坐标：相对 dst 左上角，再缩放到遮罩自己的尺寸。
            const float maskX = (localX - quad.dst.x) * maskScaleX;
            const float maskY = (localY - quad.dst.y) * maskScaleY;
            const float distance = InkingSignedDistance(
                mask, maskX, maskY, static_cast<int>(std::lround(maskWidth)),
                static_cast<int>(std::lround(maskHeight)));

            // 与 `fillShape` 的羽化带同一条公式：`f <= 0` 是内部（不透明），
            // `f > 0.5` 完全透明，中间线性过渡 ⇒ 约 1 像素的抗锯齿边。
            const float coverage = std::clamp(0.5f - distance, 0.0f, 1.0f);

            InkingVertex& vertex =
                result.vertices[static_cast<std::size_t>(iy) * side + ix];
            vertex.x = localX;
            vertex.y = localY;
            vertex.u = u0 + (u1 - u0) * s;
            vertex.v = v0 + (v1 - v0) * t;
            // 只动 alpha：颜色仍是 tint，遮罩不该改变色相。
            const auto alpha = static_cast<std::uint32_t>(
                std::lround(coverage * tintAlpha * 255.0f));
            vertex.color = (tint & 0x00FFFFFFu) | (alpha << 24);
        }
    }

    // 索引：每格两个三角形。绕向与 `InkingGeometryLayer` 的 quad 一致
    // （屏幕坐标 y 向下，0-1-2 / 0-2-3 是正面）。
    result.indices.reserve(static_cast<std::size_t>(steps) * steps * 6);
    for (int iy = 0; iy < steps; ++iy) {
        for (int ix = 0; ix < steps; ++ix) {
            const auto a = static_cast<std::uint16_t>(iy * side + ix);
            const auto b = static_cast<std::uint16_t>(iy * side + ix + 1);
            const auto c = static_cast<std::uint16_t>((iy + 1) * side + ix + 1);
            const auto d = static_cast<std::uint16_t>((iy + 1) * side + ix);
            result.indices.push_back(a);
            result.indices.push_back(b);
            result.indices.push_back(c);
            result.indices.push_back(a);
            result.indices.push_back(c);
            result.indices.push_back(d);
        }
    }
    return result;
}
