#pragma once

// 键盘输入：**状态可以轮询、事件可以订阅**——两条路都给。
//
// 为什么两条都要（`TaskGuide.md` A3 定的）：
//   - **状态**（`KeyboardState`）：适合"按住方向键就移动""现在在不在打字"这类
//     每帧问一次的问题。轮询比事件省事，也不会漏（事件只来一次，错过了就没了）；
//   - **事件**（`KeyInputEvent`）：适合"按下 Enter 就提交"这类**一次性**反应。
//     用状态轮询它的写法是"上一帧没按、这一帧按了"，自己维护边沿检测——那是
//     框架该做的事，不该让每个组件各写一遍。
//
// 事件的到达方式与指针钩子（A1）对称：场景把事件推给**焦点节点**，
// 组件重写 `onKeyDown` / `onKeyUp` / `onTextInput` 就是"订阅"——
// 编译期确定、零分配、没有回调列表要维护（`AGENTS.md` §6 第 8 条那条
// "公开头不拖第三方"也顺带满足：这里只用到 `std::int32_t`）。
//
// 这一层与 `MouseInput` 一样：**不碰渲染器、不碰命中**，只把 SDL 事件累积成
// "跨帧状态 + 本帧事件"。谁收到、什么时候收到，由场景层决定。
//
// 实现见 `src/input/KeyboardInput.cpp`。

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// 只做前向声明：公开头不拖 SDL 进来（`AGENTS.md` §6 第 8 条）。
// `SDL_Event` 是 **union**，前向声明必须写 union。
union SDL_Event;

namespace inking {

/**
 * 一次按键（按下 / 抬起）。
 *
 * `key` 的值就是 SDL 的 `SDL_Keycode`（例如 `SDLK_SPACE`）；这里存成
 * `std::int32_t` 是为了不把 SDL 拖进公开头。组件要写可读的键名就
 * `#include <SDL3/SDL_keycode.h>` 用 `SDLK_*` 常量去比——
 * **框架内的组件可以这么做**，业务代码则应该自己把键位收成配置。
 *
 * `scancode` 是**物理键位**（与键盘布局无关）：WASD 这类"按位置"的操作
 * 应该用它，否则 AZERTY 键盘上会错位。
 */
struct KeyEvent {
    std::int32_t key = 0;       ///< SDL_Keycode
    std::int32_t scancode = 0;  ///< 物理键位
    bool repeat = false;        ///< 长按重复（SDL 的 `key.repeat`）
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool super = false;
};

/// 本帧累积的一个事件。三种：按下、抬起、文本。
struct KeyInputEvent {
    enum class Kind { Down, Up, Text };

    Kind kind = Kind::Down;
    KeyEvent key{};    ///< Kind::Down / Kind::Up 时有效
    std::string text;  ///< Kind::Text 时有效（`SDL_EVENT_TEXT_INPUT`，已含输入法结果）
};

/**
 * 键盘的**跨帧状态**：现在按着哪些键、修饰键、在不在"打字"。
 *
 * "在不在打字"（`IsTyping`）就是"现在有任何键按着吗"。它的**变化**会作为一次
 * 事件推给焦点组件（`onTypingChanged`）——业务可以拿它做"正在输入 → 暂停自动
 * 播放 / 隐藏提示"这类事。注意它**不是**"输入法正在组合"：那个要看 TEXT_INPUT。
 */
class KeyboardState {
public:
    bool IsKeyDown(std::int32_t key) const noexcept;
    bool IsScancodeDown(std::int32_t scancode) const noexcept;

    bool IsShiftDown() const noexcept { return _shift; }
    bool IsCtrlDown() const noexcept { return _ctrl; }
    bool IsAltDown() const noexcept { return _alt; }
    bool IsSuperDown() const noexcept { return _super; }

    /// 现在有任何键按着吗（"正在输入"）。
    bool IsTyping() const noexcept { return !_down.empty(); }

