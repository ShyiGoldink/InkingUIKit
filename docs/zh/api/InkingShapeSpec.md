# 形状层

**回到API目录：**
[API index](../API.md)

形状层是本项目"一个组件的轮廓长什么样"的唯一定义处，用 **SDF（带符号距离场）** 描述。
全部内容都在 `namespace inking` 下，是 **header-only 的纯数据 + 纯函数**，没有对应的 `.cpp`。

符号用通用约定，即：
`f < 0` 在内部、`f = 0` 在边界、`f > 0` 在外部；裁剪 = 保留 `f <= 0` 的区域。
这样公开的 SDF 参考实现可以照着抄，不用逐个翻符号；抗锯齿也是标准写法
`smoothstep(-aa, aa, d)`；`abs(d)` 天然就是"到边界的距离"。

**一条硬约定：形状同时决定渲染 / 命中 / AABB，三者从这一份定义派生。**
各写一遍就会出"画的是圆角、点到的是直角"——两边单独看都对，合起来才错，
而且极难排查。所以本文件只提供三个派生量，且都从这里算：

| 派生量 | 用途 |
| --- | --- |
| `SignedDistance` | 渲染（填充 / 描边 / 裁剪，形状层接手后） |
| `ShapeContains` | 命中（这个点在不在形状里，现在就能用） |
| `GetShapeBounds` | AABB（将来塞进命中表的哪个格子） |

`ShapeSpec` 是形状的**说明书**：把全部形状整合到一个结构里，靠 `kind` 分辨当前是哪一种，
只属于它的那几个参数才生效。它需要显式查 `type` 做参数校验，所以**强调用 key 来写**：
`{"type":"roundedRect","radius":10}`，生成器在编译期把它展开成本结构。

你可以使用 `#include <dataStruct/InkingShapeSpec.h>` 来引入该头文件。

## 坐标系与形状语义

一律以**组件左上角**为原点、单位是**设计坐标**（不是设备像素）：

| 形状 | 尺寸语义 |
| --- | --- |
| `rect` | 用满 `width × height` |
| `roundedRect` | 用满 `width × height`，四角半径 `radius`（夹到 `min(w,h)/2`） |
| `circle` | 正圆，直径取 `min(w,h)`，**居中** |
| `ellipse` | 椭圆，用满 `width × height` |
| `capsule` | 用满 `width × height`，两端是半径 `min(w,h)/2` 的半圆（沿长轴） |
| `ring` | 圆环，外径取 `min(w,h)`、**居中**，环宽 `thickness`（往里长） |
| `line` | 水平线段，纵向居中；线宽 `thickness`，端点形状 `cap` |
| `polygon` | 正多边形，外接圆直径取 `min(w,h)`、**居中**；`sides` 条边、`rotation` 是起始角（度） |
| `chamferRect` | 用满 `width × height`，四个角各切掉 `chamfer` 长的一条 45° 斜边 |
| `star` | 外接圆直径取 `min(w,h)`、**居中**；`points` 个角，`innerRatio` 是内 / 外半径之比 |
| `superellipse` | 用满 `width × height`，指数 `exponent`（2 = 椭圆，越大越方） |
| `arc` | 外径取 `min(w,h)`、**居中**，环宽 `thickness`（往里长）；从 `startAngle` 起顺时针扫 `sweepAngle` 度，两端**径向切平** |
| `polygonPath` | 顶点表给出来的任意多边形（相对左上角；可以非凸，甚至可以不是星形域） |

**注意 `circle` 是居中的**：一个 100×60 的组件画圆，得到的是居中、直径 60 的圆，
而不是一个 100×60 的"椭圆"。要椭圆就写 `ellipse`。

## 用到的数据结构

>**ShapeKind**

形状的种类。放一个显式的整数而不是直接塞 `variant`：生成器是按 `type` 查表展开的，
运行期只需要"这一个是哪一种"。

