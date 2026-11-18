#pragma once

//InkingShapeSpec是InkingUIKit的形状层，它基于的是SDF这种几何表示法，是UI常见的表示方式

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cmath>
#include <limits>

//形状种类枚举
enum class InkingShapeKind : std::uint8_t {
    Rect = 0,//矩形
    RoundedRect,//圆角矩形
    Circle,//圆
    Ellipse,//椭圆
    Capsule,//胶囊体
    Ring,//圆环
    Line,//线
    Polygon,//多边形
    ChamferRect,  // 切角矩形
    Star, // 星形
    Superellipse, // 超椭圆
    Arc, // 圆弧
    PolygonPath,  // 顶点表给出来的任意多边形
};

//线段种类枚举
enum class InkingLineCap : std::uint8_t {
    Butt = 0,  // 平头
    Round,     // 圆头
    Square,    // 方头
};


//形状的结构体，这是一种假反射。对于有域的属性，会进行夹取来避免不合理的属性
struct InkingShapeSpec {
    InkingShapeKind kind = InkingShapeKind::Rect;//形状种类
    float radius = 0.0f;//圆角弧度，只对圆角矩形RoundedRect有用
    float thickness = 0.0f;//线宽，对于圆环Ring和线Line有用
    InkingLineCap cap = InkingLineCap::Butt;//线头，对于线Line有用
    int sides = 0;//边数，只对多边形Polygon有用
    float rotation = 0.0f;//起始角，对多边形Polygon和星形Star有用。0表示第一个顶点落在 +x（正右方），其余顶点按 360/sides 顺时针排。
    int points = 3;//角的数量，只对星形Star有用。
    float innerRatio = 0.5f;//内半径和外半径的比例，越小角越尖，只对星形Star有用
    float chamfer = 0.0f;//切角沿边的长度，只对切角矩形ChamferRect有用
    float exponent = 2.0f;// 指数 n，n = 2 是椭圆，越大越接近矩形，只对Superellipse超椭圆有用
    float startAngle = 0.0f;//起始角，只对圆弧Arc有用。0=正右方
    float sweepAngle = 360.0f;//扫过的角度，只对圆弧Arc有意义。
    // 借用的顶点数据；手动构造时，调用方须保证其生命周期覆盖所有使用者。
    const float* pathPoints = nullptr; // 顶点表（x0,y0,…)；仅任意多边形 PolygonPath有用
    int pathPointCount = 0;// 顶点数

    /*
    直角矩形Rect
    宽，采用组件的width，影响矩形的宽度，结构体中无域
    高，采用组件的height，影响矩形的高度，结构体中无域
    夹取规则：无
    */
    static constexpr InkingShapeSpec Rect() { return InkingShapeSpec{}; }

    /*
    圆角矩形RoundedRect
    半径，采用入参radius，影响四个角的圆角半径，结构体中域为 radius
    宽，采用组件的width，影响矩形的宽度与半径上限，结构体中无域
    高，采用组件的height，影响矩形的高度与半径上限，结构体中无域
    夹取规则：负半径断言拦截（配置错误，不自动修正）；超过 min(width,height)/2 夹到上限（形状自身的数学要求）
    */
    static InkingShapeSpec RoundedRect(float radius) {
        InkingShapeSpec spec;
        spec.kind = InkingShapeKind::RoundedRect;
        assert(radius >= 0.0f && "shape:radius 不能为负");
        spec.radius = radius < 0.0f ? 0.0f : radius;
        return spec;
    }

    /*
    正圆Circle
    宽，采用组件的width，影响直径取值（直径 = min(width,height)），结构体中无域
    高，采用组件的height，影响直径取值（直径 = min(width,height)），结构体中无域
    夹取规则：无（宽高不等时按短边定直径，另一方向居中留白）
    */
    static constexpr InkingShapeSpec Circle() {
        InkingShapeSpec spec;
        spec.kind = InkingShapeKind::Circle;
        return spec;
    }

    /*
    椭圆Ellipse
    宽，采用组件的width，影响横向半轴（= width/2），结构体中无域
    高，采用组件的height，影响纵向半轴（= height/2），结构体中无域
    夹取规则：无（宽或高为 0 时自然退化成线或点）
    */
    static constexpr InkingShapeSpec Ellipse() {
        InkingShapeSpec spec;
        spec.kind = InkingShapeKind::Ellipse;
        return spec;
    }

