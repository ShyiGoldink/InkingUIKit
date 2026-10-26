# 锚点系统

**回到API目录：**
[API index](../API.md)

本项目的锚点系统与常见的锚点配置的方式不太相同。
InkingAnchor是**定位层**：只回答"我在哪儿"。它**不是**组件基类，也不能直接实例化（构造函数是 `protected`）。
业务组件继承的是 `InkingWidget` 及其两个档位（`InkingStaticWidget` / `InkingDynamicWidget`）。
InkingAnchor完全不参与渲染，甚至不知道渲染层的存在——它这一层连 SDL 都不碰。
使用锚点时默认使用矩形。
此项目的布局约定：
窗口左上角为0，0.向右X增大，向下Y增大。
本项目使用严格的设计坐标体系，窗口的内容根据设计大小进行严格比例的放大/缩小。
倍率是**单一系数**（`_magnification`），letterbox 下 XY 同倍率、不做轴分离。
想用高清屏就增大设计尺寸，或让窗口层给出更大的倍率。
你可以使用#include <anchor/InkingAnchor.h> 来引入该头文件

## 用到的数据结构

>**AnchorPoint**

``` C++
struct AnchorPoint {
    float x = 0.0f;
    float y = 0.0f;
};
```

>**AnchorData**

AnchorData混杂了部分Widget的内容。这是因为InkingAnchor我们完全不推荐单独使用。
并且实际上也没有留任何开口。
所以将AnchorData设计成现在这个形状，并且让InkingAnchor的构造函数依赖此数据结构来起到一定的约束效果。

```C++
struct AnchorData {
    AnchorPoint selfAnchor{};  ///< 自身锚点
    AnchorPoint traceAnchor{}; ///< 上级锚点（追踪的父级锚点）
    float offsetX = 0.0f;      ///< 对齐之后再挪一点，设计坐标
    float offsetY = 0.0f;
    int width = 0;   ///< 自身尺寸，设计坐标
    int height = 0;

    int zIndex = 0;   ///< 越大越靠上，**只在兄弟之间**比较（局部层级）
    bool visible = true;  ///< 可见性；不可见连命中都不必问
    std::uint32_t color = 0xFF569CD6u;  ///< 占位填充色 0xAARRGGBB（样式层接手）
    LayoutSpec layout{};
    LayoutItem layoutItem{};
};
```

## 属性

### Protected

>**AnchorPoint _selfAnchor**  

 自身锚点

>**AnchorPoint _traceAnchor**

 上级锚点（追踪的父级锚点）

>**float _offsetX**

锚点对齐之后的横向微调，设计坐标。默认为0.0f。

>**float _offsetY**

锚点对齐之后的纵向微调，设计坐标。默认为0.0f。

>**int _width**

自身尺寸，设计坐标。它同时是位置推导的输入，也是 `GetWidth()` / `GetHeight()` 的默认返回值。

>**int _height**

自身尺寸，设计坐标。它同时是位置推导的输入，也是 `GetWidth()` / `GetHeight()` 的默认返回值。

>**bool _visible**

自己这一层的可见性。父级不可见时，本项仍可能为 `true`，因此要用 `IsVisibleInTree()` 来判断整棵树是否可见。

>**float _magnification**

展示倍率，表示设计单位到设备像素的换算比例。

>**InkingAnchor* _parent**

父级的定位链（非拥有关系）。根节点（窗口 / 场景）时为 `nullptr`。
它只负责让 `GetX/Y` / `GetAbsX/Y` / `IsVisibleInTree` 能沿链向上查询，不代表真实树结构。

## APIS

### Public

>**GetSelfAnchor()**
原名:

```C++
const AnchorPoint& GetSelfAnchor() const noexcept;
```

读回配置里的**自身锚点**（只读）。**位置的推导不经过它**——`GetX/Y` 直接用公式算，所以正常业务不需要读；调试或写自定义布局时才用。

 >**GetTraceAnchor()**
 原名:

 ```C++
 const AnchorPoint& GetTraceAnchor() const noexcept;
 ```

 读回配置里的**上级锚点**（只读）。同上：推导不经过它，正常业务不需要读。

 >**GetOffsetX()**
 原名:

 ```C++
 float GetOffsetX() const noexcept;
 ```

