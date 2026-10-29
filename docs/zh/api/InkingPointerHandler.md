# 指针输入契约

**回到API目录：**
[API index](../API.md)

指针（鼠标 / 触摸）输入契约：**想收指针的组件重写这里的钩子**。

它是 Widget 的**一条可选能力，不是另一种节点**——`InkingWidget` 与它多继承，
所以任何 Widget 都天然有这个契约。重写 `onPointerHover` / `onPointerPress` /
`onPointerClick`（以及 `onPointerWheel` / `onPointerDrag`）就能收输入；
没重写的拿默认空实现，什么都不会发生。

## 两种输入状态（0.5.0）

`onPointerPress` / `onPointerClick` 带 `PointerButton` 参数。上层**永远只看到
`Primary` / `Secondary`**，不需要知道这是鼠标左键、右键，还是触摸的轻点、长按——
设备差异在窗口层与场景层就被吸收干净了（见 `<input/PointerButton.h>` 的文件头）。

| 设备输入 | 语义 |
| --- | --- |
| 鼠标左键 | `Primary` |
| 鼠标右键 | `Secondary` |
| 触摸轻点 | `Primary`（press → release → click） |
| 触摸长按（≥ 0.5s 且没滑动） | `Secondary`（press → release → click） |

这不是"给触摸做补偿"，而是本库的**跨平台立场**：两种输入状态在**所有层同构**，
同构包括事件集、坐标、捕获规则。所以这里没有"触摸分支"，也不该出现。

三个钩子的参数形状**故意不同**：

| 钩子 | 参数 | 为什么 |
| --- | --- | --- |
| `onPointerHover` | `bool` | 只有主键有 hover——hover 是"我指着谁"，不是"我按着谁" |
| `onPointerPress` | `PointerButton, bool` | 按下不关心位置；要位置问场景 |
| `onPointerClick` | `PointerButton, float, float` | **必须**带坐标——它是事件，不带走就永久丢失 |

坐标是**按下点**（不是抬起点）：触摸长按的锚点是按下处，手指微动几像素不该让
菜单跟着挪；鼠标右键按下与抬起通常同点，两者无差异。

## 两条设计约束

1. **零状态**：这里不存任何字段。**拖动归属（"场景记住谁被按下"）住在场景上**——
   和键盘那份的焦点归属一样，是场景级的唯一归属，所以接口不该知道场景。
2. **钩子即能力发现**：场景判出"指针在谁身上 / 谁被按下 / 谁被抬起"之后，
   把判定结果用 `Push*` 推给那个节点，**不做任何类型查询**。

   > 历史教训：`DispatchPointer` 早年会把**任何**非动态节点
   > `static_cast<InkingStaticButton*>` 再调 `MouseHover`——第一个不继承 Button 的
   > 可命中组件一被指针扫到就是 **UB**。切勿在这里加 `dynamic_cast` 的路子，
   > 那是把那个 bug 装回来。

## 重写要写 override

派生类重写钩子时**要写 `override`**：漏了就变成"隐藏"而不是"重写"，
而场景是拿**基类指针**调钩子的——**静默不生效**（不报错、只是没反应）。

## 四个"归属"（这份契约里最容易混的地方）

| 输入 | 归谁 |
| --- | --- |
| **hover** | 指针下**最上面**那个（场景判出来）。**只跟主键**：右键按住不改变它 |
| **按下 / 拖动**（pointer capture） | 按下时命中的那个节点**抓住**这次交互，之后指针怎么移都只推给它（跨组件不被抢、移出矩形也算它的）；**抬起或窗口失焦时释放**。**只认主键** |
| **次键按下 / 点击**（0.5.0） | 同样有捕获，但**按按钮分槽**：与主键各记各的，左右同按互不覆盖。次键不参与 hover / 拖动 / 滚轮 / 键盘焦点 |
| **滚轮** | 正常给"指针下最上面那个"；若这时正按着某个节点（拖动捕获生效中），**改推给捕获者** |
| **滚轮（容器优先）** | 谁声明了 `HandlesSubtreeWheel()`，场景就**沿父链找最近的声明者**，把滚轮先交给它 |

