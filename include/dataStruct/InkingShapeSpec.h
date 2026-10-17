#pragma once

// 形状层：SDF（带符号距离场）。
//
// 符号用通用约定，即：
//   f < 0 在内部、f = 0 在边界、f > 0 在外部；
//   裁剪 = 保留 f <= 0 的区域。
// 这样公开的 SDF 参考实现可以照着抄，不用逐个翻符号；抗锯齿也是标准写法
// `smoothstep(-aa, aa, d)`；`abs(d)` 天然就是"到边界的距离"。
//
// ---------------------------------------------------------------------------
// 一条硬约定：**形状同时决定渲染 / 命中 / AABB，三者从这一份定义派生**
//
// 各写一遍就会出"画的是圆角、点到的是直角"——两边单独看都对，合起来才错，
// 而且极难排查（CXXCSS.md §3）。所以本文件只提供三个派生量，且都从这里算：
//   SignedDistance   → 渲染（填充 / 描边 / 裁剪，形状层接手后）
//   ShapeContains    → 命中（这个点在不在形状里，现在就能用）
//   GetShapeBounds   → AABB（将来塞进命中表的哪个格子）
//
// ---------------------------------------------------------------------------
// 坐标系与形状语义（都以组件左上角为原点、单位是设计坐标）
//
//   rect         用满 width × height
//   roundedRect  用满 width × height，四角半径 radius（夹到 min(w,h)/2）
//   circle       正圆，直径取 min(w,h)，**居中**
//   ellipse      椭圆，用满 width × height
//   capsule      用满 width × height，两端是半径 min(w,h)/2 的半圆（沿长轴）
//   ring         圆环，外径取 min(w,h)、**居中**，环宽 thickness（往里长）
//   line         水平线段，纵向居中；线宽 thickness，端点形状 cap
//   polygon      正多边形，外接圆直径取 min(w,h)、**居中**；sides 条边、
//                rotation 是起始角
//
// 注意 circle 是居中的：一个 100×60 的组件画圆，得到的是居中、直径 60 的圆，
// 而不是一个 100×60 的"椭圆"。要椭圆就写 ellipse。

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cmath>
#include <limits>

namespace inking {

// 形状的种类。放一个显式的整数而不是直接塞 variant：
// 生成器是按 `type` 查表展开的，运行期只需要"这一个是哪一种"。
enum class ShapeKind : std::uint8_t {
    Rect = 0,//矩形
    RoundedRect,//圆角矩形
    Circle,//圆
    Ellipse,//椭圆
    Capsule,//胶囊体
    Ring,//圆环
    Line,//线
    Polygon,//多边形
    ChamferRect,    // 切角矩形：四条边照旧，四个角用 45° 直线切掉
    Star,           // 星形：外/内半径交替的正星形（非凸）
    Superellipse,   // 超椭圆：`n` 控制"从椭圆到方"的过渡（n < 1 是凹的星形角）
    Arc,            // 圆弧：圆环的一段（带环宽），两端径向切平
    PolygonPath,    // 顶点表给出来的任意多边形（可以非凸，甚至可以不是星形域）
};

/**
 * @brief 线段的端点形状（只对 `line` 有意义）。
 *
 * 名字与 CXXCSS 里的字符串一一对应（`butt` / `round` / `square`），
 * 语义照 CSS 的 `stroke-linecap`：平头、圆头、方头。
 */
enum class LineCap : std::uint8_t {
    Butt = 0,  // 平头：到端点就断（默认）
    Round,     // 圆头：两端各一个半径 thickness/2 的半圆
    Square,    // 方头：两端各外扩 thickness/2
};

//`LineCap` 的配置写法；坏值不该走到这里（生成期就拦下了）。
inline const char* LineCapName(LineCap cap) {
    switch (cap) {
        case LineCap::Round:
            return "round";
        case LineCap::Square:
            return "square";
        case LineCap::Butt:
        default:
            return "butt";
    }
}

/**
 * @brief 一个形状的定义：种类 + 只属于它的那几个参数。
 *
 * 需要明确调用type查形状表做参数校验，所以强调使用key。
 * `{"type":"roundedRect","radius":10}`，生成器在编译期把它展开成本结构。
 * 该结构体是形状的说明书，将全部形状整合到一起，方便调用
 *
 * 用工厂函数造，别手动填字段：夹取和校验都在工厂里做了。
 */
struct ShapeSpec {
    ShapeKind kind = ShapeKind::Rect;

    // 只对 RoundedRect 有意义；一律已夹到 [0, min(w,h)/2]。
    float radius = 0.0f;

    // Ring 的环宽 / Line 的线宽（设计坐标，> 0）。
    float thickness = 0.0f;

    // 只对 Line 有意义。
    LineCap cap = LineCap::Butt;

    // 只对 Polygon 有意义：边数（>= 3）。
    int sides = 0;

    /**
     * 只对 Polygon 有意义
     * 起始角
     * 单位是度，跟 `TransformSpec::rotate` 统一（`InkingTransform.h` 定过
     * 这条：配置里写 15 比写 0.2618 直观）；屏幕坐标 y 向下，所以顺时针为正。
     * `0` 表示第一个顶点落在 +x（正右方），其余顶点按 360/sides 顺时针排。
     *
     * Star 也用它（第一个外角的方向）。
     */
    float rotation = 0.0f;