```C++
enum class ShapeKind : std::uint8_t {
    Rect = 0,        // 矩形
    RoundedRect,     // 圆角矩形
    Circle,          // 圆
    Ellipse,         // 椭圆
    Capsule,         // 胶囊体
    Ring,            // 圆环
    Line,            // 线
    Polygon,         // 多边形
    ChamferRect,     // 切角矩形：四条边照旧，四个角用 45° 直线切掉
    Star,            // 星形：外/内半径交替的正星形（非凸）
    Superellipse,    // 超椭圆：n 控制"从椭圆到方"的过渡（n < 1 是凹的星形角）
    Arc,             // 圆弧：圆环的一段（带环宽），两端径向切平
    PolygonPath,     // 顶点表给出来的任意多边形（可以非凸，甚至可以不是星形域）
};
```

>**LineCap**

线段的端点形状（**只对 `line` 有意义**）。名字与 CXXCSS 里的字符串一一对应
（`butt` / `round` / `square`），语义照 CSS 的 `stroke-linecap`：平头、圆头、方头。

```C++
enum class LineCap : std::uint8_t {
    Butt = 0,  // 平头：到端点就断（默认）
    Round,     // 圆头：两端各一个半径 thickness/2 的半圆
    Square,    // 方头：两端各外扩 thickness/2
};
```

>**ShapeSpec**

一个形状的定义：种类 + 只属于它的那几个参数。**用工厂函数造，别手动填字段**——
夹取和校验都在工厂里做了。

```C++
struct ShapeSpec {
    ShapeKind kind = ShapeKind::Rect;      // 形状种类

    float radius = 0.0f;                   // 只对 RoundedRect，已夹到 [0, min(w,h)/2]
    float thickness = 0.0f;                // Ring 的环宽 / Line 的线宽（> 0）
    LineCap cap = LineCap::Butt;           // 只对 Line
    int sides = 0;                         // 只对 Polygon，边数（>= 3）
    float rotation = 0.0f;                 // 只对 Polygon（Star 也用），起始角（度）
    float chamfer = 0.0f;                  // 只对 ChamferRect，切角长度（> 0）
    int points = 0;                        // 只对 Star，角数（>= 3）
    float innerRatio = 0.5f;               // 只对 Star，内 / 外半径比（0~1）
    float exponent = 2.0f;                 // 只对 Superellipse，指数 n
    float startAngle = 0.0f;               // 只对 Arc，起始角（度）
    float sweepAngle = 360.0f;             // 只对 Arc，扫过角度（度）

    const float* pathPoints = nullptr;     // 只对 PolygonPath，顶点表（借用，不拥有）
    int pathPointCount = 0;                // 只对 PolygonPath，顶点个数（>= 3）
};
```

**工厂函数的校验策略**：既违反"形状自己的数学要求"又拦不住的值（比如半径超上限）走
**夹取**；纯配置错误（比如负半径、`sides < 3`）走 `assert` **当场拦下**，
release 下再兜底修正成正数。所以 debug 里写错配置会直接断在工厂函数上。

**生命周期的坑**：`pathPoints` **不拥有**数据，只借一个视图——和 `ButtonAppearance::imageBytes`
同一个约定：生成器烘的是**文件作用域的静态数组**，生命周期是整个进程，比任何组件都长。
手写 `ShapeSpec` 时要自己保证这块内存活得够久（拷了 `ShapeSpec` 的组件会一直拿着这个指针）。

>**ShapeBounds**

形状的包围盒（AABB），相对组件左上角。

```C++
struct ShapeBounds {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};
```

## 属性

### Public

>**ShapeKind kind**

形状的种类。默认 `ShapeKind::Rect`，也就是"不写 shape"时的直角矩形。

>**float radius**

圆角半径，**只对 `RoundedRect` 有意义**；一律已夹到 `[0, min(w,h)/2]`。
它记的是**配置里写的值**，真正生效的值要用 `EffectiveRadius()` 现算（见下）。

>**float thickness**

`Ring` 的环宽 / `Line` 的线宽（设计坐标，> 0）。真正生效的环宽还会顶到外半径就停
（`detail::ShapeRingThickness`），线宽不夹。

>**LineCap cap**

线段端点形状，**只对 `Line` 有意义**。默认 `LineCap::Butt`。

>**int sides**

