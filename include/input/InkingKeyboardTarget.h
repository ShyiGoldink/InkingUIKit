#pragma once

#include <string>

//键盘输入事件，用于获得键盘现在的内容
struct InkingKeyEvent;

//这里是键盘输入，如果接受了键盘焦点，通过重写钩子来实现对于不同输入的反应
class InkingKeyboardTarget {
public:
    virtual ~InkingKeyboardTarget() = default;

    //用于回答这个节点是否想要键盘焦点，true拿，false不拿。拿到焦点才会有后面的响应
    virtual bool WantsKeyboardFocus() const noexcept { return false; }

    //key按下，只由场景推给焦点节点。
    void PushKeyDown(const InkingKeyEvent& key) noexcept { onKeyDown(key); }

    //key抬起，只由场景推给焦点节点。
    void PushKeyUp(const InkingKeyEvent& key) noexcept { onKeyUp(key); }

    // 文本输入。输入法组合好的结果在这里
    void PushTextInput(const std::string& text) noexcept { onTextInput(text); }

    //"在不在打字"变了
    void PushTypingChanged(bool typing) noexcept { onTypingChanged(typing); }

protected:
    // 按下某个键
    virtual void onKeyDown(const InkingKeyEvent& ) {}

    // 抬起某个键
    virtual void onKeyUp(const InkingKeyEvent&) {}

    // 文本输入：输入法组合好的结果，想拿用户到底打了什么字
    virtual void onTextInput(const std::string& /*text*/) {}

    //"在不在打字变了。
    virtual void onTypingChanged(bool /*typing*/) {}

    // 键盘焦点进来了 / 出去了。
    virtual void onFocusChanged(bool) {}
};

