# 布局数据层

**回到API目录：**
[API index](../API.md)

这是布局的**数据层**：规则怎么写、意图怎么表达。**只有意图，没有结果**——
本文件里一个计算函数都没有（唯一的方法 `LayoutSpec::Valid()` 只是问"这份规则要不要生效"），
它是布局的"词汇表"。全部内容在 `namespace inking` 下，header-only，没有对应的 `.cpp`。

三句话先说清这一层在整个布局里的位置：

1. **规则住在父容器上**（`LayoutSpec`）：孩子只在自己身上写"我在这个布局里的**意愿**"
   （`LayoutItem`：margin / 是否吃剩余空间 / min-max）。就像 CSS 里 `display:flex` 写在父级、
   `flex-grow` 写在子级。
2. **这里只有意图，没有结果**：算出来的矩形由布局求解器产出，产出写进组件的**运行期几何**
   （`_width` / `_height` / `_offsetX` / `_offsetY`），然后标结构脏让命中表重烘。
   为什么不把结果也放回数据里：`AnchorData` 是配置、是要被**反复读取的输入**，
   一旦被结果覆盖，重排就再也找不到原始意图了。
3. **位置推导一个字都不改**：布局只是把锚点公式里的两个量（尺寸、偏移）算出来。
   于是绘制 / 命中 / AABB / 子树变换全都不知道"有布局这回事"。

单位全是**设计坐标**（和 `AnchorData::width` / `offsetX` 同一套）。

> **求解器不在本文件里**：真正算矩形的是 `detail::layout::ArrangeSubtree`
> （声明在 `include/widget/InkingWidget.h`、实现在 `src/widget/InkingWidget.cpp`）。
> 依赖方向是"**Widget 层 → 本数据层**"，所以本文件**故意不知道**组件、树、命中的存在——
> 它单独用不起来是**设计目标，不是缺口**。

你可以使用 `#include <dataStruct/InkingLayout.h>` 来引入该头文件。

## 挂在哪

`AnchorData`（`InkingAnchorData.h`）上挂了两个字段管两件事，**别混**：

| 字段 | 站在谁的角度 | 含义 |
| --- | --- | --- |
| `layout` | 我是**容器**时 | 我怎么排我的孩子（默认 `kind = None` = 不布局，孩子的锚点自己定位——老配置一行不用改） |
| `layoutItem` | 我是某个容器的**孩子**时 | 我在那个布局里的意愿（margin / 要不要吃剩余空间 / min-max） |

这两个字段是**配置、只读**：布局算出来的尺寸与位置写进组件的运行期几何，
**不会**回写这里。对外的读法只有 `InkingWidget::GetLayoutSpec()` /
`GetLayoutItem()`（都返回 `const` 引用），头文件里没有对应的写入口——
布局的输入是**构造期**定型的。

## 求解器怎么用它（数据流）

```
        ┌──────────────────── 读 ────────────────────┐
容器 LayoutSpec ──┐                                  │
（gap / justify / ├──► detail::layout::ArrangeSubtree(InkingWidget& container)
 align / padding）│            │
孩子 MeasureLayout ┘            │ 解出每个孩子的 LayoutRect
（我在这份约束下想要多大）      ▼
                    InkingWidget::ApplyLayoutRect(rect)
                    （protected，只有 ArrangeSubtree 是 friend）
                               │ 反解成锚点公式里的量
                               ▼
                    _width / _height / _offsetX / _offsetY
                    （锚点归零、尺寸取整 → 位置推导那条路一个字没改）
```

三个要点：

- **测量是纯函数**：`MeasureLayout(constraints)` 是 `[√]` 钩子，契约是"不许改自己的几何、
  不许标脏、不许有副作用"——布局可能要问好几遍。默认实现返回"我当前的尺寸"；
  内容驱动的组件重写它才有意义（label 去问文字层要高度、图片去要原始尺寸）。
- **写入口开得极窄**：`ApplyLayoutRect` 是 `protected`，且**只有
  `detail::layout::ArrangeSubtree` 是它的 friend**——业务代码想调它是**编译错误**，
  不是靠文档约定。静态档本来没有几何写入口（`Resize` / `ChangeOffset` 只挂在动态档上，
  那是"静态几何定死"这条铁律的实现方式），这条口子是为"排一列**按钮**"开的。
- **重排是显式的**：`Relayout()`（从我这棵子树开始排）/ `RelayoutParent()`
  （往父链找第一个声明了布局的祖先）都由**调用方**在"排布输入变了"之后喊一声。
  框架不做隐式重排，代价是调用方要记得喊——所以两条都是**幂等**的：
  算出来跟当前几何一模一样时一个脏都不标，多喊几次不白干。

## 用到的数据结构

>**LayoutKind**

布局种类。`None` = 不布局（老行为：锚点自己定位）。

```C++
enum class LayoutKind : std::uint8_t {
    None = 0,
    Stack,  // 一维排布：竖着或横着，一个接一个
    Grid,   // 网格：定列数，逐行填
};
```

>**LayoutAxis**

`Stack` 的主轴方向。

```C++
enum class LayoutAxis : std::uint8_t {
    Vertical = 0,  // 竖排（一行一个）
    Horizontal,    // 横排（一列一个）
};
```

>**LayoutJustify**

主轴上"多余的空间怎么分"。

```C++
enum class LayoutJustify : std::uint8_t {
    Start = 0,     // 挤在一头（默认）
    Center,        // 居中
    End,           // 挤在另一头
    SpaceBetween,  // 两端顶住，间隙均分
    SpaceAround,   // 每个项两侧各留半份
    SpaceEvenly,   // 项与两端都是等份
};
```

>**LayoutAlign**