**只对 `Polygon` 有意义**：边数（>= 3）。

>**float rotation**

**只对 `Polygon` 有意义**（`Star` 也用它，含义是"第一个外角的方向"）：起始角，单位是**度**，
跟 `TransformSpec::rotate` 统一（配置里写 15 比写 0.2618 直观）；屏幕坐标 y 向下，
所以**顺时针为正**。`0` 表示第一个顶点落在 +x（正右方），其余顶点按 `360/sides` 顺时针排。

>**float chamfer**

**只对 `ChamferRect` 有意义**：切角沿边的长度（> 0）。真正生效的值上限是短边的一半
（`detail::ShapeChamferAmount`，再长就没有直边了）。

>**int points**

**只对 `Star` 有意义**：角数（>= 3）。

>**float innerRatio**

**只对 `Star` 有意义**：内半径 / 外半径（0~1，越小越尖）。默认 `0.5`。

>**float exponent**

**只对 `Superellipse` 有意义**：指数 n（n = 2 是椭圆，越大越接近矩形）。默认 `2.0`。

>**float startAngle**

**只对 `Arc` 有意义**：**起始角**（度，0 = 正右方，顺时针为正，同 `rotation`）。

>**float sweepAngle**

**只对 `Arc` 有意义**：**扫过的角度**（度，正 = 顺时针，> 0；夹到 ≤ 360）。默认 `360`。

>**const float* pathPoints**

**只对 `PolygonPath` 有意义**：**顶点表**（`x0,y0,x1,y1,…`，设计坐标，相对组件左上角）。
**借用**，不拥有——生命周期要覆盖用它的组件。

>**int pathPointCount**

**只对 `PolygonPath` 有意义**：顶点**个数**（不是浮点数个数），至少 3。

>**float x / float y / float width / float height**（`ShapeBounds`）

包围盒的位置与尺寸，相对组件左上角，设计坐标。

## APIS

### Public

>**Rect()**
原名:

```C++
static constexpr ShapeSpec Rect();
```

直角矩形。就是默认值（= 不写 shape），尺寸完全来自 `width` / `height`。

>**RoundedRect(float radius)**
原名:

```C++
static ShapeSpec RoundedRect(float radius);
```

圆角矩形。`radius < 0` 是配置错误，断言挡住；过大则**夹取**（但上限要等拿到
`width` / `height` 才知道，所以夹取发生在 `ClampRadius` / `EffectiveRadius`，不在这里）。
夹取的理由：半径超过一半就画不成圆角矩形了——负半径更糟，会让四个角"翻出去"长成另一个形状，
所以负值必须在配置期截下来。

>**Circle()**
原名:

```C++
static constexpr ShapeSpec Circle();
```

正圆：直径取 `min(width,height)`，**居中**。

>**Ellipse()**
原名:

```C++
static constexpr ShapeSpec Ellipse();
```

椭圆：用满 `width × height`。

>**Capsule()**
原名:

```C++
static constexpr ShapeSpec Capsule();
```

胶囊：用满 `width × height`，两端是半径 `min(w,h)/2` 的半圆（沿长轴）。

>**Ring(float thickness)**
原名:

```C++
static ShapeSpec Ring(float thickness);
```

圆环：外径取 `min(w,h)`、居中，环宽 `thickness`（往里长）。环宽没有"负"这种语义，
所以 `<= 0` 是配置错误，断言挡住。真正生效的环宽用 `detail::ShapeRingThickness` 算
（顶到外半径就停，再厚就没有孔了、退化成实心圆）。

>**Line(float thickness, LineCap cap = LineCap::Butt)**
原名:

```C++
static ShapeSpec Line(float thickness, LineCap cap = LineCap::Butt);
```

水平线段：纵向居中，线宽 `thickness`，端点形状 `cap`（默认平头）。

>**Polygon(int sides, float rotation = 0.0f)**
原名:

```C++
static ShapeSpec Polygon(int sides, float rotation = 0.0f);
```

正多边形：外接圆直径取 `min(w,h)`、居中，`sides` 条边，`rotation` 是起始角（度）。
少于三条边没有"内"可言，断言挡住并兜底成 3。

