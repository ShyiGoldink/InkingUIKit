#pragma once

// 预制件「悬浮提示」（`docs/zh/plans/2026-11-03-uikit-completeness-tasks.md` T-05）。
//
// **它就是**：鼠标停在某个控件上一小会儿之后，在它下方弹出的一小块深色面板 + 一行字。
//
// ---------------------------------------------------------------------------
// 为什么不基于 `InkingFixedPopup`（任务书 §6 T-05 的第 1 条）
//
// `FixedPopup` 的 `OwnsSubtreeHit()` 返回真，它对子树命中是**吃掉**的；而 Tooltip
// 必须让点击**穿过去**——停在提示上点一下，该点到下面的按钮。这是两个相反的语义，
// 硬套会把"吃掉"带进来。这里直接给两条硬保证：
//
//   - `HitTest()` 恒 `false`；
//   - `HitTestNode()` 恒 `nullptr`（子树里有什么都不算命中）。
//
// 于是"提示盖在按钮上、点下去还是按钮"这件事不靠调用方配，构造上就成立。
//
// ---------------------------------------------------------------------------
// 计时为什么挂在 `onAnimationTick` 而不是 `onRender`
//
// 静态档收不到 `Tick`（§6 第 37 条），但**两档都能收到 `onAnimationTick`**（每渲染
// 帧一次、带真实 delta）。挑它而不是 `onRender` 有两个理由：
//   1. 它是**非 const**，可以直接改几何（落位 / 改尺寸），`onRender` 是 const；
//   2. 它拿到的是 **delta**，不用自己去读场景时钟——探针也就能按帧精确喂时间
//      （`TickFrame(0.05)` 三次 = 0.15 秒），不依赖墙上时间。
//
// ⚠️ **计时期间这个节点必须可见**：不可见的节点不在绘制列表里，收不到任何每帧
// 钩子，计时就永远不走（症状是"提示永远不弹"）。所以 `IsVisible()` 在计时期间
// 为真、"真的画出来"另用 `IsShowing()` 表达——两者不是一个问题，见下面注释。
//
// 第一版**不做**：多行换行（超长文本会溢出面板，`maxWidth` 只夹面板宽）、
// 跟随锚点移动（落位只做一次）、`SetPainter`（同其余新控件）。

#include <panel/InkingDynamicPanel.h>
#include <button/ButtonData.h>

#include <cstdint>
#include <string>

namespace inking_prefab {

/// 提示的外观与节奏。
struct TooltipData {
    std::string name = "tooltip";

    /// 字体绝对路径；空 = 按 `PrefabTheme::DefaultFontPath()` 的候选表找。
    std::string fontPath;
    float fontSize = 14.0f;

    /// 停留多久才弹（秒）。
    float delay = 0.4f;

    float paddingX = 10.0f;
    float paddingY = 6.0f;
    /// 面板最宽（**只夹面板**，不换行）。
    float maxWidth = 320.0f;
    float radius = 8.0f;
    /// 与锚点之间留的间距（设计像素）。
    float gap = 6.0f;

    /// 面板底色（深色，和浅色界面拉开层次）。
    InkingButtonAppearance panel{};
    std::uint32_t textColor = 0xFFFFFFFFu;

    static TooltipData MakeDefault();
};

/**
 * @brief 悬浮提示。
 *
 * ```cpp
 * inking_prefab::Tooltip tip(&scene);
 * button->onPointerHover 的回调里判进出——或者由调用方在"悬停开始/结束"时：
 *   tip.ShowFor(button, "保存当前文档");
 *   tip.Hide();
 * ```
 *
 * 同一个 `Tooltip` 可以被多个控件复用（后一次 `ShowFor` 直接接管）。
 */
class Tooltip : public InkingDynamicPanel {
public:
    explicit Tooltip(InkingWidget* parent,
                     const TooltipData& data = TooltipData::MakeDefault());

    /**
     * @brief 开始计时（`delay` 秒后弹出）。
     *
     * 同一次悬停里重复调用**会重置计时**——那是"鼠标还在原地但被重新通知了一次"
     * 的合理语义；不想重置就别重复调。
     *
     * `anchor` 为空时什么都不做（返回 false）。
     */
    bool ShowFor(InkingWidget* anchor, const std::string& text);

    /// 取消计时 / 收起（幂等）。
    void Hide();

    /**
     * @brief **真的画出来了**吗（不是"在计时"）。
     *
     * 与 `IsVisible()` 的区别：计时期间 `IsVisible()` 已经是真（要收每帧钩子），
     * 而 `IsShowing()` 要等 `delay` 走完才为真。用户看到的是这一个。
     */
    bool IsShowing() const noexcept { return _showing; }

    /// 还在计时（已 `ShowFor`、未到时间）。
    bool IsPending() const noexcept { return _pending; }

    /// 现在跟着谁（没有就是 nullptr）。
    InkingWidget* GetAnchor() const noexcept { return _anchor; }

    const TooltipData& GetData() const noexcept { return _data; }

    // ---- 穿透：Tooltip 不参与命中（见文件头） ----
    bool HitTest(float /*worldX*/, float /*worldY*/) const override { return false; }

    InkingWidget* HitTestNode(float /*worldX*/, float /*worldY*/) noexcept override {
        return nullptr;
    }

protected:
    void onRender(float pixelX, float pixelY) const override;
    void onAnimationTick(float deltaSeconds) override;

private:
    /// 按文字量一遍 → 改尺寸 → 落位到锚点下方（越界则翻到上方并夹紧）。
    void applyTextLayout();

    /// 画文字那一笔（面板底由基类画）。
    void paintText(float pixelX, float pixelY) const;

    TooltipData _data{};

    InkingWidget* _anchor = nullptr;
    std::string _text;

    bool _pending = false;
    bool _showing = false;
    float _elapsed = 0.0f;
};

}  // namespace inking_prefab