    /*
    胶囊Capsule
    宽，采用组件的width，影响胶囊沿水平方向的长度与两端半径 min(width,height)/2，结构体中无域
    高，采用组件的height，影响胶囊沿垂直方向的长度与两端半径 min(width,height)/2，结构体中无域
    夹取规则：无（用满 width × height，两端半圆半径由短边决定）
    */
    static constexpr InkingShapeSpec Capsule() {
        InkingShapeSpec spec;
        spec.kind = InkingShapeKind::Capsule;
        return spec;
    }

    /*
    圆环Ring
    环宽，采用入参thickness，影响环带的厚度（从外径往里长），结构体中域为 thickness
    宽，采用组件的width，影响外径取值（外径 = min(width,height)），结构体中无域
    高，采用组件的height，影响外径取值（外径 = min(width,height)），结构体中无域
    夹取规则：环宽须大于 0，否则断言拦截（配置错误）；超过外半径则夹到外半径（再厚就没有孔了，退化成实心圆）
    */
    static InkingShapeSpec Ring(float thickness) {
        InkingShapeSpec spec;
        spec.kind = InkingShapeKind::Ring;
        // 环宽没有"负"这种语义
        assert(thickness > 0.0f && "shape:thickness 必须大于 0");
        spec.thickness = thickness > 0.0f ? thickness : 0.0f;
        return spec;
    }

    /*
    水平线段Line
    线宽，采用入参thickness，影响线段的粗细（纵向），结构体中域为 thickness
    线头，采用入参cap，影响两端端头的形状（平头/圆头/方头），结构体中域为 cap
    宽，采用组件的width，影响线段的长度，结构体中无域
    高，采用组件的height，影响线段的纵向居中位置，结构体中无域
    夹取规则：线宽须大于 0，否则断言拦截（配置错误）；无自动夹取
    */
    static InkingShapeSpec Line(float thickness, InkingLineCap cap = InkingLineCap::Butt) {
        InkingShapeSpec spec;
        spec.kind = InkingShapeKind::Line;
        assert(thickness > 0.0f && "shape:thickness 必须大于 0");
        spec.thickness = thickness > 0.0f ? thickness : 0.0f;
        spec.cap = cap;
        return spec;
    }

    /*
    正多边形Polygon
    边数，采用入参sides，影响边的条数，结构体中域为 sides
    起始角，采用入参rotation（度），影响第一个顶点的方向（0 = +x 正右方，其余顶点按 360/sides 顺时针排），结构体中域为 rotation
    宽，采用组件的width，影响外接圆半径（= min(width,height)/2），结构体中无域
    高，采用组件的height，影响外接圆半径（= min(width,height)/2），结构体中无域
    夹取规则：边数小于 3 断言拦截（配置错误）；无自动夹取
    */
    static InkingShapeSpec Polygon(int sides, float rotation = 0.0f) {
        InkingShapeSpec spec;
        spec.kind = InkingShapeKind::Polygon;
        assert(sides >= 3 && "shape:sides 必须 >= 3");
        spec.sides = sides >= 3 ? sides : 3;
        spec.rotation = rotation;
        return spec;
    }

    /*
    切角矩形ChamferRect
    切角，采用入参chamfer，影响四个角各切掉的 45° 斜边长度，结构体中域为 chamfer
    宽，采用组件的width，影响矩形的宽度与切角上限，结构体中无域
    高，采用组件的height，影响矩形的高度与切角上限，结构体中无域
    夹取规则：负切角断言拦截（配置错误）；超过短边的一半则夹到短边的一半（再长就没有直边了）
    */
    static InkingShapeSpec ChamferRect(float chamfer) {
        InkingShapeSpec spec;
        spec.kind = InkingShapeKind::ChamferRect;
        assert(chamfer >= 0.0f && "shape:chamfer 不能为负");
        spec.chamfer = chamfer < 0.0f ? 0.0f : chamfer;
        return spec;
    }

