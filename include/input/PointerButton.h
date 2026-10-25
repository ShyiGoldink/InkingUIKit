#pragma once

// 指针的"按钮"这一维：**语义位，不是设备位**。
//
// ---------------------------------------------------------------------------
// 为什么需要这个文件（0.5.0）
// ---------------------------------------------------------------------------
// 0.5.0 之前整条指针链路只有"一个 bool 的按下"：`MouseState::leftButtonDown` →
// `SetPointerState(..., down, ...)` → `onPointerPress(bool)`。右键状态其实一直在
// 记（`MouseInput` 里两个 `case SDL_BUTTON_RIGHT` 都写了），但它到场景层就断了，
// 到组件契约更不存在——不是"少一个事件"，是**少一整个维度**。
//
// ---------------------------------------------------------------------------
// 本库的跨平台立场：统一输入
// ---------------------------------------------------------------------------
// 组件层永远只看到 `Primary` / `Secondary` 两种按下状态，**永远不需要知道输入
// 设备是什么**。设备差异只在窗口层被吸收：
//
//     鼠标   左键 → Primary     右键 → Secondary
//     触摸   轻点 → Primary     长按 → Secondary（场景层转义，见 InkingScene）
//
// 所以"统一输入"不是"按钮形状统一"，而是**两种输入状态在所有层同构**：同构包括
// 事件集（press / release / click）、坐标、捕获规则。凡是只给其中一种提供能力的
// 设计（只给左键坐标、只给左键拖动）都违背这条策略。
//
// `PointerSource` 只为回答"要不要长按转义"而存在，**不进入组件契约**
// （`InkingPointerHandler` 的任何钩子都不带它）。留着它是为了让"设备差异被吸收"
// 有一个明确的落点，而不是给上层留一个 `if (touch)` 的口子。

#include <cstddef>
#include <cstdint>

namespace inking {

/**
 * @brief 指针按钮。
 *
 * **语义位**而不是设备位——叫 Primary/Secondary 而不是 Left/Right，是因为单键
 * 手写笔、手柄、无障碍设备上"主/次"才是那个意思，"左/右"只是鼠标的说法。
 *
 * 加第三个值（中键 / 笔侧键）时，`kPointerButtonCount` 与 `PointerDownState`
 * 一起改，按槽并行的代码（场景的捕获槽、`PointerProbe` 的每键状态）不用动
 * ——这正是"按按钮分槽"而不是"给右键加一条特判"的收益。
 */
enum class PointerButton : std::uint8_t {
    Primary = 0,    ///< 鼠标左键 / 触摸轻点
    Secondary = 1,  ///< 鼠标右键 / 触摸长按
};

/// 按钮个数。所有"按按钮并行"的数组都以它为长度。
inline constexpr std::size_t kPointerButtonCount = 2;

/**
 * @brief 事件来源。
 *
 * **只有触摸**会走"长按 → Secondary"的转义；鼠标的左右键本来就一一对应。
 * 这个值由窗口层判定（`MouseInput` 知道事件是 `FINGER_*` 还是 `MOUSE_*`），
 * 场景只在"按下那一刻"读一次（见 `InkingScene::SetPointerState`）。
 */
enum class PointerSource : std::uint8_t {
    Mouse = 0,
    Touch = 1,
};

/// 槽位下标。所有"按按钮并行"的数组都用它索引（`_pressedNode` /
/// `_pressOrigin` / `PointerProbe::_buttons`），避免各处散落
/// `static_cast<std::size_t>`。
inline constexpr std::size_t ToIndex(PointerButton button) noexcept {
    return static_cast<std::size_t>(button);
}

/**
 * @brief 各键的按下状态（替代原来的单个 `bool down`）。
 *
 * 为什么用具名字段而不是 `bool down[kPointerButtonCount]`：调用点读起来是
 * `down.primary` 而不是 `down[0]`，而"0 是哪个键"这种约定在跨文件时一定会
 * 走形。代价是 `IsDown` / `SetDown` 要穷举——所以下面用 `static_assert` 钉住
 * 按钮个数，加新值时它会先报出来。
 */
struct PointerDownState {
    bool primary = false;
    bool secondary = false;

    /// 有任意一个键按着吗（"点外部就关"这类判定用它）。
    bool any() const noexcept { return primary || secondary; }

    bool IsDown(PointerButton button) const noexcept {
        return button == PointerButton::Primary ? primary : secondary;
    }

    void SetDown(PointerButton button, bool down) noexcept {
        if (button == PointerButton::Primary) {
            primary = down;
        } else {
            secondary = down;
        }
    }

    /// 整包比较。`InkingScene::SetPointerState` 的"真的变了没"判据靠它——
    /// 那里少比一个字段，右键按下就不会通知动态组件，定点浮窗的
    /// "点外部关闭"对右键就会静默失灵。
    friend bool operator==(const PointerDownState&, const PointerDownState&) = default;
};

static_assert(kPointerButtonCount == 2,
              "加了新的 PointerButton 就必须同步扩 PointerDownState 与 IsDown");

}  // namespace inking