    // 只对 ChamferRect 有意义：切角沿边的长度（> 0）。
    float chamfer = 0.0f;

    // 只对 Star 有意义：角数（>= 3）。
    int points = 0;

    // 只对 Star 有意义：内半径 / 外半径（0~1，越小越尖）。
    float innerRatio = 0.5f;

    // 只对 Superellipse 有意义：指数 n（n = 2 是椭圆，越大越接近矩形）。
    float exponent = 2.0f;

    // 只对 Arc 有意义：**起始角**（度，0 = 正右方，顺时针为正，同 `rotation`）。
    float startAngle = 0.0f;

    // 只对 Arc 有意义：**扫过的角度**（度，正 = 顺时针，> 0；夹到 ≤ 360）。
    float sweepAngle = 360.0f;

    /**
     * 只对 `PolygonPath` 有意义：**顶点表**（`x0,y0,x1,y1,…`，设计坐标，
     * 相对组件左上角）。
     *
     * 这里**不拥有**数据，只借一个视图——和 `ButtonAppearance::imageBytes`
     * 同一个约定：生成器烘的是**文件作用域的静态数组**，生命周期是整个进程，
     * 比任何组件都长。手写 `ShapeSpec` 时要自己保证这块内存活得够久
     * （拷了 `ShapeSpec` 的组件会一直拿着这个指针）。
     */
    const float* pathPoints = nullptr;
    int pathPointCount = 0;

    // 直角矩形（= 不写 shape），尺寸完全来自 width / height。
    static constexpr ShapeSpec Rect() { return ShapeSpec{}; }

    // 圆角矩形。radius < 0 是配置错误，这里断言挡住；过大则夹到上限。
    static ShapeSpec RoundedRect(float radius) {
        ShapeSpec spec;
        spec.kind = ShapeKind::RoundedRect;
        // 负半径没有意义，而且会让四个角"翻出去"长成另一个形状——
        // 为了保证形状的准确性，必须在编译期将报错截下来
        assert(radius >= 0.0f && "shape:radius 不能为负");
        spec.radius = radius < 0.0f ? 0.0f : radius;
        return spec;
    }

    // 正圆：直径取 min(width,height)，居中。
    static constexpr ShapeSpec Circle() {
        ShapeSpec spec;
        spec.kind = ShapeKind::Circle;
        return spec;
    }

    // 椭圆：用满 width × height。
    static constexpr ShapeSpec Ellipse() {
        ShapeSpec spec;
        spec.kind = ShapeKind::Ellipse;
        return spec;
    }

    // 胶囊：用满 width × height，两端是半径 min(w,h)/2 的半圆（沿长轴）。
    static constexpr ShapeSpec Capsule() {
        ShapeSpec spec;
        spec.kind = ShapeKind::Capsule;
        return spec;
    }

    // 圆环：外径取 min(w,h)、居中，环宽 thickness（往里长）。
    static ShapeSpec Ring(float thickness) {
        ShapeSpec spec;
        spec.kind = ShapeKind::Ring;
        // 环宽没有"负"这种语义
        assert(thickness > 0.0f && "shape:thickness 必须大于 0");
        spec.thickness = thickness > 0.0f ? thickness : 0.0f;
        return spec;
    }

    // 水平线段：纵向居中，线宽 thickness，端点形状 cap。
    static ShapeSpec Line(float thickness, LineCap cap = LineCap::Butt) {
        ShapeSpec spec;
        spec.kind = ShapeKind::Line;
        assert(thickness > 0.0f && "shape:thickness 必须大于 0");
        spec.thickness = thickness > 0.0f ? thickness : 0.0f;
        spec.cap = cap;
        return spec;
    }

    // 正多边形：外接圆直径取 min(w,h)、居中，sides 条边，rotation 是起始角（度）。
    static ShapeSpec Polygon(int sides, float rotation = 0.0f) {
        ShapeSpec spec;
        spec.kind = ShapeKind::Polygon;
        // 少于三条边没有"内"可言
        assert(sides >= 3 && "shape:sides 必须 >= 3");
        spec.sides = sides >= 3 ? sides : 3;
        spec.rotation = rotation;
        return spec;
    }

    // 切角矩形：用满 width × height，四个角各切掉 chamfer 长的一条 45° 斜边。
    static ShapeSpec ChamferRect(float chamfer) {
        ShapeSpec spec;
        spec.kind = ShapeKind::ChamferRect;
        assert(chamfer >= 0.0f && "shape:chamfer 不能为负");
        spec.chamfer = chamfer < 0.0f ? 0.0f : chamfer;
        return spec;
    }

    // 星形：外接圆直径取 min(w,h)、居中，points 个角，innerRatio 是内/外半径之比。
    static ShapeSpec Star(int points, float innerRatio = 0.5f,
                          float rotation = 0.0f) {
        ShapeSpec spec;
        spec.kind = ShapeKind::Star;
        assert(points >= 3 && "shape:points 必须 >= 3");
        spec.points = points >= 3 ? points : 3;
        // 比例夹到 [0, 1]：0 是内半径为零的尖角（合法，画出来是放射线），
        // 1 是退化成圆（也合法）。超出去就不是星形了，夹住而不是报错——
        // 和半径那一条同样属于形状自己的数学要求。
        spec.innerRatio = std::clamp(innerRatio, 0.0f, 1.0f);
        spec.rotation = rotation;
        return spec;
    }