交叉轴上"单个项怎么放"。容器写在 `LayoutSpec::align` 上，
孩子可以用 `LayoutItem::selfAlign` 覆盖。

```C++
enum class LayoutAlign : std::uint8_t {
    Start = 0,
    Center,
    End,
    Stretch,  // 拉满交叉轴（默认；一列按钮左右撑满是常态）
};
```

>**LayoutSpacing**

四个方向的间距（**padding 与 margin 共用**）。设计坐标。

```C++
struct LayoutSpacing {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};
```

>**LayoutSize**

一个尺寸（求解器内部的表达；`AnchorData` 那边是 `int`）。

```C++
struct LayoutSize {
    float width = 0.0f;
    float height = 0.0f;
};
```

>**LayoutConstraints**

可用空间（**父级给孩子的约束**）。`0` 表示那个方向**不限**。

```C++
struct LayoutConstraints {
    float maxWidth = 0.0f;
    float maxHeight = 0.0f;
};
```

>**LayoutRect**

一个**算好的**矩形（相对父级左上角）。它是 `ApplyLayoutRect` 的入参，
也是"结果"这个概念的载体——它**不回到** `AnchorData` 里。

```C++
struct LayoutRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};
```

>**LayoutItem**

一个孩子在父容器布局里的**意愿**（挂在孩子自己的数据上）。
全部有默认值，所以"什么都不写"= 老样子：按自己的尺寸、不吃剩余空间、
没有 min-max、margin 为 0。

```C++
struct LayoutItem {
    LayoutSpacing margin{};       // 外边距：算在这个孩子的矩形之外
    bool fillMain = false;        // 主轴方向上吃掉剩余空间
    float minWidth = 0.0f;        // 尺寸下限（0 = 不限）
    float minHeight = 0.0f;
    float maxWidth = 0.0f;        // 尺寸上限（0 = 不限）
    float maxHeight = 0.0f;
    bool hasSelfAlign = false;    // 是否用 selfAlign 覆盖容器的 align
    LayoutAlign selfAlign = LayoutAlign::Start;
};
```

>**LayoutSpec**

一个容器的布局**规则**（挂在父容器的 `AnchorData::layout` 上）。
`kind == None` 时整份规则都不生效——这是"老配置一行不改"的保证。

```C++
struct LayoutSpec {
    LayoutKind kind = LayoutKind::None;

    // ---- Stack ----
    LayoutAxis axis = LayoutAxis::Vertical;
    float gap = 0.0f;                    // 主轴间距（相邻两项之间）
    LayoutJustify justify = LayoutJustify::Start;
    LayoutAlign align = LayoutAlign::Stretch;  // 交叉轴默认对齐

    // ---- Grid ----
    int columns = 1;                     // 列数（至少 1）
    float rowGap = 0.0f;
    float columnGap = 0.0f;

    // ---- 共用 ----
    LayoutSpacing padding{};             // 内容区内缩

    bool Valid() const noexcept;
};
```

## 属性

### Public

#### LayoutItem

>**LayoutSpacing margin**

**外边距**：算在这个孩子的矩形**之外**（间隙之外再加一圈）。
与容器的 `padding`（内容区内缩）是两回事，两者互不替代。

>**bool fillMain**

**主轴方向上吃掉剩余空间**。一列里多个 `fillMain` 就**均分**剩余空间；
剩余空间为负（内容超出）时**谁也不吃**，超出的部分交给容器自己的裁剪 / 滚动策略——
这与"`min` 夹住"是同一条规则的两种结果。

>**float minWidth / float minHeight**

尺寸**下限**（`0` = 不限）。

>**float maxWidth / float maxHeight**

尺寸**上限**（`0` = 不限）。

>**bool hasSelfAlign**

是否用下面的 `selfAlign` **覆盖容器的 `align`**（默认不覆盖，即听容器的）。

>**LayoutAlign selfAlign**

被 `hasSelfAlign` 启用时，这个孩子在**交叉轴**上的对齐方式。
默认 `Start`——注意它和 `LayoutSpec::align` 的默认值（`Stretch`）**不一样**：
覆盖是"显式动作"，所以默认值取的是最朴素的那一个。

#### LayoutSpec

>**LayoutKind kind**

布局种类。默认 `LayoutKind::None`，也就是"不布局"（老行为：孩子的锚点自己定位）。
它同时是 `Valid()` 的判据。

>**LayoutAxis axis**

`Stack` 的**主轴**方向（竖排 / 横排）。默认竖排。

>**float gap**

`Stack` 的**主轴间距**（相邻两项之间）。**不算 `padding`，也不算 `margin`**。

>**LayoutJustify justify**

主轴上"多余的空间怎么分"。默认 `Start`（挤在一头）。

>**LayoutAlign align**

**交叉轴的默认对齐**（孩子可以用 `LayoutItem::selfAlign` 覆盖）。
默认 `Stretch`——一列按钮左右撑满是常态。

>**int columns**

`Grid` 的**列数**（至少 1）。

>**float rowGap**

`Grid` 的**行**间距。

>**float columnGap**

`Grid` 的**列**间距。

>**LayoutSpacing padding**

**内容区内缩**：所有孩子都在这个范围里排。与孩子的 `margin` 叠加。

## APIS

### Public

>**Valid()**
原名:

```C++
bool Valid() const noexcept;
```

这份规则**要不要生效**。判据就一条：`kind != LayoutKind::None`。
`None` 时整份规则（包括 `padding` / `gap`）都不参与排布，
这是"老配置一行不改"的实现方式——不用给老的 `AnchorData` 补任何字段。

---

> 本文档对应版本：**0.6.6（0.x 发布版）**。