获取_offsetX。_offsetX是锚点的横向偏移量。锚点是父子结构对齐的基准，基于此基准横向移动的数值就是_offsetX。

 >**GetOffsetY()**
 原名:

 ```C++
 float GetOffsetY() const noexcept;
 ```

获取_offsetY。_offsetY是锚点的**纵向**偏移量。锚点是父子结构对齐的基准，基于此基准纵向移动的数值就是_offsetY。

>**GetX()**
原名:

```C++
float GetX() const noexcept;
```

获取**相对**于父结构的设计坐标x。每次调用都会基于两套锚点进行计算。

>**GetY()**
原名:

```C++
float GetY() const noexcept;
```

获取**相对**于父结构的设计坐标y。每次调用都会基于两套锚点进行计算。

>**GetWidth()**
原名:

```C++
virtual int GetWidth() const noexcept;
```

获取设计宽度。使用virtual是为了保证拥有每态尺寸的组件可以重写来返回当前尺寸。当在具体的组件重写时，语义与在Anchor中的语义有所区别。在具体组件中重写的语义将会升高为当前的宽度。

>**GetHeight()**
原名:

```C++
virtual int GetHeight() const noexcept;
```

获取设计高度。使用virtual是为了保证拥有每态尺寸的组件可以重写来返回当前尺寸。当在具体的组件重写时，语义与在Anchor中的语义有所区别。在具体组件中重写的语义将会升高为当前的高度。

>**IsVisible()**
原名:

```C++
bool IsVisible() const noexcept;
```

获取**自己这一层**的可见性（不含祖先）。父级不可见时子类自然也画不出来，但子类的 `IsVisible()` 仍然返回 `true`——所以它**不能用来判断"实际看不看得见"**。要那个结果用 `IsVisibleInTree()`（见下条）。

>**IsVisibleInTree()**
原名:

```C++
bool IsVisibleInTree() const noexcept;
```

获取当前Anchor的整棵树是否可见。如果返回false，则证明Anchor不可见；否则可见。

>**GetMagnification()**
原名:

```C++
float GetMagnification()const noexcept;
```

获取**展示倍率**：一个乘法系数，`设计坐标 × 该系数 = 设备像素`。它由窗口层写入（业务代码只读）。

### Protected Methods

>**InkingAnchor(InkingAnchor* parent, const AnchorData& data) ***

原名:

```C++
InkingAnchor(InkingAnchor* parent, const AnchorData& data) noexcept;
```

构造函数。`parent` 传 `nullptr` 代表它是一个根（窗口 / 场景）。
它是 `protected` —— **本类不可直接实例化**，每个组件声明时必须先选一种档位（静态 / 动态）。

>**~InkingAnchor()**

```C++
virtual ~InkingAnchor();
```

虚析构。本类要被继承，链上还有 `InkingWidget` 与两个档位，必须有虚析构。

>**拷贝 / 移动：四个全部 delete**

```C++
InkingAnchor(const InkingAnchor&) = delete;
InkingAnchor& operator=(const InkingAnchor&) = delete;
InkingAnchor(InkingAnchor&&) = delete;
InkingAnchor& operator=(InkingAnchor&&) = delete;
```

禁止拷贝与移动。原因是本类持有父链（`_parent`）并会被登记进场景：复制出的**第二份关系**会让父子链和注册表同时指向同一个对象。

>**GetAnchorParent()**

原名:

```C++
InkingAnchor* GetAnchorParent() const noexcept;
```

获取自己的父级（**定位链**，非拥有）。`protected` —— 外部不需要沿定位链走；
业务侧要的父子关系是 `InkingWidget::GetParent()`（返回 `InkingWidget*`，那一份带树语义）。
根节点（窗口 / 场景）返回 `nullptr`。

---

> 本文档对应版本：**0.5.3（0.x 发布版）**。
