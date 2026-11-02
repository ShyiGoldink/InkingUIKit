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
//
// ---------------------------------------------------------------------------
// G-06：排版与取色的**唯一实现**在 `InkingLabelLike`（纯重构，行为零变化）
// ---------------------------------------------------------------------------
//
//   本类与 `InkingDynamicLabel` 共用 `text/InkingLabelLike.h`：数据面、排版缓存、
//   `LayoutForWidth` / `CacheMatches` / `EffectiveTextColor` / 测量 / 画文字那一笔
//   都只有那一份。本类只留**静态档自己的**三件事：
//   - `SetText` 之后 `RelayoutParent()`（兄弟让位 / 无布局容器时自测量）；
//   - 构造函数把 `InkingLabelData` 搬进几何与 `InkingLabelLike`；
//   - `onRender` 一行转发（并把自己那份 `TextScrollX()` 传进去）。
//
//   改名两处是机械的：`MeasureLayout`（虚函数，覆盖必须留在组件里）转发到
//   `InkingLabelLike::MeasureLabel`，`onRender` 转发到 `InkingLabelLike::paintLabelText`。

#include <text/LabelLike.h>
#include <widget/InkingStaticWidget.h>

#include <cstdint>
#include <string>


/**
 * @brief 静态标签：只读、按内容撑高、默认不吃点击。
 *
 * **不吃点击**是有意的（`SetHitPassThrough(true)`）：标签经常压在按钮或图片上，
 * 吃掉点击会让"看得见的按钮点不动"——那种 bug 很难从画面看出来。
 */
class InkingStaticLabel : public InkingStaticWidget, public InkingLabelLike {
public:
    InkingStaticLabel(InkingWidget* parent, const InkingLabelData& data);
    /// 配置驱动那条路的入口：数据由派生类（生成的类）从库里取。
    InkingStaticLabel(InkingWidget* parent, const std::string& name);

    const InkingLabelData& GetData() const noexcept { return _data; }

    /**
     * @brief 换文字（`[final]` 写入口）。
     *
     * 三件事一次做完：排好的行作废、**让所在容器重排**（尺寸可能变了，
     * 兄弟要让位）、标重绘脏。**不标命中脏**——文字不参与命中
     * （`AGENTS.md` §6 第 47 条：这条"半对"的规矩就在这里）。
     *
     * 动态档（`InkingDynamicLabel`）**故意不做第 2 件**：改几何要显式喊
     * `FitToContent()`（G-05 的关键决定一）。
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
    const InkingTextLayoutResult& GetTextLayout() const;

    /**
     * @brief A6 的测量钩子：我这段文字要多大。
     *
     * 宽度：有约束就用约束（于是按它换行），没有就用配置宽度。
     * 高度：**排版结果的高度**——撑高就发生在这里。
     */
    InkingLayoutSize MeasureLayout(const InkingLayoutConstraints& constraints) const override;

    /// 标签认**外部显示变换**（A13）：文字可以整体被挪走 / 缩放（绘制跟着走）。
    bool AcceptsExternalTransform() const noexcept override { return true; }

    /// 命中包络：整块矩形 **× 外部变换**（标签默认不参与命中，但包络要跟着动，
    /// 免得哪天它改成吃点击时表里登记的仍是老位置——`AGENTS.md` §6 第 42 条）。
    InkingShapeBounds GetHitEnvelope() const override;

protected:
    /// 画排好的每一行（实现在 `InkingLabelLike::paintLabelText`）。
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
    // ---------------- G-06：保留既有私有符号（唯一实现在 `InkingLabelLike`） ----------------
    //
    // 这三个在 G-06 之前是本类的私有成员，`nm -g` 能看到它们的符号。挪进
    // `InkingLabelLike` 之后各留一行转发，符号一个不少（与 G-02 / G-04 同一条纪律）。
    // 缓存键的完整说明在 `InkingLabelLike.h` 的 `LayoutCache` 上，别在这里再抄一份。

    const InkingTextLayoutResult& LayoutForWidth(float available,
                                           float availableHeight) const;
    bool CacheMatches(float available, float availableHeight) const noexcept;
    std::uint32_t EffectiveTextColor() const noexcept;
};