    /*
    星形Star
    角数，采用入参points，影响角的数量，结构体中域为 points
    内外比，采用入参innerRatio，影响内半径与外半径之比（越小角越尖），结构体中域为 innerRatio
    起始角，采用入参rotation（度），影响第一个外角的方向，结构体中域为 rotation
    宽，采用组件的width，影响外接圆半径（= min(width,height)/2），结构体中无域
    高，采用组件的height，影响外接圆半径（= min(width,height)/2），结构体中无域
    夹取规则：角数小于 3 断言拦截（配置错误）；内外比夹到 [0,1]（0 = 内半径为 0 的尖角，1 = 退化成圆，都合法，超出就不是星形了）
    */
    static InkingShapeSpec Star(int points, float innerRatio = 0.5f,
                          float rotation = 0.0f) {
        InkingShapeSpec spec;
        spec.kind = InkingShapeKind::Star;
        assert(points >= 3 && "shape:points 必须 >= 3");
        spec.points = points >= 3 ? points : 3;
        spec.innerRatio = std::clamp(innerRatio, 0.0f, 1.0f);
        spec.rotation = rotation;
        return spec;
    }

    /*
    超椭圆Superellipse
    指数，采用入参exponent，影响形状的方度（2 = 椭圆，越大越接近矩形），结构体中域为 exponent
    宽，采用组件的width，影响横向半轴（= width/2），结构体中无域
    高，采用组件的height，影响纵向半轴（= height/2），结构体中无域
    夹取规则：指数须大于 0，否则断言拦截（配置错误）；夹到 [0.05,16]（太小退化成十字线，太大 pow 在远离中心处溢出）
    */
    static InkingShapeSpec Superellipse(float exponent) {
        InkingShapeSpec spec;
        spec.kind = InkingShapeKind::Superellipse;
        assert(exponent > 0.0f && "shape:exponent 必须大于 0");
        spec.exponent = std::clamp(exponent, 0.05f, 16.0f);
        return spec;
    }

    /*
    圆弧Arc
    环宽，采用入参thickness，影响弧带的厚度（从外径往里长），结构体中域为 thickness
    起始角，采用入参startAngle（度），影响弧的起点方向（0 = +x 正右方），结构体中域为 startAngle
    扫角，采用入参sweepAngle（度），影响从起点顺时针扫过的角度（负值表示逆时针），结构体中域为 sweepAngle
    宽，采用组件的width，影响外径取值（外径 = min(width,height)），结构体中无域
    高，采用组件的height，影响外径取值（外径 = min(width,height)），结构体中无域
    夹取规则：环宽须大于 0，否则断言拦截（配置错误）；环宽超过外半径则夹到外半径；扫角取绝对值后夹到 [0,360]，
    到 360 就退化成整圈圆环（数学要求，夹住而不报错）；两端径向切平，不做圆头
    */
    static InkingShapeSpec Arc(float thickness, float startAngle, float sweepAngle) {
        InkingShapeSpec spec;
        spec.kind = InkingShapeKind::Arc;
        assert(thickness > 0.0f && "shape:thickness 必须大于 0");
        spec.thickness = thickness > 0.0f ? thickness : 0.0f;
        spec.startAngle = startAngle;
        spec.sweepAngle = sweepAngle;
        return spec;
    }

    /*
    任意多边形PolygonPath
    顶点表，采用入参points（x0,y0,x1,y1,… 的设计坐标），影响多边形轮廓，结构体中域为 pathPoints（不拷贝，生命周期须覆盖使用者）
    顶点数，采用入参count，影响读取的顶点个数（是点数、不是浮点数个数，至少 3），结构体中域为 pathPointCount
    宽，不参与几何计算（顶点已是设计坐标），结构体中无域
    高，不参与几何计算（顶点已是设计坐标），结构体中无域
    夹取规则：顶点表为空或顶点数小于 3 断言拦截（配置错误）；非法输入时顶点数落 0，填充与命中退化成整框
    */
    static InkingShapeSpec PolygonPath(const float* points, int count) {
        InkingShapeSpec spec;
        spec.kind = InkingShapeKind::PolygonPath;
        assert(points != nullptr && count >= 3 && "polygonPath 至少要 3 个点");
        spec.pathPoints = points;
        spec.pathPointCount = (points != nullptr && count >= 3) ? count : 0;
        return spec;
    }