滚轮那两行是**先后关系**：先看有没有声明者（容器优先），没有才按"最上面那个 / 捕获者"。
无论哪种路径，滚轮的**目标都是唯一的**，不会同时给两个组件。

你可以使用 `#include <input/InkingPointerHandler.h>` 来引入该头文件。

## 五个钩子必须在这里给空实现（`{}`）

这是本文件的一条**硬约束**，注释里写得很直白：

> 它们都是虚函数，而"没有重写它们"是**正常情况**——绝大多数组件不关心指针。
> 只声明不定义的话，**基类 vtable 会引用一个不存在的符号**，会造成任何派生类
> （哪怕一个钩子都不重写）在**链接期**拿到
> `undefined reference to inking::InkingPointerHandler::onPointerWheel`。

所以这五个 `{}` 不是代码风格，**删掉就编不过**——而且报错的是链接期、
离现场很远。

## 用到的数据结构

`PointerButton` / `PointerSource` / `PointerDownState` / `ToIndex()`，来自
`<input/PointerButton.h>`（纯值类型，不依赖 SDL）。本文件不再"零依赖"——0.5.0
之前它确实不引用任何类型，因为那时只有"一个 bool 的按下"。

## 属性

**本类零状态，没有任何字段**——这是设计约束 1。

拖动归属住在场景上（"场景记住谁被按下"）；判断"我是不是捕获者"要问场景，
不要自己存一份——那份迟早和场景不同步。

## APIS

### Public

>**HandlesSubtreeWheel()**
原名:

```C++
virtual bool HandlesSubtreeWheel() const noexcept;
```

**先取本子树的滚轮**。默认 `false`。

滚轮原本给"指针下最上面那个"，而滚动容器里指针下通常是容器里的**子按钮**——
如果容器收不到滚轮，"能直接滚"就成了空话。声明之后，场景会沿父链找**最近**的声明者，
把滚轮先交给它（即用户裁定的**容器优先**）。

它和 `OwnsSubtreeHit()` 是**同一族机制**：组件说一句"我要"，框架负责找路，
**场景层不认识"滚动容器"这个具体类型**。

沿父链找那一步**不在这个接口里**（它要用树结构），见
`InkingWidget::FindSubtreeWheelOwner()`——那句话也只有它留在 Widget 层。

>**PushPointerHover(hover)**
原名:

```C++
void PushPointerHover(bool hover) noexcept;
```

指针**进出**：`true` 进入、`false` 离开（转给 `onPointerHover`）。

>**PushPointerPress(button, press)**
原名:

```C++
void PushPointerPress(PointerButton button, bool press) noexcept;
```

**按下 / 抬起**：`true` 按下、`false` 抬起（转给 `onPointerPress`）。

`button` 是**语义位**（`Primary` = 左键 / 轻点，`Secondary` = 右键 / 长按），
不是设备位——组件既不需要、也拿不到"这是鼠标还是手指"。

按下与抬起走**同一条**（而不是两个入口），因为"回哪一态 / 还算不算一次点击"
是**组件自己的知识**：按钮按下滑出去再松开要回 Normal，不是 Hover。

>**PushPointerClick(button, designX, designY)**
原名:

```C++
void PushPointerClick(PointerButton button, float designX,
                      float designY) noexcept;
```

**一次完整的点击**（按下与抬起都发生在它身上）（转给 `onPointerClick`）。

`designX` / `designY` 是**按下点**（设计坐标），不是抬起时的位置。click 是**事件**：
发生之后状态就变了，不带走那一刻的坐标就永久丢失——所以它带坐标，而
`PushPointerPress` 不带。

>**PushPointerWheel(deltaX, deltaY)**
原名:

```C++
void PushPointerWheel(float deltaX, float deltaY) noexcept;
```

