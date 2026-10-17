#pragma once

// 一次文字请求的**纯数据**面（A12 从 `InkingText.h` 搬出来的一刀）。
//
// 为什么单独一个头：画布（A12）的公开绘制词汇要能表达"画一段文字"，而
// `InkingText.h` 是**内部头**（它要 SDL 纹理与 SDL3_ttf，见那里的说明）。
// 公开头不许拖 SDL 进来（`AGENTS.md` §3 第 2 条），所以把这两个**本来就没有
// SDL 类型**的结构搬到这里——`InkingText.h` 照旧 include 本文件，既有调用方
// （Label / TextInput / ButtonPaint / 各种自检）一行都不用改。
//
// 这也是 `InkingText.h` 里那句"故意是纯数据"的兑现：数据面在公开侧，纹理与
// 光栅化留在内部侧。

#include <cstdint>
#include <string>

namespace inking {

/**
 * @brief 一次文字请求：文字层的唯一输入，也是缓存键的来源。
 *
 * 字段里**没有 SDL 类型**：尺寸测量与绘制各取所需，谁都不必知道纹理是怎么建的。
 */
struct TextRequest {
    std::string text;   ///< 要画的文字（UTF-8）
    float fontSize = 16.0f;  ///< 字号，**设计坐标**（SDL3_ttf 的 DPI 是 72，所以
                             ///< "点"在这里就等于像素，不用再乘系数）
    std::string fontPath;    ///< 字体绝对路径；空 = 机器默认字体（按候选表找）
    std::uint32_t color = 0xFFFFFFFFu;  ///< 文字色 0xAARRGGBB

    /// 文字块左上角在**组件本地**的位置（设计坐标，相对组件左上角）。
    /// 按钮那边就是配置里的 `leftSpace` / `topSpace`；画布那边由绘制器自己填
    /// （`CanvasContext::DrawText` 的两个便捷重载就是替调用方填这两个字段）。
    /// `MeasureText` 不看这两个字段。
    float offsetX = 0.0f;
    float offsetY = 0.0f;

    bool Empty() const noexcept { return text.empty(); }
};

/// 量出来的文字尺寸，**设计坐标**（和组件尺寸同一套单位）。
struct TextMetrics {
    float width = 0.0f;
    float height = 0.0f;
};

}  // namespace inking
