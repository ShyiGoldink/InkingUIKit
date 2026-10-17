#pragma once

// Panel 的**两态状态机 + 开合过渡**：静态 / 动态 Panel 共用的那一份。
//
// 位置和理由都对应 `include/button/ButtonLook.h`：两条路（静态档 / 动态档）
// 的**输入方式**不同，但"哪个状态配哪份外观""过渡怎么插值""当场改值的那笔账
// 记在哪儿"必须只有一份——抄第二遍就会分叉，而分叉的表现是"静态面板和动态
// 面板开合手感不一样"，两边单独看都对、合起来才错。
//
// 它**不碰 SDL、不碰渲染器**：纯数据 + 纯逻辑，所以放在公开头里不破纪律
// （docs/AGENTS.md §6 第 8 条）。
//
// ---------------------------------------------------------------------------
// 与 ButtonLook 的关系（**已知的重复，下一次要抽**）
//
// 两者的过渡推进（起一段过渡、按缓动曲线插值、收尾直接落目标值、把"当场改值"
// 记成待报告账）逻辑上是同一件事，现在各写了一份。A1 没有把它抽成共用的
// `TransitionTrack`，理由有两条，都写在 `docs/ComponentGuide.md`：
//   1. 抽它要动**唯一一个已验证的组件**（Button），而测试在 A1 之前被整块移除
//      ——没有回归网时动它是拿"已经对的东西"换"更整齐的东西"；
//   2. `TaskGuide.md` r2：一次运行最多完成一个任务。
// **第三个需要过渡的组件出现时**，就该把它抽出来（那时有两条调用方 + 自检）。
// ---------------------------------------------------------------------------

#include <panel/PanelData.h>

#include <cstdint>

namespace inking {

/// 面板的两个状态。注意它不是"指针三态"——开合是内容状态（见 PanelData.h）。
enum class PanelState : std::uint8_t {
    Closed = 0,  ///< 关着
    Open,        ///< 开着
};

/**
 * @brief 两态外观、状态机，以及**两态之间的颜色 / 变换过渡**。
 *
 * 输入只有一个布尔（开着没），没有隐藏状态：静态 / 动态 Panel 的差别只在
 * "谁、什么时候把那个布尔填进去"。
 *
 * 过渡只动**颜色与变换**，绝不碰本地几何（`TaskGuide.md` D-1 的硬约束）。
 * 变换属于**变换通道**：不改命中表，所以静态档照样成立——而面板比按钮还多
 * 一层收益：它的变换**作用于整棵子树**（`GetSubtreeTransform`），于是"抽屉
 * 拉出来，里面的东西跟着出来"不用每个子节点各知道一次。
 *
 * 推进靠 `Advance(delta)`，由组件在 `onAnimationTick` 里每**渲染帧**调一次
 * （真实 delta，和画面同拍）。静态组件收不到动态档的逻辑步 `Tick`，
 * 所以每帧通道单独有一条：见 `InkingWidget::TickAnimation`。
 */
struct PanelLook {
    PanelAppearance closed{};
    PanelAppearance open{};

    /// 当前两态中的一个（**目标**态，不是"显示到哪儿了"）。
    PanelState state = PanelState::Closed;

    /// 现在**实际画出来**的颜色。没有过渡在跑时等于当前状态那份；有过渡时是中间色。
    std::uint32_t displayColor = 0xFF000000u;

    /// 现在**实际生效**的变换，与 displayColor 同步推进、同一段时长同一条曲线。
    TransformSpec displayTransform{};

    /// 从配置展开两态并把状态机与过渡复位。构造时调一次。
    void Reset(const PanelData& data) noexcept {
        closed = data.closed;
        open = data.open;
        state = data.startOpen ? PanelState::Open : PanelState::Closed;
        displayColor = For(state).color;
        displayTransform = For(state).transform;
        fromColor = displayColor;
        fromTransform = displayTransform;
        elapsed = 0.0f;
        duration = 0.0f;
        _pendingRepaint = false;
        _pendingHit = false;
    }

    /// 某一个状态对应的外观。
    const PanelAppearance& For(PanelState which) const noexcept {
        return which == PanelState::Open ? open : closed;
    }

    /// 当前状态那一份外观——注意它是**目标**那一份（配置里写的）。
    /// 过渡进行中它和屏幕上正在显示的不是一回事：要那个看 `displayColor`。
    const PanelAppearance& Current() const noexcept { return For(state); }

    bool IsOpen() const noexcept { return state == PanelState::Open; }

    /**
     * 幂等写入口：设置开着 / 关着。
     *
     * @return 状态**真的变了**才返回 true——同一个值写两次什么都不做，
     *         连过渡都不重起（重起会让正在跑的动画抖一下）。
     */
    bool SetOpen(bool value) noexcept {
        const PanelState next = value ? PanelState::Open : PanelState::Closed;
        if (next == state) {
            return false;
        }
        state = next;
        // 时长与曲线取**新状态**自己那份：关→开用 open 的、开→关用 closed 的。
        BeginTransition(For(state).transitionSeconds);
        return true;
    }

