#pragma once

// 文本排版（A7）：把**一段文本**排成**若干行**，并算好它要占多大。
//
// 三件事都在这里，因为它们是同一件事的三个面：
//   1. **换行**：按可用宽度断行——中英混排下"哪里能断"是规则问题，不是估算问题；
//   2. **省略**：放不下时用 `…` 收尾（而且要"回退掉一个字再加省略号"，
//      否则加完还是超出）；
//   3. **撑高**：行数 × 行高 —— 这就是 A6 那条"label 按文字自己撑高"的出处。
//
// 两条设计决定：
//
//   - **不含 SDL**：真正的"文字 → 像素宽度"是文字层（`InkingText.h`，含 SDL）的事，
//     这一层只要一个**测量回调**（`TextMeasureFn`）。于是排版逻辑可以
//     用"每个码点固定宽"的假测量做数值自检，不需要字体、不需要渲染器；
//   - **纯函数**：`LayoutText` 不改任何状态、不缓存，同样的输入永远同样的输出
//     （A6 的 `MeasureLayout` 要求测量是纯函数，这里是它的下游）。
//
// 真实用法（在 .cpp 里把文字层接上）：
//
//     const auto measure = [](const std::string& s, const inking::TextStyle& style) {
//         const inking::TextMetrics m = inking::MeasureText(inking::TextRequest{
//             .text = s, .fontSize = style.fontSize, .fontPath = style.fontPath});
//         return inking::TextExtent{m.width, m.height};
//     };
//     const inking::TextLayoutResult laid = inking::LayoutText(request, measure);

#include <dataStruct/InkingLayout.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace inking {

/// 省略号：U+2026（UTF-8 三个字节）。别用三个点——宽度不一样，回退算法会算错。
inline constexpr const char* kTextEllipsis = "\xE2\x80\xA6";

/// 换行方式。
enum class TextWrap : std::uint8_t {
    NoWrap = 0,  ///< 不自动换行（只有显式 `\n` 才断）——单行标签用这个
    Word,        ///< 按"可断点"换行：英文整词不切开，CJK 字与字之间可断
    Anywhere,    ///< 逐码点硬切（长单词 / URL / 无空格语言）
};

/// 行内水平对齐。
enum class TextAlignH : std::uint8_t { Left = 0, Center, Right };

/// 整块垂直对齐（在给定的高度里怎么摆；高度不限时它是 `Top`）。
enum class TextAlignV : std::uint8_t { Top = 0, Middle, Bottom };

/// 放不下时怎么办。
enum class TextOverflow : std::uint8_t {
    Clip = 0,  ///< 直接切掉（多余的行不画）
    Ellipsis,  ///< 最后一行用 `…` 收尾
};

/// 量出来的尺寸，设计坐标。
struct TextExtent {
    float width = 0.0f;
    float height = 0.0f;
};

/**
 * @brief 文本样式：字号 / 行距 / 对齐 / 内缩。
 *
 * 与 A6 的布局分工：布局管"这个组件在父容器里多大"，这里管"文字在组件里怎么摆"。
 * 内缩（`padding`）是后者——它是文本框自己与文字之间的留白，不是外边距。
 */
struct TextStyle {
    float fontSize = 16.0f;      ///< 字号，设计坐标
    float lineSpacing = 1.25f;   ///< 行距倍数（1.0 = 行与行紧贴）
    std::string fontPath;        ///< 字体绝对路径；空 = 默认字体
    TextAlignH align = TextAlignH::Left;
    TextAlignV alignV = TextAlignV::Top;
    LayoutSpacing padding{};     ///< 文字与文本框四边之间的留白
};

/// 一次排版请求。
struct TextLayoutRequest {
    std::string text;
    TextStyle style{};

    /// 可用宽度上限（设计坐标）。`0` = 不限 ⇒ 不会自动换行。
    float maxWidth = 0.0f;

    /// 可用高度上限（0 = 不限）。只在"垂直对齐"与"是否被截断"上有意义——
    /// 排版**不会**为了塞进高度而缩字号（那是另一件事）。
    float maxHeight = 0.0f;

