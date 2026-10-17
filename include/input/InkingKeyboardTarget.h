#pragma once

// 键盘输入契约：想收键盘的组件重写这里的钩子。
//
// 它是 Widget 的一条可选能力，不是"另一种节点"——`InkingWidget` 与它多继承，
// 于是任何 Widget 都天然有这个契约，而"我要不要参与键盘"由`WantsKeyboardFocus()` 回答
//
// 两条设计约束（照 `InkingWidget.h` 顶上那段抄的）：
//
//   1. 零状态：这里不存任何字段。焦点归属（`_hasKeyboardFocus`）住在 Widget 上
//      它是场景级的唯一归属，天然要跟场景打交道，而接口不该知道场景；
//   2. 钩子即能力发现：场景判出"谁拿着焦点"之后直接调 `Push*`，**不做任何类型查询**。
//      别在这里加 `dynamic_cast` 的路子.那会把好不容易拆掉的"场景认具体类型"重新装回来
//      
//      事件与状态的对应关系（`include/input/KeyboardInput.h` 顶上那段）：
//   - 想"按一下就做一件事" → 重写 `onKeyDown` / `onTextInput`；
//   - 想"按住就持续做"（方向键移动）或"现在在不在打字" → 读场景的
//     `GetKeyboardState()`（`IsKeyDown` / `IsTyping`）。

#include <string>

namespace inking {

// 键盘事件。只前向声明：`PushKeyDown` / 钩子都只按引用传它，
// 不需要完整类型，所以不必把 `<input/KeyboardInput.h>` 拖进这个头
// （`AGENTS.md` §6 第 8 条）。
struct KeyEvent;

/**
 * @brief 键盘目标：能拿焦点、能收键盘事件的组件。
 *
 * 钩子全是空实现"什么都不做"是正确的默认值：绝大多数组件本来就不关心键盘。
 */
class InkingKeyboardTarget {
public:
    virtual ~InkingKeyboardTarget() = default;

    /**
     * 这个节点想不想要键盘焦点？默认不要。
     *
     * 只有返回 `true` 的节点才会在"被点击"时拿到焦点——按钮这类"点一下就完事"
     * 的控件不需要（它由指针事件驱动就够），而输入框 / 可编辑文本 / 需要方向键
     * 操作的容器才要。
     *
     * 它和 `HitTest` 一样是组件知识："我要不要参与键盘"只有组件自己知道。
     */
    virtual bool WantsKeyboardFocus() const noexcept { return false; }

    /** 按下（转给 `onKeyDown`）。只由场景推给焦点节点。 */
    void PushKeyDown(const KeyEvent& key) noexcept { onKeyDown(key); }

    /** 抬起（转给 `onKeyUp`）。只由场景推给焦点节点。 */
    void PushKeyUp(const KeyEvent& key) noexcept { onKeyUp(key); }

    /** 文本输入（转给 `onTextInput`）。输入法组合好的结果在这里。 */
    void PushTextInput(const std::string& text) noexcept { onTextInput(text); }

    /**
     * "在不在打字"变了（转给 `onTypingChanged`）。
     *
     * 它在**有键按下 ↔ 全部抬起**时各来一次；获得焦点时也会补一次当前的
     * 状态——否则组件要等到下一次变化才知道"现在其实已经按着键"。
     */
    void PushTypingChanged(bool typing) noexcept { onTypingChanged(typing); }

protected:
    // ---------------- [√] 可重写钩子 ----------------
    //
    // 这几个只会在本节点拿着键盘焦点时被调用（`onFocusChanged` 是例外，
    // 它在焦点变化时来一次）。
    //
    // 空实现必须**写在头里**（`{}`），理由同 `InkingPointerHandler.h` 那段：
    // 只声明不定义 = 基类 vtable 引用一个不存在的符号，**任何**派生类都会在
    // 链接期报 `undefined reference`。`InkingWidget` 自己就在调
    // `onFocusChanged`，所以这不是"将来才会踩"。

    /// 按下某个键（长按重复也会来，`KeyEvent::repeat` 为 true）。
    virtual void onKeyDown(const KeyEvent& /*key*/) {}

    // 抬起某个键。
    virtual void onKeyUp(const KeyEvent& /*key*/) {}

    // 文本输入：输入法组合好的结果（一个中文字可能来自多次按键）。
    // 想拿"用户到底打了什么字"就用这个，别自己去拼 `onKeyDown`。
    virtual void onTextInput(const std::string& /*text*/) {}

    // "在不在打字"变了。用途例如"正在输入 → 暂停自动播放 / 隐藏提示"。
    virtual void onTypingChanged(bool /*typing*/) {}

    // 键盘焦点进来了 / 出去了。
    virtual void onFocusChanged(bool /*focused*/) {}
};

}  // namespace inking
