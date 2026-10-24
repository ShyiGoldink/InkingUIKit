# 定点浮窗（`InkingFixedPopup`）

**回到API目录：**
[API index](../API.md)

`panel/InkingFixedPopup.h` —— 一个**固定几何、自己定位**的动态面板：给一个定点就在那里弹出，
靠边自动翻、太大就贴边，点击吃掉、点掉外部就关。

## 它是什么，不是什么

| 是 | 不是 |
| --- | --- |
| 动态档容器（`InkingDynamicWidget` 派生、不进命中表） | 静态面板（几何构造即定型） |
| 一个"吃掉点击"的浮层（`OwnsSubtreeHit() = true`） | 一个"透明热区"（不提供穿透开关） |
| 只管"框在哪、吃不吃、什么时候关" | 不管内容长什么样（`AddItem(任意 widget)`） |

## 公开面（四组）

### 定点

| 方法 | 说明 |
| --- | --- |
| `void OpenAt(float x, float y)` | 在定点打开（**父级坐标**，通常传点击位置或按钮边缘）。已开着时只挪位置，不重发 `opened` |
| `void SetTarget(float x, float y)` | 只改定点；开着就重新落位 |
| `void Close()` | 关闭（幂等）。焦点在本子树则**当场释放** |
| `bool IsOpen() const` | 现在开着吗 |
| `float GetTargetX/Y() const` | 上次的定点 |

### 内容

| 方法 | 说明 |
| --- | --- |
| `InkingWidget* AddItem(std::unique_ptr<InkingWidget>)` | 挂一个由本浮窗拥有的子项；自动抬 z、重排一次 |

### 行为开关

| 方法 | 默认 | 说明 |
| --- | --- | --- |
| `SetCloseOnOutsideClick(bool)` | `true` | 点在浮窗外部关闭 |
| `SetTakeFocusOnOpen(bool)` | `true` | 打开时夺取键盘焦点（关掉就不再是焦点模态） |
| `SetClampBounds(const ShapeBounds&)` | 父级矩形 | 收窄落位范围（父级坐标） |
| `ClearClampBounds()` | — | 恢复默认 |

### 观测

`GetResolvedSideX/Y()`（`+1` 正向 / `−1` 翻转 / `0` 夹紧）、`IsClampedX/Y()`、`GetShape()`；
事件 `opened` / `closed`。

## 落位怎么算

`detail::SolvePopupPlacement`（`detail/InkingPopupPlacement.h`，纯函数）：

1. 按自锚点 `(0,0)` 摆 ⇒ 向右下展开；
2. 该方向放不下 ⇒ **翻到对侧**（自锚点变 `(1,1)`）；
3. 还放不下 ⇒ 夹到范围内；**尺寸大于范围时贴住左上、允许另一侧溢出**。

两轴**独立**：贴着右边缘的小菜单仍向下展开，不会因为 x 翻了一下就把 y 也翻过去。
`OpenAt` 里顺序是 `Relayout()` → 算落位 → 写锚点 / 偏移；反了会拿旧尺寸算钳制（不报错，位置偏）。

## 焦点模态

`OpenAt` 默认夺取键盘焦点，于是**打开期间同场景的输入框拿不到焦点**。这是菜单类浮层的语义，
但如果你要的是非模态侧边栏，请 `SetTakeFocusOnOpen(false)`。

焦点会在**指针变化**时取回（`onPointerStateChanged`），不在 `onTick` 里每帧抢——鼠标停着就不反复夺。

## 点击语义

- 点在浮窗内（含空白区）⇒ 命中它是 `Block`，**不透到下面**；
- 点在浮窗内、但落在子控件上 ⇒ 子控件收点击（`QueryHit` 对动态容器深挖一层）；
- 点在外部 ⇒ `onOutsidePressed()`（默认 `closeOnOutside` 时关闭）；
- **跨静态 / 动态的"点透传到下层"未实现**：`SetHitPassThrough(true)` 对它没有意义
  （动态的 `PassThrough` 目前不会让给下层的静态 `Block`）。

## 继承扩展点

| 钩子 | 什么时候调 |
| --- | --- |
| `onPopupOpened()` | 已经落位、已经可见、`opened` 已发 |
| `onPopupClosed()` | 已经不可见、`closed` 已发 |
| `onOutsidePressed()` | 外部按下。默认按 `closeOnOutside` 关闭 |

## 与其他件的关系

- `MenuPopup`（`prefab/MenuPopup.h`）**继承**它，加竖排布局、项表与局部快捷键；
- `InkingStaticPanel` 是它的**静态档对照**（几何构造即定型、进命中表）——
  两者外观同一套（`PanelPaint`），差别只在档位。

## 三条容易踩的

1. **`OpenAt` 之前定好尺寸**（`Resize` 或让布局算完）。尺寸还是 0 时落位退化成"放在定点上"。
2. **父级决定"窗口范围"**：挂 `scene` 时父级就是设计画布；挂进某个面板就以那个面板为界。
3. **它不进命中表**：每次 `ChangeOffset` 都标命中脏，所以别拿它做"每帧跟随鼠标"——那是
   另一种组件（或者用显示变换）。定点浮窗是"打开时定位一次"的语义。