    // 超椭圆：用满 width × height，指数 exponent（> 0）。2 = 椭圆，越大越方。
    static ShapeSpec Superellipse(float exponent) {
        ShapeSpec spec;
        spec.kind = ShapeKind::Superellipse;
        assert(exponent > 0.0f && "shape:exponent 必须大于 0");
        spec.exponent = std::clamp(exponent, 0.05f, 16.0f);
        return spec;
    }

    /**
     * 圆弧：外径取 min(w,h)、居中，环宽 thickness（往里长）；
     * 从 startAngle 起、顺时针扫 sweepAngle 度，两端径向切平。
     *
     * `sweepAngle` 到 360（或更多）就退化成整圈的 `ring`——这是形状本身的
     * 数学要求，夹住而不是报错。
     */
    static ShapeSpec Arc(float thickness, float startAngle, float sweepAngle) {
        ShapeSpec spec;
        spec.kind = ShapeKind::Arc;
        assert(thickness > 0.0f && "shape:thickness 必须大于 0");
        spec.thickness = thickness > 0.0f ? thickness : 0.0f;
        spec.startAngle = startAngle;
        spec.sweepAngle = sweepAngle;
        return spec;
    }

    /**
     * 任意多边形：`points` 是 `x0,y0,x1,y1,…` 的设计坐标顶点表，
     * `count` 是点数*不是浮点数个数），至少 3。
     *
     * 与 `Polygon`（正多边形）的区别：这个什么形状都能表达——包括非凸、
     * 甚至"关于中心不是星形域"的（U 形那种，中心落在凹口里）。
     * 填充走ear clipping 三角化，所以凹口不会被填实。
     *
     * @param points 顶点表；不拷贝，生命周期要覆盖用它的组件。
     */
    static ShapeSpec PolygonPath(const float* points, int count) {
        ShapeSpec spec;
        spec.kind = ShapeKind::PolygonPath;
        assert(points != nullptr && count >= 3 && "polygonPath 至少要 3 个点");
        spec.pathPoints = points;
        spec.pathPointCount = (points != nullptr && count >= 3) ? count : 0;
        return spec;
    }

    /**
     * 半径的夹取：上限是 min(width,height)/2。
     *
     * 这是形状本身的数学要求（超过一半就画不成圆角矩形了），
     * 允许自动修正的那一类——不是配置错误，所以夹取而不是报错。
     */
    static float ClampRadius(float radius, int width, int height) {
        const float shorter =
            static_cast<float>(width < height ? width : height);
        const float limit = shorter * 0.5f;
        if (radius < 0.0f) {
            return 0.0f;
        }
        return radius > limit ? limit : radius;
    }

