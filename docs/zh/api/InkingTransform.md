# 变换通道

**回到API目录：**
[API index](../API.md)

本文件描述**仿射变换（平移 / 旋转 / 缩放）**，也就是本项目的"变换通道"。
全部内容在 `namespace inking` 下，是 **header-only 的纯数据 + 纯函数**，没有对应的 `.cpp`。

**变换通道和布局通道不是一回事**，把两者分清楚是这份文件的前提：

| 通道 | 内容 | 代价 |
| --- | --- | --- |
| **变换通道**（本文件） | `translateX/Y` / `rotate` / `scale` | **不改命中表**：表按"本地几何"烘，查询时把点**反变换**回本地空间。于是静态组件也能有它，命中表照样有效，只是每帧多一次反变换（几个乘加） |
| **布局通道** | `width` / `height` / 会引发兄弟重排的定位 | 必须**重烘受影响的子树**（每态一份表或动态档的事）——**不在本文件的范围里** |

变换通道**不碰本地几何**（形状、尺寸、锚点一个字节都不动）。正因为变换不改表，
"静态档进命中表"的前提没被破坏。

## 两个先定死的约定

1. **原点取组件中心**（等价 CSS 的 `transform-origin: 50% 50%`）。
   形状都是按左上角定义的，绕左上角转起来不是直觉里的样子：一个 200×100 的按钮转 90°
   会整个跑到左边去。
2. **`rotate` 的单位是度，顺时针为正**（屏幕坐标 y 向下，所以数学上的正角看起来就是顺时针）。
   理由与 CSS 一致：配置里写 `15` 比写 `0.2618` 直观。

变换的**正向用于绘制**（本地 → 屏幕），**反向用于命中**（屏幕 → 本地）。
两者必须**严格互逆**，否则就会回到"画的是一个、点的是另一个"那条老路——
所以它们挨着写在这里，而不是各写各的。

你可以使用 `#include <dataStruct/InkingTransform.h>` 来引入该头文件。

> 复合变换（相似仿射 `Affine`）在 `dataStruct/InkingAffine.h`：那个文件**依赖本文件**，
> 用 `AffineFromTransform` 把本文件的一次变换写成仿射，再做多层的复合与求逆。

## 用到的数据结构

>**Point2**

设计坐标系里的一个点。变换的进出参都用它。

```C++
struct Point2 {
    float x = 0.0f;
    float y = 0.0f;
};
```

>**TransformSpec**

一个状态的**目标变换**。缺省（全零 + `scale` 1）是单位变换。

```C++
struct TransformSpec {
    float translateX = 0.0f;  // 设计坐标
    float translateY = 0.0f;  // 设计坐标
    float rotate = 0.0f;      // 度，顺时针为正
    float scale = 1.0f;       // 均匀缩放；负值等于镜像（允许，但很少用）

    constexpr bool IsIdentity() const noexcept;
};
```

`scale` 是**单一系数**：本变换不做轴分离的 `scaleX` / `scaleY`。

## 属性

### Public

>**float translateX**

横向平移，**设计坐标**。

>**float translateY**

纵向平移，**设计坐标**。

>**float rotate**

旋转角度，单位是**度**、**顺时针为正**（屏幕坐标 y 向下）。
它和 `ShapeSpec::rotation` / `ShapeSpec::startAngle` 是同一套角度约定。

>**float scale**

**均匀**缩放系数。默认 `1.0`。**负值等于镜像**（允许，但很少用）。
`0` 是配置错误（形状被压成一个点、逆变换不存在），生成期就会拦；
真漏到运行期时按 `1` 处理，见 `InverseTransform()`。

## APIS

### Public

>**IsIdentity()**
原名:

```C++
constexpr bool IsIdentity() const noexcept;
```

是不是**单位变换**（`translateX/Y` 与 `rotate` 为 0、`scale` 为 1）。
注意判据是**精确相等**（不是"接近"）：它要回答的是"配置里这次到底有没有写"，
而不是"看起来像不像没动"。

>**SameTransform(a, b)**
原名:

```C++
constexpr bool SameTransform(const TransformSpec& a,
                             const TransformSpec& b) noexcept;
```

两个变换**按位相等**。用来判断"这次切换其实什么都没变"——那就**连过渡都不用起**。
与 `IsIdentity()` 同一套精确比较的口径。

>**ApplyTransform(transform, x, y, cx, cy)**
原名:

```C++
inline Point2 ApplyTransform(const TransformSpec& transform, float x, float y,
                             float cx, float cy);
```

**正向：本地坐标 → 变换后的坐标**（绕 `(cx, cy)`）。**绘制用。**

顺序是 CSS 的 `translate(...) rotate(...) scale(...)`：**先绕中心缩放、再旋转、最后平移**：

```C++
radians = rotate × (π/180)
dx = (x − cx) × scale
dy = (y − cy) × scale
x' = cx + dx·cos(radians) − dy·sin(radians) + translateX
y' = cy + dx·sin(radians) + dy·cos(radians) + translateY
```

`(cx, cy)` 是**调用方传进来的中心**（组件的中心），本文件不认识组件，也不替你算它。

>**InverseTransform(transform, x, y, cx, cy)**
原名:

```C++
inline Point2 InverseTransform(const TransformSpec& transform, float x, float y,
                               float cx, float cy);
```

**反向：变换后的坐标 → 本地坐标**。**命中用。**

它必须与 `ApplyTransform` **严格互逆**：顺序反过来、旋转走**转置**（同一个
`cosA` / `sinA`，符号换过来）、缩放取倒数。做法是"先把平移和中心一起减掉 →
反向旋转 → 反向缩放"。

`scale == 0` 时**没有逆**（形状被压成一个点），这里退回按 `1` 处理——那本来是配置错误
（生成期就会拦），但**宁可不命中，也不要除零得到 `inf` 之后到处乱命中**。

>**LerpTransform(from, to, t)**
原名:

```C++
inline TransformSpec LerpTransform(const TransformSpec& from,
                                   const TransformSpec& to, float t);
```

两个变换之间**线性插值**，四项（`translateX` / `translateY` / `rotate` / `scale`）各插一遍。

**角度也是直接线性插的**：一方面是相对方便差值，一方面是为了尊重用户意图。
代价是不做"最短路径"规整——`rotate` 从 350 过渡到 10 会**倒着绕 340°**，
而不是正着走 20°。要让某次过渡走近路，就把目标角度写成 `370`。

## detail 命名空间（内部小工具）

>**kTransformPi / kTransformDegToRad**

`constexpr` 常量：π 与"度 → 弧度"。

名字带 `transform` 前缀是**必须的**：`inking::detail::kPi` 这种通用名字框架里已经有别的实现
在用（例如 `InkingDraw.cpp` 的形状填充），而 `detail` 里的**重名不会报"重复定义"**，
只会在使用处报"引用不明确"，更难查。加工具函数时照这个习惯带前缀。

---

> 本文档对应版本：**0.6.6（0.x 发布版）**。