    /// 行数上限（0 = 不限）。超出时按 `overflow` 处理。
    int maxLines = 0;

    TextWrap wrap = TextWrap::Word;
    TextOverflow overflow = TextOverflow::Clip;
};

/// 一行落在源串里的哪一段，以及它排好之后的位置。
struct TextLine {
    std::size_t begin = 0;   ///< 源串里的**字节**起点
    std::size_t end = 0;     ///< 字节终点（不含）
    float width = 0.0f;      ///< 这一行的宽度（含省略号，如果有）
    float x = 0.0f;          ///< 相对文本框左边的起点（对齐算出来的）
    float y = 0.0f;          ///< 这一行的顶边
    float height = 0.0f;     ///< 这一行的行高
    bool hasEllipsis = false;  ///< 这一行末尾补了 `…`（画的时候要接上）
};

/// 排版结果。`width` / `height` 就是**文本框该有多大**（含 padding）。
struct TextLayoutResult {
    std::vector<TextLine> lines;
    float width = 0.0f;
    float height = 0.0f;
    float contentWidth = 0.0f;   ///< 不含 padding
    float contentHeight = 0.0f;
    bool truncated = false;      ///< 有内容没排进来（被行数 / 宽度截了）
    bool empty = true;           ///< 一个码点都没有（空串 / 全是空行）
};

/**
 * @brief 测量回调：量"一段连续文字"在给定样式下占多大。
 *
 * 真实实现包一层 `inking::MeasureText`；自检里用"每个码点固定宽"的假测量。
 * **按整行调用**（不是逐码点）：换行的贪心算法先切好候选行，再问它一次宽度。
 */
using TextMeasureFn =
    std::function<TextExtent(const std::string& text, const TextStyle& style)>;

/**
 * @brief 排版：切行、算位置、算总尺寸。**纯函数**。
 *
 * 规则（与 `CXXCSS.md` §3.8 的字段一一对应）：
 *
 *   1. **显式 `\n` 永远断行**（不管 `wrap` 是什么）；`\r\n` 当 `\n`；
 *   2. `Word`：优先在**可断点**断——空白之后、CJK 字与字之间。
 *      英文整词不会被切开；一个词自己就超宽时，退化成 `Anywhere` 硬切它
 *      （不然会溢出去，那是更糟的结果）；
 *   3. `Anywhere`：逐码点硬切；
 *   4. **行首空白丢掉**（换行处留下的空格不该顶在行首）；
 *   5. 行高 = `该行测量高度 × lineSpacing`；整块高 = 各行高之和（不含 padding）；
 *   6. 宽度 = **最宽的一行**（`NoWrap` 时就是那一行），再加 padding；
 *   7. 受 `maxLines` / `maxWidth` 限制而有剩余内容时：`Ellipsis` 就在最后一行
 *      末尾补 `…`（**先回退字符**直到放得下），`Clip` 就直接丢掉；两种都置
 *      `truncated`；
 *   8. `padding` **先扣**：可排宽度 = `maxWidth - 左右 padding`。扣完 ≤ 0 时
 *      **不换行**（一行硬断成零宽的行没有意义）——这是有意的行为，不是漏判。
 *
 * 空串返回 `empty = true`、尺寸只剩 padding（不崩、也不产生假行）。
 */
TextLayoutResult LayoutText(const TextLayoutRequest& request,
                            const TextMeasureFn& measure);

/**
 * @brief 一行的水平起点：把**对齐**单独拿出来，供"组件宽度和排版宽度不一样"时重算。
 *
 * 什么时候会不一样：排版按 `maxWidth` 断行，而组件被 A6 的 `fillMain` 拉得更宽
 * （或者被 `max` 夹得更窄）。那时 `TextLine::x` 那份是按排版宽度算的，
 * 组件用这个函数按自己的**实际宽度**再算一次即可——规则只有一份，不会分叉。
 */
float LineOffsetX(float lineWidth, float availableWidth, TextAlignH align);

}  // namespace inking