    /** 夹取之后真正生效的半径（宽或高有一边是 0 时就是 0）。 */
    float EffectiveRadius(int width, int height) const {
        return ClampRadius(radius, width, height);
    }
};

// 内部小工具
//
// 名字统一带 `Shape` / `shape` 前缀
namespace detail {

constexpr float shapePi = 3.14159265358979323846f;
constexpr float shapeDegToRad = shapePi / 180.0f;

/// 一个点（相对组件左上角）。只为形状内部算几何用，不进公开接口。
struct ShapePoint {
    float x = 0.0f;
    float y = 0.0f;
};

/// 圆角矩形 / 线段用到的**圆角矩形 SDF**（中心 + 两个半轴，直角版本）。
inline float ShapeBoxDistance(float px, float py, float cx, float cy,
                              float halfW, float halfH) {
    const float qx = std::fabs(px - cx) - halfW;
    const float qy = std::fabs(py - cy) - halfH;

    // 角上：到角顶点的距离（两个都为正）。
    const float outsideX = qx > 0.0f ? qx : 0.0f;
    const float outsideY = qy > 0.0f ? qy : 0.0f;
    const float outside = std::sqrt(outsideX * outsideX + outsideY * outsideY);

    // 内部：两个都是负数，取**较大**的那个（离边界更近）当负距离。
    const float nearer = qx > qy ? qx : qy;
    const float inside = nearer < 0.0f ? nearer : 0.0f;
    return outside + inside;
}

/// 点到线段（含两端）的距离。胶囊 / 圆头线段 / 多边形外包都用它。
inline float ShapeSegmentDistance(float px, float py, float ax, float ay,
                                  float bx, float by) {
    const float ex = bx - ax;
    const float ey = by - ay;
    const float lengthSquared = ex * ex + ey * ey;
    float t = 0.0f;
    if (lengthSquared > 0.0f) {
        t = ((px - ax) * ex + (py - ay) * ey) / lengthSquared;
        t = std::clamp(t, 0.0f, 1.0f);
    }
    const float dx = px - (ax + ex * t);
    const float dy = py - (ay + ey * t);
    return std::sqrt(dx * dx + dy * dy);
}

/// 圆环真正生效的环宽：顶到外半径就停（再厚就没有孔了，退化成实心圆）。
inline float ShapeRingThickness(float thickness, int width, int height) {
    const float shorter = static_cast<float>(width < height ? width : height);
    const float outerRadius = shorter * 0.5f;
    if (thickness <= 0.0f) {
        return 0.0f;
    }
    return thickness > outerRadius ? outerRadius : thickness;
}

/// 正多边形的边数（< 3 的值不该漏到这里，工厂已经挡过；这里只做兜底）。
inline int ShapePolygonSides(const ShapeSpec& shape) {
    return shape.sides >= 3 ? shape.sides : 3;
}

/// 正多边形的**外接圆**半径：和 circle 同一个约定（居中、直径取 min(w,h)）。
inline float ShapePolygonCircumradius(int width, int height) {
    const float shorter = static_cast<float>(width < height ? width : height);
    return shorter * 0.5f;
}

/// 正多边形的第 k 个顶点（k 会对边数取模），相对组件左上角。
inline ShapePoint ShapePolygonVertex(const ShapeSpec& shape, int width,
                                     int height, int k) {
    const int sides = ShapePolygonSides(shape);
    const float radius = ShapePolygonCircumradius(width, height);
    const float angle =
        (shape.rotation + 360.0f * static_cast<float>(k)
                              / static_cast<float>(sides))
        * shapeDegToRad;
    return ShapePoint{static_cast<float>(width) * 0.5f + std::cos(angle) * radius,
                      static_cast<float>(height) * 0.5f
                          + std::sin(angle) * radius};
}

/// 星形的角数（< 3 的值不该漏到这里，工厂已经挡过；这里只做兜底）。
inline int ShapeStarPoints(const ShapeSpec& shape) {
    return shape.points >= 3 ? shape.points : 3;
}

/// 星形的**外接圆**半径：和 circle / polygon 同一个约定（居中、直径取 min(w,h)）。
inline float ShapeStarOuterRadius(int width, int height) {
    const float shorter = static_cast<float>(width < height ? width : height);
    return shorter * 0.5f;
}

/// 星形的**内**半径：外半径 × innerRatio（比例已夹到 [0,1]）。
inline float ShapeStarInnerRadius(const ShapeSpec& shape, int width, int height) {
    const float ratio = std::clamp(shape.innerRatio, 0.0f, 1.0f);
    return ShapeStarOuterRadius(width, height) * ratio;
}

/**
 * 星形的第 k 个顶点（对 2×points 取模）：**偶数下标在外半径、奇数在内半径**，
 * 一圈走完正好 points 个角。`rotation` 是第一个外角的方向（度，顺时针为正）。
 */
inline ShapePoint ShapeStarVertex(const ShapeSpec& shape, int width, int height,
                                  int k) {
    const int points = ShapeStarPoints(shape);
    const int count = points * 2;
    int index = k % count;
    if (index < 0) {
        index += count;
    }
    const bool outer = (index % 2) == 0;
    const float radius = outer ? ShapeStarOuterRadius(width, height)
                               : ShapeStarInnerRadius(shape, width, height);
    const float step = 360.0f / static_cast<float>(count);
    const float angle =
        (shape.rotation + step * static_cast<float>(index)) * shapeDegToRad;
    return ShapePoint{static_cast<float>(width) * 0.5f + std::cos(angle) * radius,
                      static_cast<float>(height) * 0.5f
                          + std::sin(angle) * radius};
}

// 切角矩形真正生效的切角长度：上限是短边的一半（再长就没有直边了）。
inline float ShapeChamferAmount(float chamfer, float w, float h) {
    const float shorter = w < h ? w : h;
    const float limit = shorter * 0.5f;
    if (chamfer <= 0.0f) {
        return 0.0f;
    }
    return chamfer > limit ? limit : chamfer;
}

/**
 * 超椭圆的指数，夹在一个浮点安全的范围里：太小会退化成十字线，
 * 太大 `pow` 会在远离中心的点上溢出（`u^n` 直接冲上 inf）。
 */
inline float ShapeSuperellipseExponent(const ShapeSpec& shape) {
    return std::clamp(shape.exponent, 0.05f, 16.0f);
}

// 圆弧真正生效的环宽：和 `ring` 同一个语义（顶到外半径就停）。
inline float ShapeArcThickness(float thickness, float outerRadius) {
    if (thickness <= 0.0f) {
        return 0.0f;
    }
    return thickness > outerRadius ? outerRadius : thickness;
}

// 圆弧真正生效的扫角（弧度，> 0，≤ 2π）：`|sweepAngle|` 夹到 (0, 360] 度。
inline float ShapeArcSweepRadians(const ShapeSpec& shape) {
    const float degrees = std::clamp(std::fabs(shape.sweepAngle), 0.0f, 360.0f);
    return degrees * shapeDegToRad;
}

// 圆弧的扫向：屏幕 y 向下，角度增大就是顺时针——正扫角 = +1、负 = -1。
inline float ShapeArcDirection(const ShapeSpec& shape) {
    return shape.sweepAngle < 0.0f ? -1.0f : 1.0f;
}

// `PolygonPath` 的第 k 个顶点（对点数取模），相对组件左上角。
inline ShapePoint ShapePathVertex(const ShapeSpec& shape, int k) {
    const int count = shape.pathPointCount;
    if (count <= 0 || shape.pathPoints == nullptr) {
        return ShapePoint{};
    }
    int index = k % count;
    if (index < 0) {
        index += count;
    }
    return ShapePoint{shape.pathPoints[index * 2],
                      shape.pathPoints[index * 2 + 1]};
}

/**
 * 任意多边形的带符号距离——非凸也成立：符号用射线法（crossing number），
 * 距离用"到各边的最近距离"。
 *
 * 参数是"点数 + 取点函数"而不是某个具体形状：`Star`（顶点由公式算）与
 * `PolygonPath`（顶点来自表）共用这一份实现。两处各写一遍的话，迟早有一处
 * 忘了处理凹口——那种错只在特定角度上露头，很难查。
 */
template <typename VertexAt>
inline float ShapePolygonSdf(int count, VertexAt vertexAt, float x, float y) {
    if (count < 3) {
        return 0.0f;
    }
    bool inside = false;
    float nearest = std::numeric_limits<float>::max();
    for (int i = 0; i < count; ++i) {
        const ShapePoint a = vertexAt(i);
        const ShapePoint b = vertexAt(i + 1);
        const float edge = ShapeSegmentDistance(x, y, a.x, a.y, b.x, b.y);
        if (edge < nearest) {
            nearest = edge;
        }
        // 射线法：从这点往 +x 打一条射线，数它穿过几条边。顶点绕向无关。
        if ((a.y > y) != (b.y > y)) {
            const float crossX = a.x + (y - a.y) * (b.x - a.x) / (b.y - a.y);
            if (x < crossX) {
                inside = !inside;
            }
        }
    }
    return inside ? -nearest : nearest;
}

}  // namespace detail

/**
 * @brief 点的带符号距离，单位是设计坐标。
 * @param x,y 相对组件左上角的坐标
 * @return f < 0 在内部、f = 0 在边界、f > 0 在外部
 */
inline float SignedDistance(const ShapeSpec& shape, float x, float y, int width,
                            int height) {
    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);

