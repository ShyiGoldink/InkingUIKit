#pragma once

// 九宫格与平铺（dev010 T-04）：把"一张小图拉成任意尺寸"拆成纯几何。
//
// 为什么是**纯函数**而不是 `SetNinePatch(tex, l, t, r, b)` 这种状态式 API：
// 纹理属性一旦变成全局状态，同一张纹理在两处要不同边框就会串味（而且查起来
// 极难——"这张图的边框是谁设的？"）。这里只回答"这 9 块（或 N 块）分别画在
// 哪里、取源图的哪一块"，状态留给调用方（`InkingGeometryLayer` 的 quad 数组）。
//
// 单位：`src` 是**纹理像素**，`dst` 是**设计坐标**——与 `InkingQuad` 的字段口径
// 完全一致，所以展开出来的块可以直接填进 `InkingQuad` 交出去。
//
// 零 SDL、零依赖：只 include 一个矩形类型，可以脱离渲染器单测。

#include <dataStruct/InkingShapeSpec.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <tuple>
#include <utility>

/**
 * @brief 九宫格的边框（**源图像素**）。
 *
 * 四条边把源图切成 3×3：四角保尺寸，四条边单向拉伸，中心双向拉伸。
 * 对话框、面板、血条用它，边框粗细才不会随目标尺寸变。
 */
struct InkingNinePatchBorder {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    /// 四条边全是 0（没有边框，九宫格退化成整块拉伸）。
    bool Empty() const noexcept {
        return left <= 0.0f && top <= 0.0f && right <= 0.0f && bottom <= 0.0f;
    }
};

/// 一块的源矩形 + 目标矩形（与 `InkingQuad` 的 `src` / `dst` 同口径）。
struct InkingNinePatchTile {
    InkingShapeBounds src;  ///< 纹理像素
    InkingShapeBounds dst;  ///< 设计坐标
};

/**
 * @brief 把一块目标矩形切成 9 块。
 *
 * @param dst    目标（设计坐标）
 * @param src    源（纹理像素）
 * @param border 边框（源图像素）
 * @param out    输出缓冲，**至少 9 个**；传 nullptr 时只返回块数（用于预检）
 *
 * @return 写了几块：正常 9；`dst` 或 `src` 退化时 0。
 *
 * 边框之和超过目标尺寸时按比例夹——**不产生负宽**。夹取会同时作用在源与目标两侧
 * （否则"源切了 60、目标只给 50"会让中间那一块被压成负值）。
 */
inline std::size_t InkingExpandNinePatch(const InkingShapeBounds& dst,
                                         const InkingShapeBounds& src,
                                         const InkingNinePatchBorder& border,
                                         InkingNinePatchTile* out) {
    if (dst.width <= 0.0f || dst.height <= 0.0f || src.width <= 0.0f
        || src.height <= 0.0f) {
        return 0;
    }

    // 边框先夹来源图，再夹去目标：两次都按"和不超过上限"等比缩。
    // 顺序不能反——先夹目标再夹源的话，源那侧可能仍未被约束（反例：
    // src 100 宽、dst 20 宽、边框 60/60：夹完目标剩 10/10，源那边仍是 60）。
    const auto clampPair = [](float a, float b, float limit) {
        constexpr float kEpsilon = 1e-6f;
        const float sum = a + b;
        if (sum <= limit || sum <= kEpsilon) {
            return std::pair<float, float>{a, b};
        }
        const float scale = std::max(limit, 0.0f) / sum;
        return std::pair<float, float>{a * scale, b * scale};
    };

    float left = std::max(0.0f, border.left);
    float right = std::max(0.0f, border.right);
    float top = std::max(0.0f, border.top);
    float bottom = std::max(0.0f, border.bottom);

    std::tie(left, right) = clampPair(left, right, src.width);
    std::tie(left, right) = clampPair(left, right, dst.width);
    std::tie(top, bottom) = clampPair(top, bottom, src.height);
    std::tie(top, bottom) = clampPair(top, bottom, dst.height);

    // 三条线的起点与宽度（目标侧是设计坐标，源侧是像素）。
    const float dstX[3] = {dst.x, dst.x + left, dst.x + dst.width - right};
    const float dstW[3] = {left, dst.width - left - right, right};
    const float dstY[3] = {dst.y, dst.y + top, dst.y + dst.height - bottom};
    const float dstH[3] = {top, dst.height - top - bottom, bottom};

    const float srcX[3] = {src.x, src.x + left, src.x + src.width - right};
    const float srcW[3] = {left, src.width - left - right, right};
    const float srcY[3] = {src.y, src.y + top, src.y + src.height - bottom};
    const float srcH[3] = {top, src.height - top - bottom, bottom};

    if (out == nullptr) {
        return 9;
    }
    std::size_t written = 0;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            InkingNinePatchTile& tile = out[written++];
            tile.src = InkingShapeBounds{srcX[col], srcY[row], srcW[col], srcH[row]};
            tile.dst = InkingShapeBounds{dstX[col], dstY[row], dstW[col], dstH[row]};
        }
    }
    return written;
}

