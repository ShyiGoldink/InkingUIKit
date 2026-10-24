# 键盘输入契约

**回到API目录：**
[API index](../API.md)

键盘输入契约：**想收键盘的组件重写这里的钩子**。

它是 Widget 的**一条可选能力，不是"另一种节点"**——`InkingWidget` 与它多继承，
于是任何 Widget 都天然有这个契约，而"我要不要参与键盘"由 `WantsKeyboardFocus()` 回答。
（Widget 的三条继承线各管一件事：`InkingAnchor` 定位、`InkingPointerHandler` 指针、
本类键盘。**输入是能力，不是第二套继承**。）

## 两条设计约束

1. **零状态**：这里不存任何字段。焦点归属（`_hasKeyboardFocus`）住在 Widget 上——
   它是场景级的**唯一归属**，天然要跟场景打交道，而接口不该知道场景。
2. **钩子即能力发现**：场景判出"谁拿着焦点"之后**直接调 `Push*`，不做任何类型查询**。
   别在这里加 `dynamic_cast` 的路子——那会把好不容易拆掉的"场景认具体类型"重新装回来。

## 事件还是状态

两者分工（与指针那边的"事件 vs 跨帧状态"同一条规矩）：

- 想"**按一下就做一件事**" → 重写 `onKeyDown` / `onTextInput`；
- 想"**按住就持续做**"（方向键移动）或"现在**在不在打字**" → 读场景的
  `GetKeyboardState()`（`IsKeyDown` / `IsTyping`）。

键盘状态是**跨帧**的（按下的键留着），事件才是一次性的——所以"按住方向键连续移动"
靠的是状态，不是重复的 `onKeyDown`（长按重复只保证 `KeyEvent::repeat` 为 `true`）。

你可以使用 `#include <input/InkingKeyboardTarget.h>` 来引入该头文件。

## 用到的数据结构

>**KeyEvent**

本头里**只做前向声明**：

```C++
struct KeyEvent;
```

`PushKeyDown` / `PushKeyUp` / 两个钩子都**只按引用传它**，不需要完整类型，
所以不必把 `<input/KeyboardInput.h>` 拖进这个头（公开头不拖依赖那条硬约定）。
它的字段说明在 `include/input/KeyboardInput.h`，不在本文件。

注意它是 `struct`（不是 union）——前向声明必须与定义同形，
这一点与 `SDL_Event`（union）正好相反。

## 属性

**本类零状态，没有任何字段**——这是设计约束 1。

焦点归属住在 Widget 上：`InkingWidget::HasKeyboardFocus()` 读它、
`InkingWidget::MarkKeyboardFocus(bool)` 写它（只由场景调）。判断"我有没有焦点"
在钩子里用 `HasKeyboardFocus()`，不要自己存一份——那份迟早和场景不同步。

## APIS

### Public

>**WantsKeyboardFocus()**
原名:

```C++
virtual bool WantsKeyboardFocus() const noexcept;
```

**这个节点想不想要键盘焦点？默认不要。**

只有返回 `true` 的节点才会在"被点击"时拿到焦点——按钮这类"点一下就完事"的控件
不需要（它由指针事件驱动就够），而输入框 / 可编辑文本 / 需要方向键操作的容器才要。

它和 `HitTest` 一样是**组件知识**："我要不要参与键盘"只有组件自己知道。
（默认 `false` 是个**不报错**的坑：忘了重写就永远拿不到焦点，且没有任何提示。）

>**PushKeyDown(key)**
原名:

```C++
void PushKeyDown(const KeyEvent& key) noexcept;
```

按键**按下**（转给 `onKeyDown`）。**只由场景推给焦点节点**。

>**PushKeyUp(key)**
原名:

```C++
void PushKeyUp(const KeyEvent& key) noexcept;
```

按键**抬起**（转给 `onKeyUp`）。只由场景推给焦点节点。

>**PushTextInput(text)**
原名:

```C++
void PushTextInput(const std::string& text) noexcept;
```

**文本输入**（转给 `onTextInput`）。输入法组合好的结果在这里。

>**PushTypingChanged(typing)**
原名:

```C++
void PushTypingChanged(bool typing) noexcept;
```

"**在不在打字**"变了（转给 `onTypingChanged`）。

它在**有键按下 ↔ 全部抬起**时各来一次；**获得焦点时也会补一次当前的状态**——
否则组件要等到下一次变化才知道"现在其实已经按着键"。

>**~InkingKeyboardTarget()**
原名:

```C++
virtual ~InkingKeyboardTarget() = default;
```

虚析构（默认实现）。本类要被继承，而且和 `InkingWidget` 是**多继承**关系
（场景通过基类指针调钩子），所以必须是虚的。

> **`Push*` 是 `noexcept`，而钩子本身不是**：钩子里的异常逃出去会直接
> `std::terminate`。重写时别让异常离开钩子。

### Protected Methods

>**onKeyDown(key)**
原名:

```C++
virtual void onKeyDown(const KeyEvent& key);
```

按下某个键。**长按重复也会来**（`KeyEvent::repeat` 为 `true`）。
默认什么都不做——"什么都不做"是正确的默认值：绝大多数组件本来就不关心键盘。

>**onKeyUp(key)**
原名:

```C++
virtual void onKeyUp(const KeyEvent& key);
```

抬起某个键。

>**onTextInput(text)**
原名:

```C++
virtual void onTextInput(const std::string& text);
```

文本输入：**输入法组合好的结果**（一个中文字可能来自多次按键）。
想拿"用户到底打了什么字"就用这个，**别自己去拼 `onKeyDown`**。

>**onTypingChanged(typing)**
原名:

```C++
virtual void onTypingChanged(bool typing);
```

"在不在打字"变了。用途例如"正在输入 → 暂停自动播放 / 隐藏提示"。

>**onFocusChanged(focused)**
原名:

```C++
virtual void onFocusChanged(bool focused);
```

键盘焦点进来了 / 出去了。

**它是唯一一个"不要求在焦点内"的钩子**：其余四个只在本节点**正拿着**键盘焦点时
被调用，而它在**焦点变化那一刻**来一次（进入时 `true`、离开时 `false`）。
由 `InkingWidget::MarkKeyboardFocus` 触发——那条路只认基类指针，
所以重写时**要写 `override`**，否则从基类指针调过来时静默不生效。

---

> 本文档对应版本：**0.4.0（0.x 发布版）**。