    //静态辅助函数，夹取半径，满足形状上的数学要求
    static float ClampRadius(float radius, int width, int height) {
        const float shorter =
            static_cast<float>(width < height ? width : height);
        const float limit = shorter * 0.5f;
        if (radius < 0.0f) {
            return 0.0f;
        }
        return radius > limit ? limit : radius;
    }

    //实例辅助函数，夹取半径，满足形状上的数学要求
    float EffectiveRadius(int width, int height) const {
        return ClampRadius(radius, width, height);
    }
};

// 内部小工具。名字统一带 Shape/shape前缀
namespace inking_detail {

constexpr float shapePi = 3.14159265358979323846f;//圆周率
constexpr float shapeDegToRad = shapePi / 180.0f;//弧度

// 相对于组件左上角的一个点。只为形状内部算几何用，不进公开接口。
struct ShapePoint {
    float x = 0.0f;
    float y = 0.0f;
};

//SDF几何辅助函数：用于确定点px，py和矩形的关系
inline float ShapeBoxDistance(float px, float py, float cx, float cy,
                              float halfW, float halfH) {
    //先将点px，py处理，将全部象限的情况统一成一个象限的内容
    const float qx = std::fabs(px - cx) - halfW;
    const float qy = std::fabs(py - cy) - halfH;

    /*
    使用0来截取qx和qy。
    因为qx和qy还需要在下方继续使用，所以使用outsideX和outsideY来代替计算
    如果qx或者qy其中一方大于零，那么求得的outside就会大于零。否则outside就为0。
    这是情况A
    */
    const float outsideX = qx > 0.0f ? qx : 0.0f;
    const float outsideY = qy > 0.0f ? qy : 0.0f;
    const float outside = std::sqrt(outsideX * outsideX + outsideY * outsideY);

    /*
    使用0来截取qx和qy
    先求出qx和qy中大的那一个，如果大的那一个小于0，说明双方都小于0
    只有小于0的情况下，inside才会为负数
    这是情况B
    */ 
    const float nearer = qx > qy ? qx : qy;
    const float inside = nearer < 0.0f ? nearer : 0.0f;

    /*
     情况A和情况B天然互斥，其中一方满足条件，另一方就为0
     所以返回的值就是满足条件情况的那个值 
     */
    return outside + inside;
}
// SDF几何辅助函数：用于确定点px，py到由点bx，by和ax，ay组成的线段的距离
inline float ShapeSegmentDistance(float px, float py, float ax, float ay,float bx, float by) {
    /*
    先使用向量法求出线段的长度
    */
    const float ex = bx - ax;
    const float ey = by - ay;
    const float lengthSquared = ex * ex + ey * ey;

    /*
    根据三角函数的相关知识，推导出t
    t是线段ab上，做p到ab的垂线，垂点p‘，ap’/p‘b的比值
    算法是很简单的推导，根据三点组成三角形的边长关系推导出cosPAB
    再根据cosPAC求得t
    由于点和线段有多种关系，如果点在线段的延长线上，那么要取的距离是最近的那个线段
    如果点不在线段的延长线上，那么要取的是垂线的长度
    clamp就是做了这个选择
    */
    float t = 0.0f;
    if (lengthSquared > 0.0f) {
        t = ((px - ax) * ex + (py - ay) * ey) / lengthSquared;
        t = std::clamp(t, 0.0f, 1.0f);
    }
    /*
    最后基于各种值，算出dx和dy，再算出长度
     */
    const float dx = px - (ax + ex * t);
    const float dy = py - (ay + ey * t);
    return std::sqrt(dx * dx + dy * dy);
}

//SDF夹取辅助函数：让thickness半价夹取到0到min(width,height)/2之中
inline float ShapeRingThickness(float thickness, int width, int height) {
    const float shorter = static_cast<float>(width < height ? width : height);
    const float outerRadius = shorter * 0.5f;
    if (thickness <= 0.0f) {
        return 0.0f;
    }
    return thickness > outerRadius ? outerRadius : thickness;
}

// SDF夹取辅助函数：确保多边形的边数至少为3
inline int ShapePolygonSides(const InkingShapeSpec& shape) {
    return shape.sides >= 3 ? shape.sides : 3;
}

// SDF夹取辅助函数：让多边形的外接圆半价夹取到0到min(width,height)/2之中
inline float ShapePolygonCircumradius(int width, int height) {
    const float shorter = static_cast<float>(width < height ? width : height);
    return shorter * 0.5f;
}

// SDF几何辅助函数：获取到正多边形第n个顶点的位置，相对于(0,0)
inline ShapePoint ShapePolygonVertex(const InkingShapeSpec& shape, int width,
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

//SDF夹取辅助函数：将星形的角数夹取到>=3
inline int ShapeStarPoints(const InkingShapeSpec& shape) {
    return shape.points >= 3 ? shape.points : 3;
}

// SDF夹取辅助函数：让星形的外接圆半价夹取到0到min(width,height)/2之中
inline float ShapeStarOuterRadius(int width, int height) {
    const float shorter = static_cast<float>(width < height ? width : height);
    return shorter * 0.5f;
}

//SDF夹取辅助函数：让星形的内接圆比例夹取到[0,1]
inline float ShapeStarInnerRadius(const InkingShapeSpec& shape, int width, int height) {
    const float ratio = std::clamp(shape.innerRatio, 0.0f, 1.0f);
    return ShapeStarOuterRadius(width, height) * ratio;
}

//SDF几何辅助函数：获取到星形第k个定点的位置，相对于(0,0)
inline ShapePoint ShapeStarVertex(const InkingShapeSpec& shape, int width, int height,
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

// SDF夹取辅助函数：将切角矩形被切去的等腰三角形的纵边/横边夹取到下限0和上限为矩形较短那边的一半
inline float ShapeChamferAmount(float chamfer, float w, float h) {
    const float shorter = w < h ? w : h;
    const float limit = shorter * 0.5f;
    if (chamfer <= 0.0f) {
        return 0.0f;
    }
    return chamfer > limit ? limit : chamfer;
}

//SDF夹取辅助函数：将超椭圆的比值夹取到一个合理的范围内
inline float ShapeSuperellipseExponent(const InkingShapeSpec& shape) {
    return std::clamp(shape.exponent, 0.05f, 16.0f);
}

// SDF夹取辅助函数：将圆弧的thickness和outerRadius夹取到能够不退化的范围
inline float ShapeArcThickness(float thickness, float outerRadius) {
    if (thickness <= 0.0f) {
        return 0.0f;
    }
    return thickness > outerRadius ? outerRadius : thickness;
}

//SDF夹取辅助函数：把圆弧扫角的幅值夹到 0..360 度，再转成弧度
inline float ShapeArcSweepRadians(const InkingShapeSpec& shape) {
    const float degrees = std::clamp(std::fabs(shape.sweepAngle), 0.0f, 360.0f);
    return degrees * shapeDegToRad;
}

//SDF几何辅助函数：给出圆弧的扫向符号，+1 顺时针、-1 逆时针
inline float ShapeArcDirection(const InkingShapeSpec& shape) {
    return shape.sweepAngle < 0.0f ? -1.0f : 1.0f;
}

//SDF几何辅助函数：取出顶点表第 k 个顶点，设计坐标、相对组件左上角、对点数取模
inline ShapePoint ShapePathVertex(const InkingShapeSpec& shape, int k) {
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

template <typename VertexAt>
//SDF几何辅助函数，计算点(x,y)到多边形边界的最短距离，并用符号表示是否在边界内部，为星形和多边形服务
inline float ShapePolygonSdf(int count, VertexAt vertexAt, float x, float y) {
    if (count < 3) {
        // 顶点不足以构成多边形，无法定义有效的带符号距离。
        return 0.0f;
    }
    // 射线与边的交点采用奇偶规则判定内外，因此也适用于凹多边形。
    bool inside = false;
    // 保存点到所有边的最小距离，作为结果的绝对值。
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
    // SDF符号约定：内部为负，外部为正。
    return inside ? -nearest : nearest;
}

}  // namespace inking_detail

/**
 * @brief 点的带符号距离，单位是设计坐标。
 * @param x,y 相对组件左上角的坐标
 * @return f < 0 在内部、f = 0 在边界、f > 0 在外部
 */
inline float InkingSignedDistance(const InkingShapeSpec& shape, float x, float y, int width,
                            int height) {
    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);

    switch (shape.kind) {
        case InkingShapeKind::RoundedRect: {
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

        case InkingShapeKind::Circle: {
            // 居中、直径取 min(w,h)：所以半径是 min(w,h)/2，圆心是 (w/2, h/2)。
            const float r = (w < h ? w : h) * 0.5f;
            const float dx = x - w * 0.5f;
            const float dy = y - h * 0.5f;
            return std::sqrt(dx * dx + dy * dy) - r;
        }

        case InkingShapeKind::Ellipse: {
            // 椭圆的 SDF 没有精确闭式（真值要解数值最近点），
            // 但这里要的是符号正确的近似：内外判定精确，
            // 距离在边缘附近也够准。
            //
            // 写法：在归一化空间算到单位圆的距离，再乘回最小半径——
            // 于是内部为负、外部为正、边界为 0，`InkingShapeContains` 永远正确。
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

        case InkingShapeKind::Capsule: {
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
            return inking_detail::ShapeSegmentDistance(x, y, ax, ay, bx, by) - radius;
        }

        case InkingShapeKind::Ring: {
            // 圆环：到"中径圆"的距离再减去半个环宽——内外两侧合起来正好
            // 就是到环带边界的距离（|d − rMid| − t/2）。
            const float outerRadius = (w < h ? w : h) * 0.5f;
            const float thickness =
                inking_detail::ShapeRingThickness(shape.thickness, width, height);
            const float midRadius = outerRadius - thickness * 0.5f;
            const float dx = x - w * 0.5f;
            const float dy = y - h * 0.5f;
            const float distance = std::sqrt(dx * dx + dy * dy);
            // t = 0 时 |d − rOuter| >= 0：孔里孔外都在外面，只有那圈线算"在内"。
            return std::fabs(distance - midRadius) - thickness * 0.5f;
        }

        case InkingShapeKind::Line: {
            // 水平线段、纵向居中：起点 (0, h/2)、终点 (w, h/2)。
            const float centerY = h * 0.5f;
            const float half = shape.thickness * 0.5f;
            if (shape.cap == InkingLineCap::Round) {
                // 圆头 = 到线段距离 − 半径（两端自然长出半圆）。
                return inking_detail::ShapeSegmentDistance(x, y, 0.0f, centerY, w,
                                                    centerY)
                     - half;
            }
            // 平头 / 方头都是矩形：方头只是两端各外扩 half。
            const float extra = shape.cap == InkingLineCap::Square ? half : 0.0f;
            return inking_detail::ShapeBoxDistance(x, y, w * 0.5f, centerY,
                                            w * 0.5f + extra, half);
        }

        case InkingShapeKind::Polygon: {
            // 正多边形（凸）：先取"到各边所在直线"的最大带符号距离当符号与内部
            // 距离（f <= 0 恰好等价于"在每条边的内侧"），外部再用"到边界折线
            // 的距离"取精确值——两条一起就是精确 SDF。
            const float radius = inking_detail::ShapePolygonCircumradius(width, height);
            if (radius <= 0.0f) {
                const float dx = x - w * 0.5f;
                const float dy = y - h * 0.5f;
                return std::sqrt(dx * dx + dy * dy);  // 退化成中心一个点
            }
            const int sides = inking_detail::ShapePolygonSides(shape);
            const float centerX = w * 0.5f;
            const float centerY = h * 0.5f;

            float maxPlaneDistance = -std::numeric_limits<float>::max();
            float minEdgeDistance = std::numeric_limits<float>::max();
            for (int i = 0; i < sides; ++i) {
                const inking_detail::ShapePoint a =
                    inking_detail::ShapePolygonVertex(shape, width, height, i);
                const inking_detail::ShapePoint b =
                    inking_detail::ShapePolygonVertex(shape, width, height, i + 1);

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
                    inking_detail::ShapeSegmentDistance(x, y, a.x, a.y, b.x, b.y);
                if (edge < minEdgeDistance) {
                    minEdgeDistance = edge;
                }
            }
            return maxPlaneDistance <= 0.0f ? maxPlaneDistance : minEdgeDistance;
        }

        case InkingShapeKind::ChamferRect: {
            // 切角矩形 = 八个半平面的交（四条边 + 四条 45° 斜边）。
            // 凸集的 SDF 就是"到各面所在平面的带符号距离取最大"——精确，不用近似。
            const float chamfer =
                inking_detail::ShapeChamferAmount(shape.chamfer, w, h);
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

        case InkingShapeKind::Star: {
            // 星形非凸，没有"半平面取最大"那条捷径了。做法回到最朴素、
            // 也最不容易错的一种：符号用射线法、距离用点到边界的最近距离
            // （和 `PolygonPath` 共用一份实现）。
            const int count = inking_detail::ShapeStarPoints(shape) * 2;
            return inking_detail::ShapePolygonSdf(
                count,
                [&](int k) {
                    return inking_detail::ShapeStarVertex(shape, width, height, k);
                },
                x, y);
        }

        case InkingShapeKind::PolygonPath: {
            // 任意多边形：同一份"射线法 + 最近边"的实现。它可以非凸，
            // 甚至可以不是星形域（U 形那种，中心落在凹口里）——命中与填充
            // 都得照样对。
            const int count = shape.pathPointCount;
            if (count < 3 || shape.pathPoints == nullptr) {
                return 0.0f;  // 顶点表不成立：退化成"整个框都算在内"
            }
            return inking_detail::ShapePolygonSdf(
                count, [&](int k) { return inking_detail::ShapePathVertex(shape, k); },
                x, y);
        }

        case InkingShapeKind::Superellipse: {
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
            const float n = inking_detail::ShapeSuperellipseExponent(shape);
            const float u = std::fabs(x - a) / a;
            const float v = std::fabs(y - b) / b;
            const float f = std::pow(std::pow(u, n) + std::pow(v, n), 1.0f / n);
            const float scale = a < b ? a : b;
            return (f - 1.0f) * scale;
        }

        case InkingShapeKind::Arc: {
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
                inking_detail::ShapeArcThickness(shape.thickness, outerRadius);
            const float midRadius = outerRadius - thickness * 0.5f;
            const float cx = w * 0.5f;
            const float cy = h * 0.5f;
            const float dx = x - cx;
            const float dy = y - cy;
            const float radius = std::sqrt(dx * dx + dy * dy);
            const float half = thickness * 0.5f;
            const float radial = std::fabs(radius - midRadius) - half;
            const float sweep = inking_detail::ShapeArcSweepRadians(shape);

            // 扫满一圈就只剩环带了（没有端头）。
            if (sweep >= 2.0f * inking_detail::shapePi - 1e-4f) {
                return radial;
            }
            if (thickness <= 0.0f) {
                // 退化成一条弧线：只有径向距离，端头照样要切平。
            }

            const float direction = inking_detail::ShapeArcDirection(shape);
            const float start = shape.startAngle * inking_detail::shapeDegToRad;
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

        case InkingShapeKind::Rect:
        default: {
            // 直角矩形：把点按两条中轴线折叠到右下象限再算（见 ShapeBoxDistance）。
            return inking_detail::ShapeBoxDistance(x, y, w * 0.5f, h * 0.5f, w * 0.5f,
                                            h * 0.5f);
        }
    }
}

/**
 * @brief 这个点在不在形状里（命中判定）。
 *
 * 与 `InkingSignedDistance` 同源：判据就是 `f <= 0`，和裁剪保留的区域完全一致，
 * 不会出现"画的是圆角、点到的是直角"。
 */
inline bool InkingShapeContains(const InkingShapeSpec& shape, float x, float y, int width,
                          int height) {
    return InkingSignedDistance(shape, x, y, width, height) <= 0.0f;
}

/**
 * @brief 形状的包围盒（AABB），相对组件左上角。
 *
 * 将来命中表按格子索引时就问这个：半径不同、形状不同，包围盒也可能不同
 * （circle 只占中间一块），所以不能一律拿 width × height 顶上。
 */
struct InkingShapeBounds {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

inline InkingShapeBounds InkingGetShapeBounds(const InkingShapeSpec& shape, int width,
                                  int height) {
    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);

    switch (shape.kind) {
        case InkingShapeKind::Circle: {
            // 居中 + 直径 min(w,h)：宽高不等时两侧会留白，包围盒也跟着小。
            const float diameter = w < h ? w : h;
            return InkingShapeBounds{(w - diameter) * 0.5f, (h - diameter) * 0.5f,
                               diameter, diameter};
        }
        case InkingShapeKind::Ring: {
            // 环带是从外径往里长的，所以包围盒就是外径那个圆（和 circle 一样）。
            const float diameter = w < h ? w : h;
            return InkingShapeBounds{(w - diameter) * 0.5f, (h - diameter) * 0.5f,
                               diameter, diameter};
        }
        case InkingShapeKind::Line: {
            // 线是水平的：纵向是 thickness 那一条，横向按端点形状外扩。
            // 别让包围盒只用"线段本身"（高度是 0）——命中表按它登记格子，
            // 登记不全就是漏命中（TaskGuide T-1 的坑）。
            const float thickness = shape.thickness > 0.0f ? shape.thickness : 0.0f;
            const float extra = shape.cap == InkingLineCap::Butt ? 0.0f
                                                        : thickness * 0.5f;
            return InkingShapeBounds{-extra, (h - thickness) * 0.5f, w + 2.0f * extra,
                               thickness};
        }
        case InkingShapeKind::Polygon: {
            // 正多边形的包围盒随边数与起始角变，按顶点实算（sides 很小，不亏）。
            const float radius = inking_detail::ShapePolygonCircumradius(width, height);
            if (radius <= 0.0f) {
                return InkingShapeBounds{w * 0.5f, h * 0.5f, 0.0f, 0.0f};
            }
            const int sides = inking_detail::ShapePolygonSides(shape);
            const inking_detail::ShapePoint first =
                inking_detail::ShapePolygonVertex(shape, width, height, 0);
            float minX = first.x;
            float minY = first.y;
            float maxX = first.x;
            float maxY = first.y;
            for (int i = 1; i < sides; ++i) {
                const inking_detail::ShapePoint point =
                    inking_detail::ShapePolygonVertex(shape, width, height, i);
                minX = point.x < minX ? point.x : minX;
                minY = point.y < minY ? point.y : minY;
                maxX = point.x > maxX ? point.x : maxX;
                maxY = point.y > maxY ? point.y : maxY;
            }
            return InkingShapeBounds{minX, minY, maxX - minX, maxY - minY};
        }
        case InkingShapeKind::Star: {
            // 星形的顶点都在外接圆上 → 包围盒就是那个外接正方形的内切圆框
            // （和 circle 同一个约定：居中、直径 min(w,h)）。
            const float diameter = w < h ? w : h;
            return InkingShapeBounds{(w - diameter) * 0.5f, (h - diameter) * 0.5f,
                               diameter, diameter};
        }
        case InkingShapeKind::Arc: {
            // 圆弧带整个落在外径圆里 —— 保守但一定装得下（命中表还按它登记格子）。
            // 按实际角度范围算更紧，但那段代码只为了省几个格子，不值当。
            const float diameter = w < h ? w : h;
            return InkingShapeBounds{(w - diameter) * 0.5f, (h - diameter) * 0.5f,
                               diameter, diameter};
        }
        case InkingShapeKind::PolygonPath: {
            // 任意多边形的包围盒按顶点表实算（点数不多，不亏）。它可能只占
            // 框里的一小块，甚至是偏的——登记时用整框会白白多占格子。
            const int count = shape.pathPointCount;
            if (count < 3 || shape.pathPoints == nullptr) {
                return InkingShapeBounds{0.0f, 0.0f, w, h};
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
            return InkingShapeBounds{minX, minY, maxX - minX, maxY - minY};
        }
        case InkingShapeKind::Capsule:
        case InkingShapeKind::ChamferRect:
        case InkingShapeKind::Superellipse:
        case InkingShapeKind::RoundedRect:
        case InkingShapeKind::Ellipse:
        case InkingShapeKind::Rect:
        default: {
            // 这几种都用满整个矩形：圆角只是把四角磨掉、切角只是把角切掉、
            // 胶囊两端就是矩形边界、超椭圆在 (±a,0)/(0,±b) 处顶到边——都不影响包围盒。
            return InkingShapeBounds{0.0f, 0.0f, w, h};
        }
    }
}