/**
 * @brief 平铺：把一张图按 `tileScale` 重复铺满目标矩形。
 *
 * 两段式（与 `InkingBuildMaskedQuad` 同一口径）：
 *
 * ```cpp
 * const std::size_t need = InkingExpandTiled(dst, src, 1.0f, nullptr, 0);
 * tiles.resize(need);
 * InkingExpandTiled(dst, src, 1.0f, tiles.data(), tiles.size());
 * ```
 *
 * 边缘那一列 / 行的块会小于一整块：`src` 跟着等比缩小（不是裁掉），
 * 这样"半块"显示的是被压窄的整张图，而不是露出图的一部分——地形重复时
 * 这两种做法看起来完全不同。
 *
 * @param tileScale 每块的显示尺寸相对源像素的倍率（1 = 原尺寸）。
 * @param capacity  `out` 能放几块；写满就停（返回值仍是要几块）。
 * @return **需要**几块（不只是写了几块），`dst` / `src` 退化时 0。
 */
inline std::size_t InkingExpandTiled(const InkingShapeBounds& dst,
                                     const InkingShapeBounds& src,
                                     float tileScale,
                                     InkingNinePatchTile* out,
                                     std::size_t capacity) {
    if (dst.width <= 0.0f || dst.height <= 0.0f || src.width <= 0.0f
        || src.height <= 0.0f) {
        return 0;
    }
    const float scale = tileScale > 0.0f ? tileScale : 1.0f;
    const float tileWidth = src.width * scale;
    const float tileHeight = src.height * scale;
    if (tileWidth <= 0.0f || tileHeight <= 0.0f) {
        return 0;
    }

    const auto columns =
        static_cast<std::size_t>(std::ceil(dst.width / tileWidth));
    const auto rows = static_cast<std::size_t>(std::ceil(dst.height / tileHeight));
    const std::size_t needed = columns * rows;
    if (out == nullptr) {
        return needed;
    }

    std::size_t written = 0;
    for (std::size_t row = 0; row < rows; ++row) {
        for (std::size_t col = 0; col < columns; ++col) {
            if (written >= capacity) {
                return needed;
            }
            const float x = dst.x + static_cast<float>(col) * tileWidth;
            const float y = dst.y + static_cast<float>(row) * tileHeight;
            const float width = std::min(tileWidth, dst.x + dst.width - x);
            const float height = std::min(tileHeight, dst.y + dst.height - y);
            if (width <= 0.0f || height <= 0.0f) {
                continue;
            }
            InkingNinePatchTile& tile = out[written++];
            // 边缘块：源也等比缩小（见上面那段"半块"的取舍）。
            tile.src = InkingShapeBounds{src.x, src.y, src.width * (width / tileWidth),
                                         src.height * (height / tileHeight)};
            tile.dst = InkingShapeBounds{x, y, width, height};
        }
    }
    return needed;
}