    /// 现在按着几个键（自检与诊断用）。
    std::size_t DownCount() const noexcept { return _down.size(); }

    // ---------------- 只给 KeyboardInput 调 ----------------
    // 做成 public 是为了让 `KeyboardInput` 直接改它，不必 friend；
    // 组件侧只读，不要自己去 Press/Release（那会让状态和 SDL 不一致）。

    void Press(std::int32_t key, std::int32_t scancode) noexcept;
    void Release(std::int32_t key) noexcept;
    void SetModifiers(bool shift, bool ctrl, bool alt, bool super) noexcept;

    /**
     * 清掉所有"按着"的键与修饰键。
     *
     * **窗口失焦时必须调**：按着键切到别的窗口，抬起事件永远不会来，于是
     * `IsTyping()` 会一直卡在 true、`IsKeyDown(方向键)` 会永远成立——那是
     * "状态与事实不符"的经典死法（而且不报错，只是行为诡异）。
     * 它**不清本帧事件**：这一帧已经发生的事是事实，不该被抹掉。
     */
    void ClearHeldKeys() noexcept {
        _down.clear();
        _shift = false;
        _ctrl = false;
        _alt = false;
        _super = false;
    }

private:
    struct DownKey {
        std::int32_t key = 0;
        std::int32_t scancode = 0;
    };

    /// 按下的键。用 vector 而不是 set：同时按下的键通常个位数，
    /// 线性查比哈希快，而且没有哈希表的分配与迭代器失效问题。
    std::vector<DownKey> _down;

    bool _shift = false;
    bool _ctrl = false;
    bool _alt = false;
    bool _super = false;
};

/**
 * @brief 键盘输入：把 SDL 事件累积成"跨帧状态 + 本帧事件"。
 *
 * 窗口层每帧**喂**（`UpdateFromSDL`），主循环里派发一次，帧末清瞬时
 * （`ResetFrameFlags`）。结构对照 `MouseInput`。
 */
class KeyboardInput {
public:
    /// 由 SDL 事件循环直接驱动：按下 / 抬起 / 文本输入都在这里累积。
    void UpdateFromSDL(const SDL_Event& event);

    /**
     * 帧末清瞬时。
     *
     * **只清"本帧事件"，不清按下的键**：后者是跨帧状态——"按住方向键"
     * 之所以能每帧都成立，就是因为它不会在帧末被清掉。
     */
    void ResetFrameFlags() noexcept;

    /**
     * 窗口失焦时清掉"按着"的状态（见 `KeyboardState::ClearHeldKeys`）。
     * 由窗口层在 `SDL_EVENT_WINDOW_FOCUS_LOST` 时调。
     */
    void ClearHeldKeys() noexcept { _state.ClearHeldKeys(); }

    const KeyboardState& GetState() const noexcept { return _state; }

    /// 本帧累积的事件（按下 / 抬起 / 文本），顺序就是它们到达的顺序。
    const std::vector<KeyInputEvent>& GetFrameEvents() const noexcept {
        return _events;
    }

    /**
     * 删掉本帧的第 `index` 条事件。
     *
     * 谁用它：**全局快捷键**。命中之后那一下按键不该再推给焦点组件
     * （否则输入框里会冒出一个控制字符，或者按钮被"空格"顺带按一次）。
     *
     * 两条边界，都是有意为之：
     *   - 只动**本帧事件**，**不动"按着"的状态**——`IsKeyDown(Ctrl)` 仍然为真，
     *     那是游戏 / 拖拽这类"按住就持续"的逻辑要的；
     *   - 越界与重复消费都是安全的空操作（返回 false），因为派发方会先收集
     *     命中下标、再倒序消费。
     */
    bool ConsumeFrameEvent(std::size_t index) noexcept;

private:
    KeyboardState _state{};
    std::vector<KeyInputEvent> _events{};
};

}  // namespace inking