**滚轮**（转给 `onPointerWheel`）。归属见上面的表：指针下最上面的那个静态组件；
若这时正按着某个节点（拖动捕获生效中），改推给捕获者。

>**PushPointerDrag(designX, designY, deltaX, deltaY)**
原名:

```C++
void PushPointerDrag(float designX, float designY, float deltaX,
                     float deltaY) noexcept;
```

**拖动**（转给 `onPointerDrag`）。**只有捕获者收得到**：按下时命中的那个节点抓住这次
交互，之后指针怎么移都只推给它（跨组件不被抢，移出矩形也算它的）；**抬起或窗口失焦时释放**。

这就是设计稿里说的 **pointer capture**，只不过落点从"路由层"换成了
"场景记住谁被按下"。

>**~InkingPointerHandler()**
原名:

```C++
virtual ~InkingPointerHandler() = default;
```

虚析构（默认实现）。本类要被继承，而且和 `InkingWidget` 是**多继承**关系
（场景通过基类指针推 `Push*`），所以必须是虚的。

> **`Push*` 是 `noexcept`，而钩子不是**：钩子里的异常逃出去会直接
> `std::terminate`。重写时别让异常离开钩子。

### Protected Methods

>**onPointerHover(hover)**
原名:

```C++
virtual void onPointerHover(bool hover) {}
```

指针进出。默认什么都不做。

它只表示"**场景认为指针在你身上**"，**不表示"你被按下了"**。
拖拽时按着不放、指针滑出去，组件会**先收到一次 `hover = false`，而它仍然按着**——
这正是按钮"按下期间滑出去还是按下"这条手感的来源。

>**onPointerPress(button, press)**
原名:

```C++
virtual void onPointerPress(PointerButton button, bool press) {}
```

按下 / 抬起。**抬起也是这条**（`press = false`）。

**重写了就必须判 `button`**：按钮只在 `Primary` 时进 Pressed 态，否则右键按下会
把按钮点亮，用户会以为左键生效了。不关心右键的组件写一行守卫即可：

```C++
if (button != PointerButton::Primary) { return; }
```

>**onPointerClick(button, designX, designY)**
原名:

```C++
virtual void onPointerClick(PointerButton button, float designX,
                            float designY) {}
```

一次完整点击。默认什么都不做。

最典型的用法是右键菜单：

```C++
void MyWidget::onPointerClick(PointerButton button, float x, float y) {
    if (button == PointerButton::Secondary) {
        _menu->OpenAt(x, y);      // 坐标就是按下点，不必再换算
    }
}
```

>**onPointerWheel(deltaX, deltaY)**
原名:

```C++
virtual void onPointerWheel(float deltaX, float deltaY) {}
```

滚轮。默认什么都不做。

`deltaX` / `deltaY` 是**本帧累积**的滚动量（正 = 向右 / 远离用户），
**一帧多个滚轮事件已经合并成一次调用**。要"滚动内容"就在这里累加自己的偏移量；
要"缩放"就把 `deltaY` 当成幂次——具体含义由组件定。

与指针位置的区别：**位置是跨帧状态**（每帧都能问），**滚轮是一次性事件**
（这一帧没收到就是没滚），所以它走钩子而不是"每帧读状态"。

>**onPointerDrag(designX, designY, deltaX, deltaY)**
原名:

```C++
virtual void onPointerDrag(float designX, float designY,
                           float deltaX, float deltaY) {}
```

拖动。默认什么都不做。

只会在本节点"**按着**"期间来（按下时它抓住了这次交互），并且是**唯一**收得到拖动事件
的节点——指针移到别的组件上也不改归属；**移出矩形仍继续收到**，这正是拖拽该有的语义
（滑块 / 分割条 / 框选）。

`designX` / `designY` 是**当前指针位置**（设计坐标）；`deltaX` / `deltaY` 是相对
**上一次拖动事件**（第一次是相对按下点）的位移，**已经从窗口换算到设计坐标**——
组件直接加到自己身上即可，**不必再乘倍率**。

---

> 本文档对应版本：**0.6.6（0.x 发布版）**。
