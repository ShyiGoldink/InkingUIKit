#pragma once

// 静态标签（A7）：一段**只读**文字，按内容自己撑高，排在父容器布局里。
//
// 它把三件事接在一起（前两件是 A6 / A7 各自的地基）：
//
//   1. **文字排版**（`InkingTextLayout.h`）：换行、省略号、行位置；
//   2. **测量契约**（A6 的 `MeasureLayout`）：报告"这段文字要多大"——
//      这就是任务书那条"**label 按文字自己撑高**"，一个容器里两个不同长度的
//      标签会自动被推开；
//   3. **文字层**（`InkingText.h`，在 .cpp 里接）：把排好的每一行画出来。
//
// 可编辑的输入框（光标 / 选区 / IME）是另一件事，见 `TaskGuide.md` B7。

#include <widget/InkingStaticWidget.h>
#include <text/LabelData.h>

#include <cstdint>
#include <string>
#include <vector>

namespace inking {

/**
 * @brief 静态标签：只读、按内容撑高、默认不吃点击。
 *
 * **不吃点击**是有意的（`SetHitPassThrough(true)`）：标签经常压在按钮或图片上，
 * 吃掉点击会让"看得见的按钮点不动"——那种 bug 很难从画面看出来。
 */
class InkingStaticLabel : public InkingStaticWidget {
public:
    InkingStaticLabel(InkingWidget* parent, const LabelData& data);
    /// 配置驱动那条路的入口：数据由派生类（生成的类）从库里取。
    InkingStaticLabel(InkingWidget* parent, const std::string& name);

    const LabelData& GetData() const noexcept { return _data; }

    /**
     * @brief 换文字（`[final]` 写入口）。
     *
     * 三件事一次做完：排好的行作废、**让所在容器重排**（尺寸可能变了，
     * 兄弟要让位）、标重绘脏。**不标命中脏**——文字不参与命中
     * （`AGENTS.md` §6 第 47 条：这条"半对"的规矩就在这里）。
     */
    bool SetText(std::string text) noexcept;

    /// 换文字色（`[final]`）：只标重绘脏（颜色走纹理调制，不重烘纹理）。
    bool SetTextColor(std::uint32_t color) noexcept;

    /**
     * @brief 排好的行（观测口 + 绘制用）。
     *
     * **不推脏**：它按当前宽度排一次并缓存，宽度没变就直接给上一次的结果
     * （`AGENTS.md` §6 第 41 条那条"观测口要不要推脏"必须写死一条）。
     */
    const TextLayoutResult& GetTextLayout() const;

    /**
     * @brief A6 的测量钩子：我这段文字要多大。
     *
     * 宽度：有约束就用约束（于是按它换行），没有就用配置宽度。
     * 高度：**排版结果的高度**——撑高就发生在这里。
     */
    LayoutSize MeasureLayout(const LayoutConstraints& constraints) const override;

    /// 标签认**外部显示变换**（A13）：文字可以整体被挪走 / 缩放（绘制跟着走）。
    bool AcceptsExternalTransform() const noexcept override { return true; }

    /// 命中包络：整块矩形 **× 外部变换**（标签默认不参与命中，但包络要跟着动，
    /// 免得哪天它改成吃点击时表里登记的仍是老位置——`AGENTS.md` §6 第 42 条）。
    ShapeBounds GetHitEnvelope() const override;

protected:
    void onRender(float pixelX, float pixelY) const override;

    /**
     * 文字这一笔的**水平滚动量**（设计坐标，0 = 不滚）。
     *
     * 默认 0，标签不用管它。存在的理由是输入框（`InkingTextInput`，T-12）：
     * 单行文本超宽时要把**文字**左移，让光标停在框里而不是贴在右边框上。
     * 做成虚钩子而不是给 `onRender` 加参数：`onRender` 的签名是全库统一的那一个，
     * 为一个组件改它会让十几个重写全跟着动。
     *
     * 只影响**绘制**：排版（换行 / 行宽）与命中都用未滚动的坐标，滚动量由
     * 使用方自己负责（输入框在 `BoxForOffset` / `OffsetAtPoint` 里一对一抵消）。
     */
    virtual float TextScrollX() const noexcept { return 0.0f; }

private:
    /// 排版的缓存键：**所有影响排版的字段**都要进来（`AGENTS.md` §6 第 72 条
    /// 那条教训——漏一个就是"改了配置、画面没变"）。
    ///
    /// `languageVersion` 是 A7 的"切换时机"：它一变，缓存自然未命中 ⇒ 重查串、
    /// 重排、重烘纹理。**不需要**谁来遍历所有标签通知一遍。
    /// `resolvedText` 存的是**查表之后**的串：行区间（`TextLine::begin/end`）
    /// 指向的是它，而不是 `LabelData::text` 里的那个占位符。
    struct LayoutCache {
        bool valid = false;
        std::string text;
        std::string fontPath;
        /// 字体回退链（T-13）：**必须一起比**。链变了而这里不比，排出来的
        /// 宽高会停在旧字体上——"改了配置、框还是旧尺寸"，就是 §6 第 72 条
        /// 那类静默失效。
        std::vector<std::string> fontPaths;
        float fontSize = 0.0f;
        float lineSpacing = 0.0f;
        TextAlignH align = TextAlignH::Left;
        TextAlignV alignV = TextAlignV::Top;
        LayoutSpacing padding{};
        float availableWidth = 0.0f;
        int maxLines = 0;
        TextWrap wrap = TextWrap::Word;
        TextOverflow overflow = TextOverflow::Clip;
        std::uint64_t languageVersion = 0;
        std::string resolvedText;
        TextLayoutResult result{};
    };

    /// 按给定可用宽度排版（带缓存）。`available <= 0` 表示"宽度不限"。
    const TextLayoutResult& LayoutForWidth(float available) const;

    /// 缓存键是否还匹配（逐字段比，见 `LayoutCache` 的注释）。
    bool CacheMatches(float available) const noexcept;

    /**
     * 文字色**真正要画**的值。
     *
     * `_data.color` 是生成期按默认主题烘好的字面量；`_data.colorToken` 非空时
     * 说明它是语义色，要在运行期按当前主题重查（`theme/InkingTheme.h`）。
     * 结果按 `ThemeVersion()` 缓存——稳态只比一次版本号，不查表、不分配。
     */
    std::uint32_t EffectiveTextColor() const noexcept;

    LabelData _data;
    mutable LayoutCache _cache;

    /// 上一次解析文字色时的主题版本与结果（和 `LayoutCache` 分开：颜色变不改排版）。
    /// `_themeColorValid` 区分"还没算过"与"算出来正好是版本 0"。
    mutable bool _themeColorValid = false;
    mutable std::uint64_t _themeVersion = 0;
    mutable std::uint32_t _themeColor = 0xFFFFFFFFu;
};

}  // namespace inking