>**ChamferRect(float chamfer)**
原名:

```C++
static ShapeSpec ChamferRect(float chamfer);
```

切角矩形：用满 `width × height`，四个角各切掉 `chamfer` 长的一条 45° 斜边。
负值断言挡住；真正生效的长度上限是短边的一半。

>**Star(int points, float innerRatio = 0.5f, float rotation = 0.0f)**
原名:

```C++
static ShapeSpec Star(int points, float innerRatio = 0.5f,
                      float rotation = 0.0f);
```

星形：外接圆直径取 `min(w,h)`、居中，`points` 个角，`innerRatio` 是内 / 外半径之比。
`innerRatio` 夹到 `[0, 1]` 而**不报错**——`0` 是内半径为零的尖角（合法，画出来是放射线）、
`1` 是退化成圆（也合法），超出去就不是星形了，与半径那一条一样属于形状自己的数学要求。

>**Superellipse(float exponent)**
原名:

```C++
static ShapeSpec Superellipse(float exponent);
```

超椭圆：用满 `width × height`，指数 `exponent`（> 0）。`2` = 椭圆，越大越方。
断言挡住非正值，实际使用前夹到 `[0.05, 16]`：太小会退化成十字线，太大 `pow` 会在
远离中心的点上溢出（`u^n` 直接冲上 `inf`）。

>**Arc(float thickness, float startAngle, float sweepAngle)**
原名:

```C++
static ShapeSpec Arc(float thickness, float startAngle, float sweepAngle);
```

圆弧：外径取 `min(w,h)`、居中，环宽 `thickness`（往里长）；从 `startAngle` 起、
顺时针扫 `sweepAngle` 度，两端**径向切平**。
`sweepAngle` 到 360（或更多）就退化成整圈的 `ring`——同样是形状本身的数学要求，
夹住而不是报错。

>**PolygonPath(const float* points, int count)**
原名:

```C++
static ShapeSpec PolygonPath(const float* points, int count);
```

任意多边形：`points` 是 `x0,y0,x1,y1,…` 的设计坐标顶点表，`count` 是**点数**
（不是浮点数个数），至少 3。
与 `Polygon`（正多边形）的区别：这个什么形状都能表达——包括非凸、甚至"关于中心不是星形域"的
（U 形那种，中心落在凹口里）。填充走 ear clipping 三角化，所以凹口不会被填实。
顶点表**不拷贝**，生命周期要覆盖用它的组件。

>**ClampRadius(float radius, int width, int height)**
原名:

```C++
static float ClampRadius(float radius, int width, int height);
```

半径的夹取：上限是 `min(width,height)/2`，负值一律归零。
这是形状本身的数学要求（超过一半就画不成圆角矩形了），属于**允许自动修正**的那一类——
不是配置错误，所以夹取而不是报错。

>**EffectiveRadius(int width, int height)**
原名:

```C++
float EffectiveRadius(int width, int height) const;
```

夹取之后**真正生效**的半径（宽或高有一边是 0 时就是 0）。这是 `radius` 的规范读法。

>**LineCapName(LineCap cap)**
原名:

```C++
inline const char* LineCapName(LineCap cap);
```

`LineCap` 的配置写法（`"round"` / `"square"` / `"butt"`）。坏值不该走到这里
（生成期就拦下了），所以 `default` 一并返回 `"butt"`。

>**SignedDistance(shape, x, y, width, height)**
原名:

```C++
inline float SignedDistance(const ShapeSpec& shape, float x, float y, int width,
                            int height);
```

点的**带符号距离**，单位是设计坐标；`x,y` 是**相对组件左上角**的坐标。
返回值 `f < 0` 在内部、`f = 0` 在边界、`f > 0` 在外部。

各形状的算法要点（了解即可，改形状时要知道边界在哪）：

- **`rect`** —— 把点按两条中轴线折叠到右下象限，再用 `ShapeBoxDistance` 算；
- **`roundedRect`** —— 内缩矩形 + 到角心的距离。**关键在最后减的那一项**：边上到的是边线
  （已经是真距离，不用再减半径），角上到的是角心（要减半径），所以减的是
  `min(max(qx,qy), 0)`；直接在结果上减 `r` 会把四条直边也算短 `r`——圆角矩形的中心到上边
  是 `h/2`，不是 `h/2 - r`；
