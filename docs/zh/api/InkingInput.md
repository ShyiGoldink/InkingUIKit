# 输入状态层

**回到API目录：**
[API index](../API.md)

本页覆盖"把 SDL 事件变成可用状态"的这一层，以及快捷键与文本编辑状态：

| 头文件 | 内容 |
| --- | --- |
| `input/KeyboardInput.h` | `KeyEvent` / `KeyInputEvent` / `KeyboardState` / `KeyboardInput` |
| `input/MouseInput.h` | `MousePosition` / `MousePoint` / `MouseState` / `MouseInput` |
| `input/Shortcut.h` | `Shortcut`（值类型：解析 / 匹配 / 打印） |
| `input/ShortcutRegistry.h` | 进程级快捷键绑定表 |
| `input/TextEditState.h` | 文本框的编辑状态（光标 / 选区 / UTF-8 边界） |

这一层**不碰渲染器、不碰命中**，只把事件累积成"跨帧状态 + 本帧事件"。
谁收到、什么时候收到，由场景层决定（见 [InkingScene.md](InkingScene.md)）。

**"想收指针 / 键盘"的组件不读本页**：它们重写钩子就行，
见 [InkingPointerHandler.md](InkingPointerHandler.md) 与
[InkingKeyboardTarget.md](InkingKeyboardTarget.md)。本页是**框架内部与调试**用的那一层。

## 一、状态 vs 事件：两条路都给

| | 适合 | 生命周期 |
| --- | --- | --- |
| **状态**（`KeyboardState`） | "按住方向键就移动"、"现在在不在打字" | **跨帧**（按下的键留着） |
| **事件**（`KeyInputEvent`） | "按下 Enter 就提交" | **本帧**（`ResetFrameFlags` 清掉） |

用状态轮询一次性事件，写法是"上一帧没按、这一帧按了"——边沿检测是框架该做的事，
不该让每个组件各写一遍。这就是两条路都要的原因。

鼠标同理：`MouseState::moved` / `wheelX` / `wheelY` 是**本帧发生过的事**（帧末清），
而 `leftButtonDown` / 位置是**跨帧状态**。

## 二、键盘

>**KeyEvent**

```C++
struct KeyEvent {
    std::int32_t key = 0;       // SDL_Keycode（例如 SDLK_SPACE）
    std::int32_t scancode = 0;  // 物理键位（与键盘布局无关）
    bool repeat = false;        // 长按重复
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool super = false;
};
```

`key` 存成 `std::int32_t` 是为了不把 SDL 拖进公开头。要写可读的键名就
`#include <SDL3/SDL_keycode.h>` 用 `SDLK_*` 去比——**框架内的组件可以这么做**，
业务代码应该自己把键位收成配置。

`scancode` 是**物理键位**：WASD 这类"按位置"的操作应该用它，否则 AZERTY 键盘上会错位。

>**KeyInputEvent**

```C++
struct KeyInputEvent {
    enum class Kind { Down, Up, Text };
    Kind kind = Kind::Down;
    KeyEvent key{};    // Down / Up 时有效
    std::string text;  // Text 时有效（SDL_EVENT_TEXT_INPUT，已含输入法结果）
};
```

>**KeyboardState**

```C++
bool IsKeyDown(std::int32_t key) const noexcept;
bool IsScancodeDown(std::int32_t scancode) const noexcept;
bool IsShiftDown() / IsCtrlDown() / IsAltDown() / IsSuperDown() const noexcept;
bool IsTyping() const noexcept;        // 现在有任何键按着吗
std::size_t DownCount() const noexcept;

void Press(key, scancode) noexcept;    // 只给 KeyboardInput 调
void Release(key) noexcept;
void SetModifiers(shift, ctrl, alt, super) noexcept;
void ClearHeldKeys() noexcept;
```

`IsTyping()` 就是"现在有任何键按着吗"。它的**变化**会作为一次事件推给焦点组件
（`onTypingChanged`）；业务可以拿它做"正在输入 → 暂停自动播放"。
注意它**不是**"输入法正在组合"——那个要看 TEXT_INPUT。

**`ClearHeldKeys()` 在窗口失焦时必须调**：按着键切到别的窗口，抬起事件永远不会来，
于是 `IsTyping()` 一直卡在 true、`IsKeyDown(方向键)` 永远成立——"状态与事实不符"的
经典死法，而且不报错，只是行为诡异。它**不清本帧事件**：这一帧已经发生的事是事实。