    switch (shape.kind) {
        case ShapeKind::RoundedRect: {
            // 半径先夹取（形状自己的数学要求），再按"内缩矩形 + 到角心的距离"算。
            //
            // 关键在最后减的那一项：
            //   - 边上（内缩矩形之外、但只有一个方向越界）到的是**边线**，
            //     已经是真距离，不用再减半径；
            //   - 角上到的是**角心**，要减掉半径才是到圆弧的距离。
            // 所以减的是 `min(max(qx,qy), 0)`：只有"角上"这个分量才非零。
            // 直接在结果上减 r 会把四条直边也算短 r——圆角矩形的中心到上边
            // 就是 h/2，不是 h/2 - r。
            const float r = shape.EffectiveRadius(width, height);
            const float halfW = w * 0.5f;
            const float halfH = h * 0.5f;
            const float innerW = halfW - r;
            const float innerH = halfH - r;

            // 把点搬到"内缩矩形的角"为原点的象限里，取绝对值即可复用一份公式。
            const float qx = std::fabs(x - halfW) - innerW;
            const float qy = std::fabs(y - halfH) - innerH;

            const float outsideX = qx > 0.0f ? qx : 0.0f;
            const float outsideY = qy > 0.0f ? qy : 0.0f;
            const float outside =
                std::sqrt(outsideX * outsideX + outsideY * outsideY);

            // 内部时 max(qx,qy) 为负，就是到最近边线的距离。
            const float nearer = qx > qy ? qx : qy;
            const float inside = nearer < 0.0f ? nearer : 0.0f;

            // 只有"两个方向都越界"（真在角上）时才还要减半径。
            return outside + inside - (nearer < 0.0f ? 0.0f : r);
        }

        case ShapeKind::Circle: {
            // 居中、直径取 min(w,h)：所以半径是 min(w,h)/2，圆心是 (w/2, h/2)。
            const float r = (w < h ? w : h) * 0.5f;
            const float dx = x - w * 0.5f;
            const float dy = y - h * 0.5f;
            return std::sqrt(dx * dx + dy * dy) - r;
        }

        case ShapeKind::Ellipse: {
            // 椭圆的 SDF 没有精确闭式（真值要解数值最近点），
            // 但这里要的是符号正确的近似：内外判定精确，
            // 距离在边缘附近也够准。
            //
            // 写法：在归一化空间算到单位圆的距离，再乘回最小半径——
            // 于是内部为负、外部为正、边界为 0，`ShapeContains` 永远正确。
            const float rx = w * 0.5f;
            const float ry = h * 0.5f;
            if (rx <= 0.0f || ry <= 0.0f) {
                const float dx = rx <= 0.0f ? std::fabs(x) : 0.0f;
                const float dy = ry <= 0.0f ? std::fabs(y) : 0.0f;
                return dx + dy;  // 退化成一条线 / 一个点，只有它本身算"在内"
            }
            const float k0 = std::hypot((x - rx) / rx, (y - ry) / ry);
            const float k1 = std::hypot((x - rx) / (rx * rx),
                                        (y - ry) / (ry * ry));
            const float scale = rx < ry ? rx : ry;
            if (k1 <= 0.0f) {
                return -scale;
            }
            return (k0 * (k0 - 1.0f) / k1) * scale;
        }

        case ShapeKind::Capsule: {
            // 胶囊 = 脊椎线段 + 半径 r 的圆（r 取短边的一半，沿长轴放）。
            // 到形态的距离就是这个圆盘的距离：到线段距离 − r。
            const float radius = (w < h ? w : h) * 0.5f;
            float ax = 0.0f;
            float ay = 0.0f;
            float bx = 0.0f;
            float by = 0.0f;
            if (w >= h) {
                ax = radius;
                ay = h * 0.5f;
                bx = w - radius;
                by = h * 0.5f;
            } else {
                ax = w * 0.5f;
                ay = radius;
                bx = w * 0.5f;
                by = h - radius;
            }
            return detail::ShapeSegmentDistance(x, y, ax, ay, bx, by) - radius;
        }

        case ShapeKind::Ring: {
            // 圆环：到"中径圆"的距离再减去半个环宽——内外两侧合起来正好
            // 就是到环带边界的距离（|d − rMid| − t/2）。
            const float outerRadius = (w < h ? w : h) * 0.5f;
            const float thickness =
                detail::ShapeRingThickness(shape.thickness, width, height);
            const float midRadius = outerRadius - thickness * 0.5f;
            const float dx = x - w * 0.5f;
            const float dy = y - h * 0.5f;
            const float distance = std::sqrt(dx * dx + dy * dy);
            // t = 0 时 |d − rOuter| >= 0：孔里孔外都在外面，只有那圈线算"在内"。
            return std::fabs(distance - midRadius) - thickness * 0.5f;
        }

        case ShapeKind::Line: {
            // 水平线段、纵向居中：起点 (0, h/2)、终点 (w, h/2)。
            const float centerY = h * 0.5f;
            const float half = shape.thickness * 0.5f;
            if (shape.cap == LineCap::Round) {
                // 圆头 = 到线段距离 − 半径（两端自然长出半圆）。
                return detail::ShapeSegmentDistance(x, y, 0.0f, centerY, w,
                                                    centerY)
                     - half;
            }
            // 平头 / 方头都是矩形：方头只是两端各外扩 half。
            const float extra = shape.cap == LineCap::Square ? half : 0.0f;
            return detail::ShapeBoxDistance(x, y, w * 0.5f, centerY,
                                            w * 0.5f + extra, half);
        }

        case ShapeKind::Polygon: {
            // 正多边形（凸）：先取"到各边所在直线"的最大带符号距离当符号与内部
            // 距离（f <= 0 恰好等价于"在每条边的内侧"），外部再用"到边界折线
            // 的距离"取精确值——两条一起就是精确 SDF。
            const float radius = detail::ShapePolygonCircumradius(width, height);
            if (radius <= 0.0f) {
                const float dx = x - w * 0.5f;
                const float dy = y - h * 0.5f;
                return std::sqrt(dx * dx + dy * dy);  // 退化成中心一个点
            }
            const int sides = detail::ShapePolygonSides(shape);
            const float centerX = w * 0.5f;
            const float centerY = h * 0.5f;

            float maxPlaneDistance = -std::numeric_limits<float>::max();
            float minEdgeDistance = std::numeric_limits<float>::max();
            for (int i = 0; i < sides; ++i) {
                const detail::ShapePoint a =
                    detail::ShapePolygonVertex(shape, width, height, i);
                const detail::ShapePoint b =
                    detail::ShapePolygonVertex(shape, width, height, i + 1);

                // 外法向：垂直于这条边、指向远离中心的那一侧。判定用的是
                // "顶点相对中心的方向"，与顶点绕向无关，正多边形怎么转都对。
                float nx = b.y - a.y;
                float ny = -(b.x - a.x);
                const float length = std::sqrt(nx * nx + ny * ny);
                if (length > 0.0f) {
                    nx /= length;
                    ny /= length;
                    if (nx * (a.x - centerX) + ny * (a.y - centerY) < 0.0f) {
                        nx = -nx;
                        ny = -ny;
                    }
                    const float plane =
                        (x - a.x) * nx + (y - a.y) * ny;
                    if (plane > maxPlaneDistance) {
                        maxPlaneDistance = plane;
                    }
                }

                const float edge =
                    detail::ShapeSegmentDistance(x, y, a.x, a.y, b.x, b.y);
                if (edge < minEdgeDistance) {
                    minEdgeDistance = edge;
                }
            }
            return maxPlaneDistance <= 0.0f ? maxPlaneDistance : minEdgeDistance;
        }

        case ShapeKind::ChamferRect: {
            // 切角矩形 = 八个半平面的交（四条边 + 四条 45° 斜边）。
            // 凸集的 SDF 就是"到各面所在平面的带符号距离取最大"——精确，不用近似。
            const float chamfer =
                detail::ShapeChamferAmount(shape.chamfer, w, h);
            const float dx = std::fabs(x - w * 0.5f);
            const float dy = std::fabs(y - h * 0.5f);

            // 四条直边（半个矩形）。
            const float box = std::max(dx - w * 0.5f, dy - h * 0.5f);

            // 四条斜边：`|x| + |y| <= 半周长 − 切角`（法向是 (1,1)/√2，
            // 所以距离要乘 1/√2 才是真的欧氏距离）。
            constexpr float kInvSqrt2 = 0.70710678f;
            const float diagonal =
                (dx + dy - (w * 0.5f + h * 0.5f - chamfer)) * kInvSqrt2;

            return std::max(box, diagonal);
        }

        case ShapeKind::Star: {
            // 星形非凸，没有"半平面取最大"那条捷径了。做法回到最朴素、
            // 也最不容易错的一种：符号用射线法、距离用点到边界的最近距离
            // （和 `PolygonPath` 共用一份实现）。
            const int count = detail::ShapeStarPoints(shape) * 2;
            return detail::ShapePolygonSdf(
                count,
                [&](int k) {
                    return detail::ShapeStarVertex(shape, width, height, k);
                },
                x, y);
        }

        case ShapeKind::PolygonPath: {
            // 任意多边形：同一份"射线法 + 最近边"的实现。它可以非凸，
            // 甚至可以不是星形域（U 形那种，中心落在凹口里）——命中与填充
            // 都得照样对。
            const int count = shape.pathPointCount;
            if (count < 3 || shape.pathPoints == nullptr) {
                return 0.0f;  // 顶点表不成立：退化成"整个框都算在内"
            }
            return detail::ShapePolygonSdf(
                count, [&](int k) { return detail::ShapePathVertex(shape, k); },
                x, y);
        }

        case ShapeKind::Superellipse: {
            // 超椭圆 |x/a|^n + |y/b|^n = 1 没有初等闭式的距离，做法与 ellipse
            // 那条同一个精神：符号精确（归一化后的 f 与 1 比），距离在边缘
            // 附近够准（按最小半轴缩放回来）。
            const float a = w * 0.5f;
            const float b = h * 0.5f;
            if (a <= 0.0f || b <= 0.0f) {
                const float dx = a <= 0.0f ? std::fabs(x) : 0.0f;
                const float dy = b <= 0.0f ? std::fabs(y) : 0.0f;
                return dx + dy;  // 退化成一条线 / 一个点
            }
            const float n = detail::ShapeSuperellipseExponent(shape);
            const float u = std::fabs(x - a) / a;
            const float v = std::fabs(y - b) / b;
            const float f = std::pow(std::pow(u, n) + std::pow(v, n), 1.0f / n);
            const float scale = a < b ? a : b;
            return (f - 1.0f) * scale;
        }

        case ShapeKind::Arc: {
            // 圆弧带 = 环带 ∩ 两个端头半平面。两样各是一个简单距离场，
            // 取最大就得到交的 SDF（符号精确；在交角附近是近似，够用）：
            //    环带：`|半径 − 中径| − 半个环宽`；
            //    端头：径向切平，用"过端点的径向线"那个半平面表示，
            //    距离就是点到那条线的带符号距离。
            //
            // 端头为什么不能写成"到端点的距离 − 半个环宽"（圆头）：那画出来的
            // 是圆头，而渲染侧的两端是切平的——两边分家就会出现"画了没点中 /点中了没画"。
            const float outerRadius = (w < h ? w : h) * 0.5f;
            const float thickness =
                detail::ShapeArcThickness(shape.thickness, outerRadius);
            const float midRadius = outerRadius - thickness * 0.5f;
            const float cx = w * 0.5f;
            const float cy = h * 0.5f;
            const float dx = x - cx;
            const float dy = y - cy;
            const float radius = std::sqrt(dx * dx + dy * dy);
            const float half = thickness * 0.5f;
            const float radial = std::fabs(radius - midRadius) - half;
            const float sweep = detail::ShapeArcSweepRadians(shape);

            // 扫满一圈就只剩环带了（没有端头）。
            if (sweep >= 2.0f * detail::shapePi - 1e-4f) {
                return radial;
            }
            if (thickness <= 0.0f) {
                // 退化成一条弧线：只有径向距离，端头照样要切平。
            }

            const float direction = detail::ShapeArcDirection(shape);
            const float start = shape.startAngle * detail::shapeDegToRad;
            const float end = start + direction * sweep;
            // 端头半平面的内法向：角度增大的切向是 (−sin, cos)；
            // 起点朝扫向那一侧、终点朝扫向的来向那一侧。
            const float startNormalX = -std::sin(start) * direction;
            const float startNormalY = std::cos(start) * direction;
            const float endNormalX = std::sin(end) * direction;
            const float endNormalY = -std::cos(end) * direction;
            const float startX = cx + std::cos(start) * midRadius;
            const float startY = cy + std::sin(start) * midRadius;
            const float endX = cx + std::cos(end) * midRadius;
            const float endY = cy + std::sin(end) * midRadius;

            // 半平面的带符号距离：内部为负（`−投影`）。
            const float startPlane = -((x - startX) * startNormalX
                                       + (y - startY) * startNormalY);
            const float endPlane = -((x - endX) * endNormalX
                                     + (y - endY) * endNormalY);
            const float nearest = startPlane > endPlane ? startPlane : endPlane;
            return radial > nearest ? radial : nearest;
        }

        case ShapeKind::Rect:
        default: {
            // 直角矩形：把点按两条中轴线折叠到右下象限再算（见 ShapeBoxDistance）。
            return detail::ShapeBoxDistance(x, y, w * 0.5f, h * 0.5f, w * 0.5f,
                                            h * 0.5f);
        }
    }
}

