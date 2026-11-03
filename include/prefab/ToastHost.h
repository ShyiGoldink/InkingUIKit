#pragma once

// 预制件「通知条」（`docs/zh/plans/2026-11-03-uikit-completeness-tasks.md` T-07）。
//
// **它就是**：一个宿主 + 一摞纵向堆叠的短提示，到点自己消失。
//
// ---------------------------------------------------------------------------
// 三条设计决定
//
//   1. **不参与命中**：通知是"告诉你的"，不是"让你点的"。所以宿主 `HitTest` 恒
//      false、`HitTestNode` 恒 nullptr——整摞提示对指针完全透明，盖在按钮上也
//      不挡那一下。要"点通知就跳转"是另一个控件（那得有个真实热区）。
//   2. **位置自己算，不用布局求解器**：栈的增删是运行期的，而布局求解器那套是
//      构造期定型（`InkingPanelData::layout` 只在 `Relayout()` 时算一遍）。
//   3. **到期在 `onAnimationTick` 里统一删**，不在遍历中途删（`AGENTS.md` §6
//      第 24 条）。顺序是"先收集 → 循环外统一 `RemoveChild` → 再重排"。
//
// ---------------------------------------------------------------------------
// 计时与"宿主常驻"
//
// 计时用 `onAnimationTick` 的 delta 累加（不读墙上时间）：探针可以按帧精确喂时间
// （`TickFrame(0.5)` 四次 = 2 秒），不依赖 sleep。
//
// 宿主**始终可见**（即便一条都没有）：不可见的节点收不到每帧钩子，那样最后一条
// 到期时就没人负责把它删掉。代价是空的时候每帧一次 bool 判断 + 一次空 `onRender`，
// 比"每次增删都改整棵树的可见性（结构脏 + 命中表重烘）"便宜得多。

#include <panel/InkingDynamicPanel.h>
#include <event/InkingEvent.h>
#include <button/ButtonData.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace inking_prefab {

/// 通知条的外观与堆叠参数。
struct ToastData {
    std::string name = "toastHost";

    /// 单条宽度（设计坐标）。文字超出会被裁（这一版不换行、不做多行）。
    float width = 260.0f;
    /// 距右下角的边距。
    float marginRight = 24.0f;
    float marginBottom = 24.0f;
    /// 条与条之间的间距。
    float gap = 8.0f;

    float paddingX = 12.0f;
    float paddingY = 8.0f;
    float radius = 10.0f;
    float fontSize = 14.0f;
    std::string fontPath;  ///< 空 = 机器默认字体

    InkingButtonAppearance panel{};
    std::uint32_t textColor = 0xFFFFFFFFu;

    static ToastData MakeDefault();
};

/**
 * @brief 通知宿主：`Show()` 一条就多一条，到点自己消失。
 *
 * ```cpp
 * inking_prefab::ToastHost toasts(&scene);
 * toasts.Show("已保存");                 // 2 秒后自己消失
 * toasts.Show("网络断了", 4.0f);          // 4 秒
 * toasts.dismissed.Subscribe([](int id) { ... });
 * ```
 *
 * **不拥有调用方的代码**：`Show` 返回一个自增 id（从 1 起，永不复用），
 * `dismissed` 带的就是它——按下标不行，因为前面的条消失之后下标会整体前移。
 */
class ToastHost : public InkingDynamicPanel {
public:
    /// 某一条到期消失。参数是 `Show()` 返回的 id。
    InkingEvent<int> dismissed;

    explicit ToastHost(InkingWidget* parent,
                       const ToastData& data = ToastData::MakeDefault());

    /**
     * @brief 弹一条。
     *
     * @param text    文案（单行；超宽会被裁）
     * @param seconds 停留时长（秒）；`<= 0` 时用 2 秒
     * @return 这一条的 id（从 1 起自增，永不复用）
     */
    int Show(const std::string& text, float seconds = 2.0f);

    /// 现在挂着几条（自检断言用；也已包含"正在淡出"的那些——这一版没有淡出）。
    std::size_t GetToastCount() const noexcept;

    /// 立刻清空（不发 `dismissed`：那是"自己到期"的通知，不是"被你清掉"）。
    void Clear();

    const ToastData& GetData() const noexcept { return _data; }

    // ---- 不参与命中（见文件头第 1 条） ----
    bool HitTest(float /*worldX*/, float /*worldY*/) const override { return false; }
    InkingWidget* HitTestNode(float /*worldX*/, float /*worldY*/) noexcept override {
        return nullptr;
    }

protected:
    /// 宿主自己不画东西（子项各画各的）。**不调基类**——基类会画一块整屏的底。
    void onRender(float /*pixelX*/, float /*pixelY*/) const override {}

    /// 推进时间 → 收集到期 → 统一删 → 重排。
    void onAnimationTick(float deltaSeconds) override;

private:
    /// 按"从下往上、新的在最下"重算每条的偏移与尺寸。
    void relayoutToasts();

    ToastData _data{};
    /// 宿主自己的时钟（秒，从构造起累加；只有 `onAnimationTick` 的 delta 会推动它）。
    double _now = 0.0;
    int _nextId = 1;
};

}  // namespace inking_prefab