>**KeyboardInput**

```C++
void UpdateFromSDL(const SDL_Event& event);
void ResetFrameFlags() noexcept;      // 只清本帧事件，不清按下的键
void ClearHeldKeys() noexcept;
const KeyboardState& GetState() const noexcept;
const std::vector<KeyInputEvent>& GetFrameEvents() const noexcept;
bool ConsumeFrameEvent(std::size_t index) noexcept;
```

`ConsumeFrameEvent` 的消费者是**全局快捷键**：命中之后那一下按键不该再推给焦点组件。
两条边界都是有意为之：

- 只动**本帧事件**，**不动"按着"的状态**——`IsKeyDown(Ctrl)` 仍为真，
  那是"按住就持续"的逻辑要的；
- 越界与重复消费都是安全的空操作（返回 false），因为派发方会先收集命中下标、再倒序消费。

## 三、鼠标与触摸

>**MouseState**

```C++
struct MouseState {
    MousePosition position{};   // 窗口像素，SDL 事件给的原样
    MousePoint design{};        // 设计坐标，窗口层每帧换算后写入
    bool leftButtonDown = false;
    bool rightButtonDown = false;
    bool middleButtonDown = false;
    bool moved = false;         // 本帧移动过（帧末清）
    float wheelX = 0.0f;        // 本帧累积滚动量（帧末清）
    float wheelY = 0.0f;
};
```

滚轮方向按 SDL3 口径（`SDL_MOUSEWHEEL_FLIPPED` 已在 `UpdateFromSDL` 里翻正）：
`wheelY > 0` = 远离用户（滚轮向上 / 内容上滚），`wheelX > 0` = 向右。
数值是**滚动量、不是格数**——鼠标滚轮通常一步 ±1，触控板会是小数。

>**MouseInput**

```C++
void UpdateFromSDL(const SDL_Event& event);
void SetDesignPosition(float x, float y) noexcept;
void SetWindowSize(float width, float height) noexcept;
bool IsTouchActive() const noexcept;
std::int64_t GetTouchFinger() const noexcept;
void ResetFrameFlags() noexcept;

const MouseState& GetState() const noexcept;
bool IsLeftDown() / IsRightDown() / IsMiddleDown() / IsMoved() const noexcept;
float GetWheelX() / GetWheelY() const noexcept;
bool IsWheelScrolled() const noexcept;
```

三件事值得单独说：

1. **坐标换算不在这一层**：窗口坐标 → 设计坐标要考虑 letterbox 的偏移与留边，
   那是**渲染器**的知识，所以由窗口层算完写进来（`SetDesignPosition`）。
   `MouseInput` 自己不去碰渲染器。
2. **触摸给的是归一化坐标**（0~1），要乘 `SetWindowSize` 喂进来的窗口像素尺寸才是窗口坐标
   ——之后与鼠标共用同一条路，所以触摸不需要第二条通路。
3. **只认第一根手指**。多指针的结论是"这一版不做"：两根手指同时按会互相抢一个指针状态，
   表现是"点击乱跳"，那比"只认第一根"糟得多。后来的手指**明确忽略**；第一根抬起之后
   下一根可以接管。

## 四、快捷键

### Shortcut：一个值类型

```C++
class Shortcut {
public:
    static constexpr std::uint8_t kShift = 1u;
    static constexpr std::uint8_t kCtrl  = 2u;
    static constexpr std::uint8_t kAlt   = 4u;
    static constexpr std::uint8_t kSuper = 8u;   // Win 键 / Command 键

    Shortcut() = default;
    Shortcut(std::int32_t keyCode, std::uint8_t modifierMask, bool repeats = false);

    bool Empty() const noexcept;                 // key == 0
    bool Matches(const KeyEvent& event) const noexcept;
    static Shortcut Parse(std::string_view text, bool* ok = nullptr);
    std::string ToString() const;
    bool operator==(const Shortcut&) const noexcept;
    bool operator!=(const Shortcut&) const noexcept;
};
```

**两条语义先钉住**：

1. **修饰键完全匹配**。绑了 `Ctrl+S`，按 `Ctrl+Shift+S` **不**触发。
   理由：宽松匹配会让"保存"吃掉将来的"另存为"，而那种 bug 极难发现
   （按下没反应，或者两个动作都发生）。要宽松匹配就显式加一档，不默认。