    bool IsTransitioning() const noexcept { return duration > 0.0f; }

    /**
     * 过渡的**线性**进度：0 = 刚开始，1 = 已到位。没有过渡在跑时恒为 1。
     * 这是没过缓动的那个 k——缓动只改变"画到哪儿了"，不改变"走了多久"。
     */
    float TransitionProgress() const noexcept {
        if (duration <= 0.0f) {
            return 1.0f;
        }
        const float k = elapsed / duration;
        return k < 0.0f ? 0.0f : (k > 1.0f ? 1.0f : k);
    }

    /**
     * 一帧过渡推进的结果：**变了什么**。
     *
     * 分成两项的理由与 `ButtonLook::AdvanceResult` 完全一样：`repaint` 走重绘脏、
     * `hit` 走命中脏，两者消费点不同（一个帧末记账、一个让悬停重算）。
     * 合成一个 bool 的代价是"颜色每帧变也去标命中脏"，等于每帧白重建一次绘制
     * 列表（`AGENTS.md` §6 第 40 条就是这么被抓出来的）。
     */
    struct AdvanceResult {
        bool repaint = false;  ///< 颜色或变换变了：这一帧的画面和上一帧不同
        bool hit = false;      ///< 变换变了：命中结果可能跟着变（表不用重烘）
    };

    AdvanceResult Advance(float deltaSeconds) noexcept {
        // 先结清"当场换掉显示值"留下的账（没配过渡的状态切换，见 BeginTransition）。
        // 必须在 duration 判断**之前**：那种情况 duration 就是 0，
        // 先返回的话这笔账永远报不出去（`AGENTS.md` §6 第 39 条）。
        AdvanceResult result;
        result.repaint = _pendingRepaint;
        result.hit = _pendingHit;
        _pendingRepaint = false;
        _pendingHit = false;

        if (duration <= 0.0f) {
            return result;
        }

        // 留一份上一帧的显示值，最后拿它比对出这一帧到底变没变。
        const std::uint32_t previousColor = displayColor;
        const TransformSpec previousTransform = displayTransform;

        elapsed += deltaSeconds > 0.0f ? deltaSeconds : 0.0f;
        const bool finished = elapsed >= duration;
        const float linear = finished ? 1.0f : (elapsed / duration);
        const float k = ApplyEasing(For(state).easing, linear);

        if (finished) {
            // 收尾直接落目标值，免掉插值的最后一点浮点误差。
            displayColor = For(state).color;
            displayTransform = For(state).transform;
            duration = 0.0f;
            elapsed = 0.0f;
        } else {
            displayColor = MixColor(fromColor, For(state).color, k);
            displayTransform = LerpTransform(fromTransform, For(state).transform, k);
        }

        result.repaint = result.repaint || (displayColor != previousColor)
            || !SameTransform(displayTransform, previousTransform);
        // 变换变了，命中区域就变了：同一个点可能落到别的组件上（和按钮同一条理由）。
        result.hit = result.hit
            || !SameTransform(displayTransform, previousTransform);
        return result;
    }

private:
    /// 过渡的起点 / 已用时长 / 总时长。`duration == 0` 表示没有过渡在跑。
    std::uint32_t fromColor = 0xFF000000u;
    TransformSpec fromTransform{};
    float elapsed = 0.0f;
    float duration = 0.0f;

    /**
     * 起一段过渡。起点取**当前显示值**（不是上一状态的配置值）：
     * 过渡走到一半再切状态时，从"眼睛现在看到的"接着走，不会先跳回去再出发。
     */
    void BeginTransition(float seconds) noexcept {
        fromColor = displayColor;
        fromTransform = displayTransform;

        const std::uint32_t targetColor = For(state).color;
        const TransformSpec targetTransform = For(state).transform;
        if (seconds <= 0.0f
            || (fromColor == targetColor
                && SameTransform(fromTransform, targetTransform))) {
            // 没配过渡、或者本来就一样：直接到位，连计时都不用起。
            // 但"显示值当场就变了"这件事发生在 Advance 之外，所以先记一笔
            // 待报告——漏了它，没配过渡的开关就永远标不上重绘脏。
            _pendingRepaint = _pendingRepaint || (displayColor != targetColor)
                || !SameTransform(displayTransform, targetTransform);
            _pendingHit = _pendingHit
                || !SameTransform(displayTransform, targetTransform);

            duration = 0.0f;
            elapsed = 0.0f;
            displayColor = targetColor;
            displayTransform = targetTransform;
            return;
        }
        duration = seconds;
        elapsed = 0.0f;
    }

    /// "显示值被当场换掉了、还没报告出去"的那笔账（见 BeginTransition）。
    bool _pendingRepaint = false;
    bool _pendingHit = false;
};

}  // namespace inking
