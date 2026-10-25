#pragma once

// SVG 光栅化件（0.5.1 T-01）：把一段 SVG 字节按**目标设备像素**变成一张 surface。
//
// 为什么单独一件、而不是塞在 `InkingText.cpp` 里：
//   - 解码（SDL3_image / nanosvg）与纹理缓存是两件事：前者只回答"这份字节能变成
//     多大一张图"，后者回答"这张图还要不要重做"；混在一起时"缓存不生效"与
//     "解码失败"看起来一个样；
//   - 它是**框架内部头**（和 `InkingBatch.h` / `InkingDraw.h` / `InkingText.h`
//     同属"render 层但不在组件公开面"那一类）：只前向声明 `SDL_Surface`，
//     不把 SDL 头拖给组件。自检要直接断言矩阵，所以它得像 `InkingText.h` 一样
//     能被 `#include <render/...>` 找到。
//
// 能力边界（2026-10-24 实测，详见 `docs/zh/plans/2026-10-24-0.5.1-svg-design.md` §2）：
//   - 认：多图元、`g` / `transform`、linear/radial 渐变、组透明度、嵌套 `<svg>`、
//     `stroke` + `dasharray`、`fill-rule="evenodd"`（孔洞）；
//   - **不认且静默丢**：`text` / `use` / `<image href>` / `clipPath` / `mask` / `filter`
//     —— 这一件负责**扫描并报出来**（`unsupportedCount` / `unsupportedList`），
//     让上层能发 WARN；渲染它们要换库，见任务书 §7。
//
// 尺寸语义：SDL 的 `IMG_LoadSizedSVG_IO` **保持比例**（给 200×100 的框解 100×100 的稿
// 会得到 100×100），所以这里的"目标框"只是**像素预算**，不是最终绘制矩形。
// 若解出来任一边小于目标框（稿比框细长），会再按需要的倍数解一次（最多一次）。

#include <cstddef>
#include <cstdint>
#include <string>

struct SDL_Surface;

namespace inking::detail {

/// 一次光栅化的结果。`surface` 由调用方 `SDL_DestroySurface`。
struct SvgRasterResult {
    SDL_Surface* surface = nullptr;
    std::string error;      ///< 空 = 成功；否则是给人看的原因
    int secondPasses = 0;   ///< 0 或 1：为了"任一边不小于目标框"多解了几次
    int clamped = 0;        ///< 1 = 目标框被像素预算钳过
    int unsupportedCount = 0;  ///< 不支持的元素 / 属性命中总次数
    std::string unsupportedList;  ///< "text×2、clip-path×1"（空 = 没有）
};

/// 这份字节是不是 SVG（内容嗅探，不看扩展名）。
bool LooksLikeSvg(const std::uint8_t* data, std::size_t size);

/**
 * @brief 按目标框光栅化。
 *
 * @param boxWidth,boxHeight 目标框（设备像素）——"想画多大"，允许被预算钳制。
 * @param maxPixels          单张 surface 的像素上限（<=0 = 不钳）。
 *
 * 顺序是**先算目标框、再解码**；反过来先按固有尺寸解再放大，就是位图放大（糊）。
 */
SvgRasterResult RasterizeSvg(const std::uint8_t* data, std::size_t size,
                             int boxWidth, int boxHeight, int maxPixels);

/// 扫描不支持的标签 / 属性（大小写不敏感，跳过 `<!-- -->`）。
/// `count` 返回命中总次数；返回值为 "text×2、clip-path×1" 形式的串。
std::string ScanUnsupportedSvgElements(const std::uint8_t* data,
                                       std::size_t size, int* count);

}  // namespace inking::detail