2. **长按默认不重复触发**（`allowRepeat = false`）。快捷键是"命令"，按住不放连发十次
   几乎没有一次是用户想要的；要连发的（例如缩放）设成 true 即可。

`Parse` 接受 `"Ctrl+Shift+S"` 这种写法（配置里迟早要写它，所以现在就把双向定下来）。
`ok` 返回解析是否成功。

### ShortcutRegistry：进程级的一张表

```C++
using Action = std::function<void()>;
using Id = std::uint64_t;

Id Bind(const Shortcut& shortcut, Action action, std::string label = {});
bool Unbind(Id id);
bool IsTaken(const Shortcut& shortcut) const noexcept;
Id Find(const Shortcut& shortcut) const noexcept;
std::size_t Count() const noexcept;
std::string GetLabel(Id id) const;
std::size_t Dispatch(KeyboardInput& input);
void Clear() noexcept;
```

**为什么这张表必须住在窗口层**：场景的键盘派发是"推给焦点节点"，它天生**不全局**
——焦点在输入框里时 `Ctrl+S` 会被输入框拿走。而快捷键的语义恰恰是"不管焦点在哪都要响应"，
所以它必须住在焦点**之上**。窗口层是唯一合适的位置（它同时持有键盘输入与主循环）。

**生命周期**：表活在进程级的 `InkingWindow` 里，注册者（例如 `MenuBar`）会先死。
所以**注册者必须在析构时 `Unbind`**（`Bind` 返回的 id 就是干这个的）。

`Bind` 遇到已被占用的组合会**先到先得 + 打一条 WARN**（`ShortcutRegistry.h` 类的约定）。
`Dispatch` 返回这一帧命中了几个，并把命中的 Down 事件从 `KeyboardInput` 的本帧事件里
消费掉。

## 五、TextEditState：文本框的编辑状态

```C++
explicit TextEditState(std::string text);

const std::string& Text() const noexcept;
void SetText(std::string text);
bool Empty() const noexcept;
std::size_t Size() const noexcept;

std::size_t Caret() const noexcept;
void SetCaret(std::size_t byteOffset) noexcept;
std::size_t SelectionBegin() / SelectionEnd() const noexcept;
bool HasSelection() const noexcept;
std::string SelectedText() const;
void SelectAll() noexcept;
void ClearSelection() noexcept;

void MoveLeft(bool select = false) noexcept;
void MoveRight(bool select = false) noexcept;
void MoveHome(bool select = false) noexcept;
void MoveEnd(bool select = false) noexcept;
void MoveTo(std::size_t byteOffset, bool select = false) noexcept;

void InsertText(std::string_view utf8);
void Backspace();
void DeleteForward();
std::string CopySelection() const;
std::string CutSelection();
void Paste(std::string_view text);

const Change& LastChange() const noexcept;   // 最近一次改动（含种类与范围）

// UTF-8 边界工具
static bool IsBoundary(std::string_view text, std::size_t pos) noexcept;
static std::size_t SnapToBoundary(std::string_view text, std::size_t pos);
static std::size_t PrevCodePoint(std::string_view text, std::size_t pos);
static std::size_t NextCodePoint(std::string_view text, std::size_t pos);
```

**所有位置都是 UTF-8 字节偏移**，不是"第几个字"。`MoveLeft/Right` 走的是**码点**边界，
所以中文 / emoji 不会被切成半个字符。自己算位置时用 `SnapToBoundary` 收一下。

它是**纯状态机**（不含渲染、不含输入）：`InkingTextInput` 持有它、把按键翻译成这里的调用、
再按 `LastChange` 决定要不要重排 / 重绘。

## 常见坑

1. **窗口失焦没 `ClearHeldKeys()`**：`IsTyping` / `IsKeyDown` 永远为真。
2. **`ResetFrameFlags()` 之后还去读 `GetFrameEvents()`**：已经是空的。派发必须在帧末之前。
3. **拿 `Shortcut` 做宽松匹配**：框架故意完全匹配；要宽松自己写一档。
4. **注册快捷键不 `Unbind`**：析构之后表里还挂着 `this`，下一次按键就是悬垂调用。
5. **把 `TextEditState` 的位置当字符索引**：它是字节偏移。
6. **以为触摸支持多指**：只认第一根，后来的明确忽略。

---

> 本文档对应版本：**0.1.0-test（测试版本）**。