- **`circle`** —— 半径 `min(w,h)/2`、圆心 `(w/2, h/2)`；
- **`ellipse`** —— 椭圆的 SDF **没有精确闭式**（真值要解数值最近点），这里要的是符号正确的
  近似：内外判定精确，距离在边缘附近也够准（在归一化空间算到单位圆的距离，再乘回最小半径）；
- **`capsule`** —— 脊椎线段 + 半径 `r` 的圆：到形态的距离 = 到线段距离 − `r`；
- **`ring`** —— 到"中径圆"的距离再减去半个环宽（`|d − rMid| − t/2`），内外两侧合起来正好是
  到环带边界的距离；`t = 0` 时只有那圈线算"在内"；
- **`line`** —— 圆头就是"到线段距离 − 半径"；平头 / 方头都是矩形，方头只是两端各外扩 `half`；
- **`polygon`** —— 正多边形是**凸**的：先取"到各边所在直线"的最大带符号距离当符号与内部距离
  （`f <= 0` 恰好等价于"在每条边的内侧"），外部再用"到边界折线的距离"取精确值，两条合起来
  就是精确 SDF。判定外法向用的是"顶点相对中心的方向"，**与顶点绕向无关**；
- **`chamferRect`** —— 八个半平面的交（四条边 + 四条 45° 斜边），凸集的 SDF 就是"到各面所在
  平面的带符号距离取最大"，精确、不用近似（斜边距离要乘 `1/√2` 才是真的欧氏距离）；
- **`star` / `polygonPath`** —— 非凸，没有"半平面取最大"那条捷径了，回到最朴素也最不容易错的
  一种：**符号用射线法**（crossing number，绕向无关）、**距离用点到边界的最近距离**。
  两者共用同一份 `detail::ShapePolygonSdf`（参数是"点数 + 取点函数"），两处各写一遍的话迟早
  有一处忘了处理凹口，而那种错只在特定角度上露头；
- **`superellipse`** —— `|x/a|^n + |y/b|^n = 1` 也没有初等闭式，做法与 `ellipse` 同一个精神：
  符号精确（归一化后的 `f` 与 1 比），距离按最小半轴缩放回来；
- **`arc`** —— 圆弧带 = **环带 ∩ 两个端头半平面**，两样各是一个简单距离场，**取最大**就得到交的
  SDF。端头用"过端点的径向线"那个半平面表示（距离就是点到那条线的带符号距离）。
  **端头千万不能写成"到端点的距离 − 半个环宽"**：那是圆头，而渲染侧的两端是切平的——
  两边分家就会出现"画了没点中 / 点中了没画"。扫满一圈时只剩环带、没有端头。

>**ShapeContains(shape, x, y, width, height)**
原名:

```C++
inline bool ShapeContains(const ShapeSpec& shape, float x, float y, int width,
                          int height);
```

这个点在不在形状里（命中判定）。与 `SignedDistance` **同源**：判据就是 `f <= 0`，
和裁剪保留的区域完全一致，所以不会出现"画的是圆角、点到的是直角"。

>**GetShapeBounds(shape, width, height)**
原名:

```C++
inline ShapeBounds GetShapeBounds(const ShapeSpec& shape, int width,
                                  int height);
```

形状的包围盒（AABB），相对组件左上角。将来命中表按格子索引时就问这个：
半径不同、形状不同，包围盒也可能不同，所以**不能一律拿 `width × height` 顶上**。

各形状的包围盒：

- **`circle` / `ring` / `star` / `arc`** —— 居中、直径 `min(w,h)` 的正方形（宽高不等时两侧留白，
  包围盒跟着小）。`ring` 的环带是从外径往里长的，所以就是外径那个圆；`arc` 整个落在外径圆里，
  **保守但一定装得下**（按实际角度范围算更紧，但那段代码只为了省几个格子，不值当）；
