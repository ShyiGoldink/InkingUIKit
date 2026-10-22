#pragma once

// 静态标签（A7）的**数据面**：一段文字 + 它怎么排。
//
// 与按钮上的文字有什么区别：按钮的文字是"贴在按钮上的一行"，而标签是
// **独立的组件**——它按内容撑大自己、参与 A6 的布局、可以被换行与省略。
// 两者共用同一个文字层（`MeasureText` / `PaintText`），所以观感一致。

#include <dataStruct/InkingAnchorData.h>
#include <dataStruct/InkingLayout.h>
#include <dataStruct/InkingTextLayout.h>

#include <cstdint>
#include <string>

namespace inking {

/// 模块名常量：日志与自检要用（约定：每个模块都暴露一个）。
inline constexpr const char* kLabelModuleName = "InkingStaticLabel";

/// 标签的数据面。
struct LabelData {
    std::string name;
    int width = 0;    ///< 配置的宽高：参与布局时会被布局改写，否则就是它
    int height = 0;

    AnchorPoint selfAnchor{};
    AnchorPoint traceAnchor{};
    float offsetX = 0.0f;
    float offsetY = 0.0f;

    int zIndex = 0;
    bool visible = true;

    /// 布局（A6）：`layoutItem` 决定它在父容器里怎么排；
    /// **撑高**由 `InkingStaticLabel::MeasureLayout` 报告，不需要额外开关。
    LayoutSpec layout{};
    LayoutItem layoutItem{};

    // ---------------- 文本 ----------------
    std::string text;              ///< 源串（UTF-8）
    TextStyle style{};             ///< 字号 / 行距 / 字体 / 对齐 / 内缩
    std::uint32_t color = 0xFFFFFFFFu;  ///< 文字色 0xAARRGGBB

    /**
     * 文字色来自哪个**主题 token**（不带 `$`）；`nullptr` = 不是语义色。
     *
     * `color` 是生成期按默认主题烘好的字面量，token 名留给运行期换肤重查。
     * **必须指向静态生命周期的串**（生成器发的是字面量）。
     */
    const char* colorToken = nullptr;

    /// 排版的三个开关（见 `InkingTextLayout.h`）。
    TextWrap wrap = TextWrap::Word;
    TextOverflow overflow = TextOverflow::Clip;
    int maxLines = 0;              ///< 0 = 不限

    bool Empty() const noexcept { return text.empty(); }
};

}  // namespace inking