/**
 * @brief 这个点在不在形状里（命中判定）。
 *
 * 与 `SignedDistance` 同源：判据就是 `f <= 0`，和裁剪保留的区域完全一致，
 * 不会出现"画的是圆角、点到的是直角"。
 */
inline bool ShapeContains(const ShapeSpec& shape, float x, float y, int width,
                          int height) {
    return SignedDistance(shape, x, y, width, height) <= 0.0f;
}

/**
 * @brief 形状的包围盒（AABB），相对组件左上角。
 *
 * 将来命中表按格子索引时就问这个：半径不同、形状不同，包围盒也可能不同
 * （circle 只占中间一块），所以不能一律拿 width × height 顶上。
 */
struct ShapeBounds {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

inline ShapeBounds GetShapeBounds(const ShapeSpec& shape, int width,
                                  int height) {
    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);

    switch (shape.kind) {
        case ShapeKind::Circle: {
            // 居中 + 直径 min(w,h)：宽高不等时两侧会留白，包围盒也跟着小。
            const float diameter = w < h ? w : h;
            return ShapeBounds{(w - diameter) * 0.5f, (h - diameter) * 0.5f,
                               diameter, diameter};
        }
        case ShapeKind::Ring: {
            // 环带是从外径往里长的，所以包围盒就是外径那个圆（和 circle 一样）。
            const float diameter = w < h ? w : h;
            return ShapeBounds{(w - diameter) * 0.5f, (h - diameter) * 0.5f,
                               diameter, diameter};
        }
        case ShapeKind::Line: {
            // 线是水平的：纵向是 thickness 那一条，横向按端点形状外扩。
            // 别让包围盒只用"线段本身"（高度是 0）——命中表按它登记格子，
            // 登记不全就是漏命中（TaskGuide T-1 的坑）。
            const float thickness = shape.thickness > 0.0f ? shape.thickness : 0.0f;
            const float extra = shape.cap == LineCap::Butt ? 0.0f
                                                        : thickness * 0.5f;
            return ShapeBounds{-extra, (h - thickness) * 0.5f, w + 2.0f * extra,
                               thickness};
        }
        case ShapeKind::Polygon: {
            // 正多边形的包围盒随边数与起始角变，按顶点实算（sides 很小，不亏）。
            const float radius = detail::ShapePolygonCircumradius(width, height);
            if (radius <= 0.0f) {
                return ShapeBounds{w * 0.5f, h * 0.5f, 0.0f, 0.0f};
            }
            const int sides = detail::ShapePolygonSides(shape);
            const detail::ShapePoint first =
                detail::ShapePolygonVertex(shape, width, height, 0);
            float minX = first.x;
            float minY = first.y;
            float maxX = first.x;
            float maxY = first.y;
            for (int i = 1; i < sides; ++i) {
                const detail::ShapePoint point =
                    detail::ShapePolygonVertex(shape, width, height, i);
                minX = point.x < minX ? point.x : minX;
                minY = point.y < minY ? point.y : minY;
                maxX = point.x > maxX ? point.x : maxX;
                maxY = point.y > maxY ? point.y : maxY;
            }
            return ShapeBounds{minX, minY, maxX - minX, maxY - minY};
        }
        case ShapeKind::Star: {
            // 星形的顶点都在外接圆上 → 包围盒就是那个外接正方形的内切圆框
            // （和 circle 同一个约定：居中、直径 min(w,h)）。
            const float diameter = w < h ? w : h;
            return ShapeBounds{(w - diameter) * 0.5f, (h - diameter) * 0.5f,
                               diameter, diameter};
        }
        case ShapeKind::Arc: {
            // 圆弧带整个落在外径圆里 —— 保守但一定装得下（命中表还按它登记格子）。
            // 按实际角度范围算更紧，但那段代码只为了省几个格子，不值当。
            const float diameter = w < h ? w : h;
            return ShapeBounds{(w - diameter) * 0.5f, (h - diameter) * 0.5f,
                               diameter, diameter};
        }
        case ShapeKind::PolygonPath: {
            // 任意多边形的包围盒按顶点表实算（点数不多，不亏）。它可能只占
            // 框里的一小块，甚至是偏的——登记时用整框会白白多占格子。
            const int count = shape.pathPointCount;
            if (count < 3 || shape.pathPoints == nullptr) {
                return ShapeBounds{0.0f, 0.0f, w, h};
            }
            float minX = shape.pathPoints[0];
            float minY = shape.pathPoints[1];
            float maxX = minX;
            float maxY = minY;
            for (int i = 1; i < count; ++i) {
                const float px = shape.pathPoints[i * 2];
                const float py = shape.pathPoints[i * 2 + 1];
                minX = px < minX ? px : minX;
                minY = py < minY ? py : minY;
                maxX = px > maxX ? px : maxX;
                maxY = py > maxY ? py : maxY;
            }
            return ShapeBounds{minX, minY, maxX - minX, maxY - minY};
        }
        case ShapeKind::Capsule:
        case ShapeKind::ChamferRect:
        case ShapeKind::Superellipse:
        case ShapeKind::RoundedRect:
        case ShapeKind::Ellipse:
        case ShapeKind::Rect:
        default: {
            // 这几种都用满整个矩形：圆角只是把四角磨掉、切角只是把角切掉、
            // 胶囊两端就是矩形边界、超椭圆在 (±a,0)/(0,±b) 处顶到边——都不影响包围盒。
            return ShapeBounds{0.0f, 0.0f, w, h};
        }
    }
}

}  // namespace inking