- **`line`** —— 纵向是 `thickness` 那一条、横向按端点形状外扩。**别让包围盒只用"线段本身"**
  （高度是 0）——命中表按它登记格子，登记不全就是漏命中；
- **`polygon` / `polygonPath`** —— 按顶点**实算**（`sides` / 点数都不多，不亏）。
  `polygonPath` 可能只占框里的一小块、甚至是偏的，用整框会白白多占格子；
- **其余（`rect` / `roundedRect` / `ellipse` / `capsule` / `chamferRect` / `superellipse`）** ——
  一律用满 `width × height`：圆角只是把四角磨掉、切角只是把角切掉、胶囊两端就是矩形边界、
  超椭圆在 `(±a,0)` / `(0,±b)` 处顶到边，都不影响包围盒。

## detail 命名空间（内部小工具）

内部小工具，名字统一带 `Shape` / `shape` 前缀。**业务代码不要直接调**：它们不进公开接口，
只为形状内部算几何用，将来可能随实现调整。

>**shapePi / shapeDegToRad**

`constexpr` 常量：π 与"度 → 弧度"。

>**ShapePoint**

一个点（相对组件左上角），只为形状内部算几何用。

>**ShapeBoxDistance(px, py, cx, cy, halfW, halfH)**

**圆角矩形 / 线段用到的直角矩形 SDF**（中心 + 两个半轴）。

>**ShapeSegmentDistance(px, py, ax, ay, bx, by)**

点到线段（含两端）的距离。胶囊 / 圆头线段 / 多边形外包都用它。

>**ShapeRingThickness(thickness, width, height)**

圆环真正生效的环宽：顶到外半径就停（再厚就没有孔了，退化成实心圆）。

>**ShapePolygonSides(shape)**

正多边形的边数（`< 3` 的值不该漏到这里，工厂已经挡过；这里只做兜底）。

>**ShapePolygonCircumradius(width, height)**

正多边形的**外接圆**半径：和 `circle` 同一个约定（居中、直径取 `min(w,h)`）。

>**ShapePolygonVertex(shape, width, height, k)**

正多边形的第 k 个顶点（`k` 会对边数取模），相对组件左上角。

>**ShapeStarPoints(shape)**

星形的角数（兜底同上）。

>**ShapeStarOuterRadius(width, height)**

星形的**外接圆**半径：和 `circle` / `polygon` 同一个约定。

>**ShapeStarInnerRadius(shape, width, height)**

星形的**内**半径：外半径 × `innerRatio`（比例已夹到 `[0,1]`）。

>**ShapeStarVertex(shape, width, height, k)**

星形的第 k 个顶点（对 `2×points` 取模）：**偶数下标在外半径、奇数在内半径**，
一圈走完正好 `points` 个角。

>**ShapeChamferAmount(chamfer, w, h)**

切角矩形真正生效的切角长度：上限是短边的一半（再长就没有直边了）。

>**ShapeSuperellipseExponent(shape)**

超椭圆的指数，夹在一个**浮点安全**的范围里（见 `Superellipse()` 那条）。

>**ShapeArcThickness(thickness, outerRadius)**

圆弧真正生效的环宽：和 `ring` 同一个语义（顶到外半径就停）。

>**ShapeArcSweepRadians(shape)**

圆弧真正生效的扫角（弧度，> 0，≤ 2π）：`|sweepAngle|` 夹到 `(0, 360]` 度。

>**ShapeArcDirection(shape)**

圆弧的扫向：屏幕 y 向下，角度增大就是顺时针——正扫角 = `+1`、负 = `-1`。

>**ShapePathVertex(shape, k)**

`PolygonPath` 的第 k 个顶点（对点数取模），相对组件左上角。

>**ShapePolygonSdf(count, vertexAt, x, y)**

任意多边形的带符号距离——**非凸也成立**：符号用射线法、距离用"到各边的最近距离"。
参数是"点数 + 取点函数"而不是某个具体形状，所以 `Star`（顶点由公式算）与
`PolygonPath`（顶点来自表）共用这一份实现。

---

> 本文档对应版本：**0.1.0-test（测试版本）**。
